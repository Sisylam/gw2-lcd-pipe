/*
 * lgpipe_server.exe - minimal stand-in for the Logitech LCD service pipe.
 *
 * WHY THIS EXISTS
 * ---------------
 * Guild Wars 2 loads LgLcdApi.dll, which is a named-pipe CLIENT. It ships the
 * rendered bitmap out over
 *
 *     \\.\pipe\LGLCDPIPE-00000001
 *
 * and the server end of that pipe is the Logitech Gaming Software process
 * (LCore.exe). Verified experimentally:
 *
 *   - With LCore.exe killed, GW2's CreateFileW returns INVALID_HANDLE_VALUE
 *     four times, it writes zero bytes, and it never retries for the rest of
 *     the session. It renders nothing.
 *   - With LCore.exe running, GW2 writes a 1084-byte negotiation packet and
 *     then 307224-byte frames unprompted.
 *
 * So the pipe is load-bearing, and LCore.exe cannot simply be uninstalled.
 * This process creates the same pipe so GW2 has something to talk to, and
 * drains it. It deliberately does NOT implement the hardware side: nobody is
 * feeding a G19 panel, which is fine, because the frames are consumed out of
 * band by lcdproxy.dll publishing them to the shared-memory mapping.
 *
 * WHAT IT DOES *NOT* DO
 * ---------------------
 * It writes nothing back. The read side of the protocol carries small
 * soft-button notification messages (LCore answers a 560-byte read request
 * with 16 bytes), and GW2 is the only party that cares about G19 LCD buttons.
 * The first version deliberately answers nothing so we can find out whether
 * GW2 renders regardless. If it stays silent, that is the signal that a reply
 * is mandatory, and we can add one without touching GW2's process - mistakes
 * here cost a log line, not a crash.
 *
 * WHY THIS IS SAFE TO ITERATE ON
 * ------------------------------
 * Nothing in GW2's address space is patched or wrapped. A wrong reply here
 * can only make GW2 show no LCD; it cannot crash the game. The previous
 * approach - wrapping ReadFileEx completion routines inside GW2 - did crash
 * it, because the completion-routine argument order differs between the
 * MinGW headers and the real Win32 ABI, and guessing it wrong dereferenced a
 * byte count as a pointer. Keeping the experiment in a separate process is
 * what makes the rest of this tractable.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#define PIPE_NAME    L"\\\\.\\pipe\\LGLCDPIPE-00000001"
#define LOG_PATH     L"lgpipe_server.log"
#define FRAME_BYTES  307224u
#define HANDSHAKE    1084u
#define MAX_MSG      (2u*1024u*1024u)
#define POLL_MS      250
#define INSTANCES    8
#define CONNECT_TIMEOUT_MS 1000

static volatile LONG g_stop = 0;
static volatile LONG g_alive = 0;
static volatile LONG g_sent_total = 0;

/*
 * View-button control channel.
 *
 * Local\LGLCDCtl, 32 bytes, created here and written by the viewer
 * (viewer/src/ctl.rs, formerly view_shim.py):
 *
 *   +0  u32 magic      CTL_MAGIC
 *   +4  u32 request    bumped by the viewer on every button press
 *   +8  u32 button     one of the BTN_* bits (0x100 .. 0x4000)
 *   +12 u32 served     last request the server has answered
 *
 * The request is queued, not sent immediately. A 0x0702 notification has to be
 * the answer to a read the client is already waiting on - LCore's captured
 * 0x0702 replies line up one-for-one with reads, same as everything else.
 * Injecting extra messages the moment a button is pressed is exactly the queue
 * discipline violation that produced the original black screens, so each
 * queued message is spent on the next reply instead.
 */
#define CTL_NAME    L"Local\\LGLCDCtl"
#define CTL_MAGIC   0x31544347u          /* "GCT1" */
#define CTL_BYTES   32

/*
 * Small queue of 0x0702 payloads waiting to be spent on replies.
 *
 * The last 4 bytes of a 0x0702 are the button bit. Read them the way the wire
 * stores them - little-endian, because the proxy log dumps raw bytes in memory
 * order (lcdproxy.c:166, "%02x" per byte). Getting this wrong is the trap this
 * block exists to document.
 *
 * The proxy dump for a real [>] press ends in the bytes
 *
 *     00 02 00 00        ->  DWORD 0x00000200
 *
 * but a previous revision read that 8-hex-char group as a big-endian number and
 * recorded 0x00020000. The whole "SETTLED" button table in docs/PROTOCOL.md was
 * byte-swapped the same way, which is why it claims bits 16-22. A re-parse of
 * the 756 captured 0x0702 messages shows the real nonzero values are only ever
 * 0x100 and 0x200 - bits 8 and 9 - so the original constants were right and the
 * "off by eight bits" rewrite was a regression.
 *
 * Measured bits, with the byte-swapped mistake each one replaces:
 *
 *     [<]   0x00000100   (was misread as 0x00010000)
 *     [>]   0x00000200   (was misread as 0x00020000)
 *     [ok]  0x00000400   (was misread as 0x00040000)
 *     [^]   0x00001000   (was misread as 0x00100000)
 *     [v]   0x00002000   (was misread as 0x00200000)
 *     [menu]0x00004000   (was misread as 0x00400000)
 *
 * [<] and [>] are confirmed directly from the captured bytes. The other four
 * come from byte-swapping the recorded table and still need a live press to
 * confirm which button owns which bit.
 *
 * A press is followed ~130-250 ms later by the same message with 0, and that
 * release is required: an earlier build sent only the press half and GW2
 * ignored it.
 *
 * The queue exists so the pair is spread across two replies rather than sent
 * back to back, which would put two messages in the pipe at once.
 */
#define PQ_MAX 8

/* Measured bit for each physical button on the G19S (little-endian). */
#define BTN_PREV   0x00000100u   /* [<]  */
#define BTN_NEXT   0x00000200u   /* [>]  */
#define BTN_OK     0x00000400u   /* [ok] */
#define BTN_UP     0x00001000u   /* [^]  */
#define BTN_DOWN   0x00002000u   /* [v]  */
#define BTN_MENU   0x00004000u   /* [menu] */

/* Every measured bit, for masking a request down to a known button. */
#define BTN_ALL    (BTN_PREV | BTN_NEXT | BTN_OK | BTN_UP | BTN_DOWN | BTN_MENU)

static volatile LONG g_pq[PQ_MAX];
static volatile LONG g_pq_head = 0;
static volatile LONG g_pq_tail = 0;

static void pq_push(DWORD v)
{
    LONG tail = g_pq_tail, head = g_pq_head;
    if (((tail + 1) % PQ_MAX) == head) return;        /* full: drop, never block */
    g_pq[tail] = (LONG)v;
    InterlockedExchange(&g_pq_tail, (tail + 1) % PQ_MAX);
}

static BOOL pq_pop(DWORD *out)
{
    LONG head = g_pq_head, tail = g_pq_tail;
    if (head == tail) return FALSE;
    *out = (DWORD)g_pq[head];
    InterlockedExchange(&g_pq_head, (head + 1) % PQ_MAX);
    return TRUE;
}

static int pq_count(void)
{
    return (int)((g_pq_tail - g_pq_head + PQ_MAX) % PQ_MAX);
}

static HANDLE        g_ctl_map = NULL;
static LONG          *g_ctl = NULL;
static volatile LONG g_ctl_seen = 0;

static void logline(const char* fmt, ...);   /* defined below */

static void ctl_open(void)
{
    if (g_ctl) return;
    g_ctl_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
                                   PAGE_READWRITE, 0, CTL_BYTES, CTL_NAME);
    if (!g_ctl_map) {
        logline("ctl: CreateFileMapping failed err=%u", GetLastError());
        return;
    }
    g_ctl = (LONG *)MapViewOfFile(g_ctl_map, FILE_MAP_ALL_ACCESS,
                                  0, 0, CTL_BYTES);
    if (!g_ctl) {
        logline("ctl: MapViewOfFile failed err=%u", GetLastError());
        CloseHandle(g_ctl_map);
        g_ctl_map = NULL;
        return;
    }
    if (g_ctl[0] != CTL_MAGIC) { g_ctl[0] = CTL_MAGIC; g_ctl[3] = 0; }

    /*
     * Adopt the counter that is already there. The mapping outlives the server,
     * so its request value belongs to the previous run; treating that as a new
     * press would fire a phantom button press on every startup.
     */
    InterlockedExchange(&g_ctl_seen, (LONG)g_ctl[1]);
    logline("ctl: %ls ready, adopting request=%lu",
            CTL_NAME, g_ctl[1]);
}

/* Latch a button press if the shim has posted one. Called once per loop. */
static void ctl_poll(void)
{
    if (!g_ctl || g_ctl[0] != CTL_MAGIC) return;

    /*
     * InterlockedExchange, not InterlockedCompareExchange: it always stores and
     * returns the previous value, which is the read-and-set we need. A
     * CompareExchange with Comparand == Exchange only stores when the
     * comparison *succeeds* - i.e. only when nothing changed - so it never
     * records a press, and every poll queues another pair forever.
     *
     * This runs on all 8 instance threads at once, hence the interlocked op.
     */
    LONG seen = (LONG)g_ctl[1];
    LONG prev = InterlockedExchange(&g_ctl_seen, seen);
    if (seen == prev) return;

    /*
     * Mask to the six measured bits. The previous mask was only
     * BTN_NEXT|BTN_PREV, which silently zeroed [ok], [^], [v] and [menu] and
     * then turned them into a [>] press via the fallback below.
     */
    DWORD btn = g_ctl[2] & BTN_ALL;
    if (!btn) {
        /* A request we do not recognise. Dropping it is safer than guessing:
         * inventing a press is how the flood bug looked in the first place. */
        logline("ctl: button #%ld carried no known bit (0x%lx), dropped",
                seen, (DWORD)g_ctl[2]);
        return;
    }

    /*
     * One pair per missed press, so clicks inside a single poll interval are not
     * collapsed. But only ONE pair may be outstanding: a flood of 0x0702
     * presses wedges GW2 (observed - it hung on a loading screen), so anything
     * that arrives while a pair is still unspent is dropped, not queued.
     */
    LONG delta = seen - prev;
    if (delta < 1) delta = 1;
    if (pq_count() >= 2) {
        logline("ctl: button #%ld (+%ld) dropped, a press is already pending",
                seen, delta);
        return;
    }
    if (delta > 1) delta = 1;

    pq_push(btn);
    pq_push(0);
    logline("ctl: button #%ld (+%ld) -> press 0x%lx + release, queued",
            seen, delta, btn);
}


/* Breadcrumb: last message this server handed the client. */
static char g_last_sent[256] = "(none)";
static CRITICAL_SECTION g_lock;
static LONG g_lines = 0;
#define LOG_CAP 20000

static void logline(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    buf[n] = 0;

    if (InterlockedIncrement(&g_lines) > LOG_CAP) return;
    EnterCriticalSection(&g_lock);
    HANDLE f = CreateFileW(LOG_PATH, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(f, buf, (DWORD)n, &w, NULL);
        CloseHandle(f);
    }
    LeaveCriticalSection(&g_lock);
}

/* This MinGW/SDK vintage predates both of these; declare them ourselves. */
typedef BOOL (WINAPI *pCancelIoEx)(HANDLE, LPOVERLAPPED);
typedef BOOL (WINAPI *pGetNamedPipeClientProcessId)(HANDLE, PULONG);
typedef BOOL (WINAPI *pGetNamedPipeInfo)(HANDLE, PULONG, PULONG, PULONG,
                                         PULONG, PULONG);
static pCancelIoEx                 g_CancelIoEx;
static pGetNamedPipeClientProcessId g_GetNamedPipeClientPid;
static pGetNamedPipeInfo           g_GetNamedPipeInfo;

static void cancel(HANDLE h, LPOVERLAPPED ov)
{
    if (g_CancelIoEx) g_CancelIoEx(h, ov);
    else CancelIo(h);
}

static BOOL WINAPI console_ctrl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    }
    return FALSE;
}

/* Sleep in small slices so the stop flag is honoured promptly. */
static void wait_slice(DWORD ms)
{
    DWORD left = ms;
    while (left && !g_stop) {
        DWORD chunk = left < 50 ? left : 50;
        Sleep(chunk);
        left -= chunk;
    }
}

/*
 * Wait for an overlapped operation, pumping the stop flag. Returns TRUE when
 * the event fired, FALSE on shutdown or timeout.
 */
static BOOL pump(HANDLE ev, DWORD timeout_ms)
{
    DWORD waited = 0;
    for (;;) {
        DWORD r = WaitForSingleObject(ev, POLL_MS);
        if (r == WAIT_OBJECT_0)
            return TRUE;
        if (r == WAIT_TIMEOUT) {
            waited += POLL_MS;
            if (g_stop) return FALSE;
            if (waited >= timeout_ms) return FALSE;
            continue;
        }
        return FALSE;
    }
}

/*
 * The protocol, captured from LCore.exe and confirmed against a live
 * single-press session (see docs/PROTOCOL.md for the full account).
 *
 * Every message is length-prefixed: `u32 length` counts the whole message
 * including itself, so framing is trivial. Client messages then carry a
 * sequence number and a type; server messages carry a type but no sequence.
 *
 * Client (GW2 -> server):
 *
 *     1084 B  type 0x0831  opening negotiation
 *       16 B  type 0x0803  short config/ack
 *       20 B  type 0x0830  region/page state
 *  307224 B  type 0x0806  24-byte header + 320*240 BGRA frame
 *
 * Server (server -> GW2) - the five-entry opening, then keepalives:
 *
 *       16 B  type 0x0900  config
 *       32 B  type 0x0703  device/handshake state
 *       12 B  type 0x0900  keepalive, third field counts up
 *       20 B  type 0x0702  soft-button press/release (last word = button bit)
 *
 * The opening is sent as two replies per client message; afterwards it is one
 * reply per client message. The exact bytes are in replay_table.inc.
 */

/*
 * Client message types, as seen in the captured stream. The client's header is
 *
 *     u32 length      total bytes in the message
 *     u32 seq         increments on every message the client sends
 *     u32 type
 *     ... payload
 *
 * w1 being a sequence counter, not a payload field, is what made an earlier
 * hand-built server wrong: the counter was read as data.
 */
#define C_NEGOTIATE  0x0831u   /* 1084 B opening negotiation  */
#define C_FRAME      0x0806u   /* 307224 B BGRA frame         */
#define C_SHORT      0x0803u   /* 16 B                        */
#define C_REGION     0x0830u   /* 20 B                        */
#define C_REGION2    0x0802u   /* 20 B                        */

#include "replay_table.inc"

/*
 * Write one message, correctly.
 *
 * The handle is created with FILE_FLAG_OVERLAPPED, so a synchronous
 * WriteFile(..., NULL) is invalid usage on it and comes back as
 * ERROR_NO_DATA (232) with zero bytes written. That is exactly the failure the
 * logs showed on every handshake, and it is a bug on our side, not something
 * the client did wrong.
 *
 * Note the client opens the pipe with acc=c0000000, i.e.
 * GENERIC_READ | GENERIC_WRITE, so it can read. The earlier
 * "GW2 only opened write-only" idea was wrong and is gone.
 */
static BOOL write_msg(HANDLE pipe, const DWORD *words, DWORD nwords,
                      long id, const char *what)
{
    BYTE       buf[64];
    DWORD      n = nwords * 4;
    OVERLAPPED ov;
    HANDLE     ev;
    DWORD      put = 0;
    BOOL       ok = FALSE;
    /* NULL means "sent, but not worth a log line" - see the keepalive sampler. */
    const char *label = what ? what : "message";

    if (n > sizeof(buf)) return FALSE;
    memcpy(buf, words, n);

    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ev) return FALSE;
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = ev;

    if (WriteFile(pipe, buf, n, &put, &ov)) {
        ok = TRUE;                       /* completed inline */
    } else {
        DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) {
            if (pump(ev, 5000)) {
                if (GetOverlappedResult(pipe, &ov, &put, FALSE) && put == n)
                    ok = TRUE;
                else
                    logline("instance %ld: %s GetOverlappedResult err=%u put=%u",
                            id, label, GetLastError(), put);
            } else {
                cancel(pipe, &ov);
                logline("instance %ld: %s write timed out", id, label);
            }
        } else {
            logline("instance %ld: %s WriteFile err=%u", id, label, e);
        }
    }
    CloseHandle(ev);
    if (!ok) return FALSE;

    if (!what) return TRUE;               /* sampled out, but it did go out */

    char hex[128];
    DWORD show = n < 24 ? n : 24;
    for (DWORD k = 0; k < show && (k * 2 + 2) < sizeof(hex); k++)
        _snprintf(hex + k * 2, sizeof(hex) - k * 2, "%02x", buf[k]);
    logline("instance %ld: sent %s %u bytes: %s", id, what, n, hex);

    /*
     * Watchdog breadcrumb. When GW2 deadlocks, the last thing we sent before it
     * stopped talking is the single most useful clue we have, and having to
     * reconstruct it from log ordering afterwards is exactly the slow part.
     * Recorded separately so it survives log truncation.
     */
    _snprintf(g_last_sent, sizeof(g_last_sent), "inst%ld %s %uB %s", id, what, (unsigned)n, hex);
    InterlockedExchangeAdd(&g_sent_total, 1);
    return TRUE;
}

/*
 * Reply selection.
 *
 * There is no handshake function and no message-building code. The server's
 * entire vocabulary is g_replay[], generated by gen_replay.py straight from the
 * captured LCore traffic, and the only question is which entry to send next.
 *
 * That is the whole point of the rewrite. The previous version hand-transcribed
 * the opening messages into C arrays, and got a word in the wrong position
 * twice - once reversing the reply order, once mistaking a sequence number for
 * a payload field. Both bugs looked identical from the outside: GW2 hung at
 * character select. Generating the table removes the possibility of a silent
 * transcription error, so a failure now points at a real protocol problem.
 *
 * Position is the answer rather than any per-message logic, because the LCore
 * capture is itself positional: reply N is what LCore sent in answer to read N.
 * Once the table runs out we hold the last keepalive, since a real service
 * keeps answering long after the capture stopped.
 *
 * The counter is not frozen at zero. LCore numbers keepalives 0,1,2,3..., so a
 * server that keeps answering with the same value is answering out of order,
 * and gen_replay.py derives the resume point from the last captured keepalive.
 */
static volatile LONG g_tail = (LONG)REPLAY_TAIL_COUNTER;

/*
 * How long the client must be silent before we assume it is blocked in a read
 * and push a message unasked. Overridable so the pacing can be tuned without a
 * rebuild; the value is logged at startup so a test run is self-documenting.
 */
static DWORD idle_interval(void)
{
    char  *s = getenv("LGLCD_IDLE_MS");
    DWORD  v = (s) ? (DWORD)atoi(s) : 0;
    if (v < 20)  v = 20;
    if (v > 5000) v = 5000;
    return v;
}

static BOOL reply_next(HANDLE pipe, long id, DWORD *cursor)
{
    if (*cursor >= REPLAY_COUNT) {
        /*
         * A queued button press outranks the keepalive, but it is still just one
         * message in answer to one read - the discipline is unchanged, only the
         * payload is. Sending it here rather than at button-press time is what
         * keeps the client's queue exactly as deep as it asked for.
         */
        DWORD want;
        if (pq_pop(&want)) {
            DWORD pg[5] = { 20, 0x0702, 0, 0x80000002, want };
            return write_msg(pipe, pg, 5, id,
                             want ? "button(0x0702) press" : "button(0x0702) release");
        }

        /*
         * Past the end of the capture: stay alive with an incrementing keepalive.
         * These fire once per poll interval for the life of the session, so they
         * are sampled in the log rather than logged individually - otherwise a
         * five minute session buries everything else under thousands of lines.
         */
        DWORD  n = (DWORD)InterlockedIncrement(&g_tail);
        DWORD  ka[3] = { 12, 0x0900, n };
        static DWORD seen = 0;
        char   what[64];
        seen++;
        if (seen <= 3 || (seen % 64) == 0)
            _snprintf(what, sizeof(what), "keepalive(tail) #%lu counter=%lu",
                      seen, n);
        else
            return write_msg(pipe, ka, 3, id, NULL);
        return write_msg(pipe, ka, 3, id, what);
    }
    DWORD nwords = g_replay_len[*cursor] / 4;
    char what[64];
    _snprintf(what, sizeof(what), "replay[%lu] type=0x%04lx",
              *cursor, g_replay[*cursor][1]);
    if (!write_msg(pipe, g_replay[*cursor], nwords, id, what)) return FALSE;
    (*cursor)++;
    return TRUE;
}

static void handle_client(HANDLE pipe, long id)
{
    static BYTE  acc[MAX_MSG];
    DWORD        used = 0;
    unsigned long long frames = 0, handshakes = 0, other = 0;

    logline("instance %ld: client connected", id);

    /*
     * The one rule that matters, and the reason this server previously hung the
     * game three times: answer every read with EXACTLY ONE message.
     *
     * The pipe is in message mode, so each WriteFile queues a whole record and
     * each ReadFile dequeues exactly one. LCore honours that strictly - the
     * capture shows 49 reads answered by 48 replies, one for one. The earlier
     * server fired four handshake messages at the first read and then two per
     * cycle, building a backlog. GW2 dequeued messages it had not asked for,
     * the framing desynchronised, and its main thread blocked on the next
     * read, which Windows reports as AppHangB1. There was never a missing
     * message type; there was a queue discipline violation.
     */
    /*
     * Reads are invisible from this side: GW2's ReadFileEx goes straight to the
     * pipe and never touches us, so we cannot wait to be asked. The captured
     * trace shows why that matters -
     *
     *     R n=560        read #1 posted
     *     W n=1084       negotiation written
     *     RP got=16      our reply satisfies read #1
     *     R n=560        read #2 posted
     *     (silence)      GW2 blocks here forever
     *
     * GW2 expects a second message and will not send another request until it
     * gets one. LCore's 12-byte 0x0900 is the answer to that: a poll response,
     * which is exactly why its counter advances by one per message rather than
     * tracking any timer. So an idle client is a waiting client, and the same
     * reply path serves both cases.
     */
    DWORD cursor = 0;
    DWORD last_out = GetTickCount();

    for (;;) {
        OVERLAPPED ov;
        HANDLE     ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ev) break;
        ZeroMemory(&ov, sizeof(ov));
        ov.hEvent = ev;

        ctl_poll();

        DWORD got = 0;
        DWORD avail = 0;
        /*
         * Query before reading. A byte-mode pipe can be written by the client
         * only while it has read data we sent, and reading with a zero-length
         * request on an overlapped handle is legal but pointless. If the buffer
         * is full the read below would block indefinitely and we'd never notice
         * the client going away, so bail out early instead of hanging a thread
         * forever.
         */
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) ||
            avail == 0) {
            CloseHandle(ev);
            if (g_stop) break;

            /*
             * Answer a read that has gone unanswered for one poll interval.
             *
             * This is required. GW2 uses asynchronous reads, but during a zone
             * / loading screen it blocks waiting for the LCD reply, and with
             * this timer removed GW2 wedged on the loading screen (a read
             * posted at 01:06:49.792 was never completed). LCore answers such
             * reads too - in the clean [>] capture its release arrived ~100ms
             * after the read with no client message in between.
             *
             * The parse loop still drives the opening (two replies per client
             * message); this only fills gaps.
             */
            DWORD waited = GetTickCount() - last_out;
            if (waited >= idle_interval()) {
                if (!reply_next(pipe, id, &cursor)) {
                    logline("instance %ld: idle reply #%lu failed, dropping client",
                            id, cursor);
                    return;
                }
                last_out = GetTickCount();
            } else {
                wait_slice(2);
            }
            continue;
        }

        BOOL  ok = ReadFile(pipe, acc + used,
                            (avail < (MAX_MSG - used)) ? avail
                                                        : (MAX_MSG - used),
                            &got, &ov);
        if (!ok) {
            DWORD e = GetLastError();
            if (e == ERROR_IO_PENDING) {
                if (!pump(ev, 5000)) {
                    cancel(pipe, &ov);
                    CloseHandle(ev);
                    if (g_stop) break;
                    /* timed out: no traffic this round, loop and wait again */
                    continue;
                }
                if (!GetOverlappedResult(pipe, &ov, &got, FALSE)) {
                    DWORD e2 = GetLastError();
                    CloseHandle(ev);
                    if (e2 == ERROR_BROKEN_PIPE || e2 == ERROR_OPERATION_ABORTED) {
                        logline("instance %ld: pipe broken (err=%u) after %llu frames",
                                id, e2, frames);
                        return;
                    }
                    logline("instance %ld: read completion failed err=%u", id, e2);
                    return;
                }
            } else {
                CloseHandle(ev);
                logline("instance %ld: ReadFile failed err=%u after %llu frames",
                        id, e, frames);
                return;
            }
        }
        CloseHandle(ev);

        if (got == 0) {
            logline("instance %ld: client closed cleanly after %llu frames",
                    id, frames);
            return;
        }
        used += got;

        /* Parse as many complete length-prefixed messages as we have. */
        for (;;) {
            if (used < 4) break;
            DWORD len, seq, type;
            memcpy(&len,  acc,      4);        /* unaligned; avoid type punning */
            memcpy(&seq,  acc +  4, 4);
            memcpy(&type, acc +  8, 4);
            if (len < 12 || len > MAX_MSG) {
                logline("implausible length %u (have %u) - resetting", len, used);
                used = 0;
                break;
            }
            if (used < len) break;             /* wait for the rest */

            const char *what = "other";
            if (type == C_NEGOTIATE) { handshakes++; what = "NEGOTIATE"; }
            else if (type == C_FRAME) { frames++; what = "FRAME"; }
            else if (type == C_SHORT)  { other++;  what = "SHORT";  }
            else if (type == C_REGION || type == C_REGION2) { other++; what = "REGION"; }

            if (type == C_FRAME) {
                if (frames <= 3 || (frames % 200) == 0)
                    logline("seq=%-4lu %s %u bytes  frame#%llu", seq, what, len, frames);
            } else {
                logline("seq=%-4lu %s %u bytes  (reply #%lu)", seq, what, len, cursor);
            }

            /*
             * During the captured opening, each client message gets TWO replies.
             * LCore's trace is explicit:
             *
             *     seq=0 NEGOTIATE -> replay[0], replay[1]
             *     seq=1 SHORT     -> replay[2], replay[3]
             *     seq=2 REGION    -> replay[4], replay[5]
             *
             * Sending only one per message handed GW2 the wrong record - a
             * 0x0703 where it expected the 0x0900 config - and it re-negotiated
             * (extra 0x0804/0x0803 messages) and left its device state
             * uninitialised, after which it ignored every button press. Past the
             * opening it is one reply per message as before.
             */
            DWORD want = (cursor < REPLAY_COUNT) ? 2u : 1u;
            for (DWORD r = 0; r < want; r++) {
                if (!reply_next(pipe, id, &cursor)) {
                    logline("instance %ld: reply #%lu failed, dropping client",
                            id, cursor);
                    return;
                }
                if (cursor >= REPLAY_COUNT) break;   /* no keepalive padding */
            }

            memmove(acc, acc + len, used - len);
            used -= len;
        }

        if (used == MAX_MSG) {
            logline("instance %ld: accumulator full, resetting", id);
            used = 0;
        }
    }
    logline("instance %ld: read loop ended (stopping)", id);
}

/*
 * Park this pipe instance until a real client arrives, or we are shutting down.
 *
 * The subtlety that cost us a session: cancelling a pending ConnectNamedPipe
 * and then reusing the same instance makes the *next* ConnectNamedPipe report
 * success immediately even though no client exists. The following WriteFile
 * then fails with ERROR_NO_DATA (232) and ReadFile with ERROR_BROKEN_PIPE
 * (109). Those two errors are the signature of a phantom "connection", and
 * they were silently eating all four of GW2's real connects.
 *
 * The fix is to make the timeout loop unconditional and to never treat a
 * completion as a connection on its own - after the event fires, ask the pipe
 * whether a client is actually there, and only then keep the instance.
 */
static BOOL wait_for_client(HANDLE pipe, LONG id)
{
    for (;;) {
        if (g_stop) return FALSE;

        OVERLAPPED ov;
        HANDLE     ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ev) return FALSE;
        ZeroMemory(&ov, sizeof(ov));
        ov.hEvent = ev;

        BOOL connected = FALSE;
        if (ConnectNamedPipe(pipe, &ov)) {
            connected = TRUE;               /* client was already waiting */
        } else {
            DWORD e = GetLastError();
            if (e == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            } else if (e == ERROR_IO_PENDING) {
                if (pump(ev, CONNECT_TIMEOUT_MS))
                    connected = TRUE;       /* event fired: a client connected */
                else
                    cancel(pipe, &ov);      /* timed out: abandon, keep instance */
            } else {
                logline("instance %ld: ConnectNamedPipe err=%u", id, e);
            }
        }
        CloseHandle(ev);

        if (connected) {
            /*
             * Verify with GetNamedPipeClientProcessId. A cancelled connect can
             * leave the instance reporting connected with no client behind it;
             * this call distinguishes that from a real one.
             */
            ULONG pid = 0;
            if (g_GetNamedPipeClientPid(pipe, &pid) && pid != 0) {
                logline("instance %ld: client connected (pid=%lu)", id, pid);
                return TRUE;
            }
            logline("instance %ld: phantom connect discarded (pid=%lu)", id, pid);
        }
        /* no client: brief pause, then wait again on the same instance */
        wait_slice(200);
    }
}

static DWORD WINAPI instance_thread(LPVOID param)
{
    LONG id = (LONG)(intptr_t)param;
    logline("instance %ld: armed", id);

    while (!g_stop) {
        /*
         * Message mode, because that is what the client chose.
         *
         * GetNamedPipeInfo reported mode=1 (PIPE_READMODE_MESSAGE) even though
         * this pipe was created PIPE_TYPE_BYTE, which means GW2 called
         * SetNamedPipeHandleState to switch to message reads. Matching that
         * here matters: in byte mode a read can return a partial message,
         * whereas in message mode each write is delivered as one whole record,
         * which is what the length-prefixed framing in this protocol expects.
         *
         * FILE_FLAG_OVERLAPPED is required for GW2's ReadFileEx to work at all,
         * and it is also why every I/O call here must pass a real OVERLAPPED.
         */
        HANDLE pipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            logline("instance %ld: CreateNamedPipe failed err=%u", id, e);
            wait_slice(2000);
            continue;
        }

        if (!wait_for_client(pipe, id)) {
            CloseHandle(pipe);
            break;
        }

        /*
         * Record the mode the client actually selected. It matters because a
         * client that switches to message reads expects whole records; seeing
         * it reported back confirms our CreateNamedPipe flags agree with the
         * client's intent rather than being overridden at runtime.
         */
        {
            ULONG mode = 0, inbuf = 0, outbuf = 0;
            if (g_GetNamedPipeInfo &&
                g_GetNamedPipeInfo(pipe, &mode, &inbuf, &outbuf, NULL, NULL))
                logline("instance %ld: pipe mode=%lu (1=message) inbuf=%lu outbuf=%lu",
                        id, (unsigned long)mode, (unsigned long)inbuf,
                        (unsigned long)outbuf);
        }

        handle_client(pipe, id);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        if (!g_stop)
            logline("instance %ld: dropped (total sent=%ld, last=%s)",
                    id, (long)g_sent_total, g_last_sent);
    }
    logline("instance %ld: exiting", id);
    return 0;
}

static DWORD WINAPI heartbeat_thread(LPVOID param)
{
    (void)param;
    for (;;) {
        wait_slice(5000);
        if (g_stop) return 0;
        logline("alive: threads=%lu", (unsigned long)g_alive);
    }
}

int main(void)
{
    InitializeCriticalSection(&g_lock);
    SetConsoleCtrlHandler(console_ctrl, TRUE);
    g_CancelIoEx = (pCancelIoEx)(void*)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "CancelIoEx");
    g_GetNamedPipeClientPid = (pGetNamedPipeClientProcessId)(void*)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                       "GetNamedPipeClientProcessId");
    if (!g_GetNamedPipeClientPid) {
        logline("FATAL: GetNamedPipeClientProcessId unavailable");
        return 2;
    }
    g_GetNamedPipeInfo = (pGetNamedPipeInfo)(void*)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "GetNamedPipeInfo");
    logline("=== lgpipe_server up: %ls instances=%d idle_poll=%ums ===",
            PIPE_NAME, INSTANCES, idle_interval());
    ctl_open();

    HANDLE hb = CreateThread(NULL, 0, heartbeat_thread, NULL, 0, NULL);

    HANDLE th[INSTANCES];
    for (long i = 0; i < INSTANCES; i++) {
        th[i] = CreateThread(NULL, 0, instance_thread, (LPVOID)(intptr_t)i, 0, NULL);
        if (!th[i]) logline("CreateThread failed for instance %ld", i);
        else InterlockedIncrement(&g_alive);
    }

    for (;;) {
        wait_slice(500);
        if (g_stop) break;
    }

    logline("=== lgpipe_server exiting ===");
    /* The instance threads are parked in overlapped ConnectNamedPipe with a
     * 30s poll, so give them a moment to notice the stop flag, then leave. */
    for (int i = 0; i < 100 && g_alive > 0; i++) wait_slice(100);
    if (hb) CloseHandle(hb);
    DeleteCriticalSection(&g_lock);
    return 0;
}
