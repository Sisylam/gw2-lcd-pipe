/*
 * LgLcdApiProxy.dll - passive observer for Guild Wars 2's Logitech LCD path.
 *
 * WHY THIS DESIGN
 * ---------------
 * An earlier version of this file replaced the entries of the function table
 * returned by GetInterface() with hand-written machine-code trampolines. That
 * crashed GW2 twice:
 *   1. a rel32 / RIP-relative displacement cannot span the >2GB gap between the
 *      VirtualAlloc'd stub region and this image, so the jump landed on a zero
 *      page, and
 *   2. the dispatch index was passed in eax, which the compiler is free to
 *      clobber in the prologue, so the index table was indexed out of bounds.
 *
 * Both are avoidable. Static analysis of LgLcdApi.dll shows it imports only
 * KERNEL32 and ADVAPI32 and contains no reference to LogitechLcd.dll or any
 * LogiLcd* symbol. It is a named-pipe client: its Transport.cpp builds a pipe
 * name with swprintf(buf, n, "%s%s-%08x", ...) and ships the bitmap to the
 * Logitech service with WriteFile/WriteFileEx over that pipe.
 *
 * So this DLL now does the following and nothing else:
 *   - returns the REAL interface table from GetInterface(), unmodified, so the
 *     game sees the genuine API and cannot crash on us;
 *   - patches the *real* LgLcdApi.dll's import address table so the WriteFile /
 *     WriteFileEx / ReadFileEx / CreateFileW it calls land in us first, observes
 *     the buffers, and forwards every call untouched.
 *
 * There is no generated code, no executable allocation and no ABI guesswork
 * here, so the game-visible behaviour is identical to stock apart from logging.
 * DllRegisterServer/DllUnregisterServer are deliberately inert: forwarding them
 * would make the real DLL re-register its own path over our proxy entry and
 * silently disable this.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

/*
 * The genuine Logitech client DLL is copyrighted, so it is NOT committed to
 * the repository. It lives in third_party/logitech/ (also gitignored) and is
 * placed there at setup time. Resolve it relative to this proxy DLL so the
 * project does not depend on a machine-specific Program Files path:
 *
 *     lcdproxy\LgLcdApiProxy.dll  ->  ..\third_party\logitech\LgLcdApi.dll
 *
 * Fall back to the stock install location if the local copy is missing.
 */
#define REAL_API_PATH L"C:\\Program Files\\Logitech Gaming Software\\SDK\\LCD\\x64\\LgLcdApi.dll"
#define REAL_API_REL  L"..\\third_party\\logitech\\LgLcdApi.dll"

static HINSTANCE g_inst;

static void resolve_real_path(WCHAR* out, int cap)
{
    WCHAR self[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_inst, self, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        int cut = (int)n;
        while (cut > 0 && self[cut - 1] != L'\\') cut--;   /* drop filename */
        int i = 0;
        for (; i < cut && i < cap - 1; i++) out[i] = self[i];
        const WCHAR* rel = REAL_API_REL;
        for (int j = 0; rel[j] && i < cap - 1; j++) out[i++] = rel[j];
        out[i] = 0;
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return;
    }
    int i = 0;
    for (; REAL_API_PATH[i] && i < cap - 1; i++) out[i] = REAL_API_PATH[i];
    out[i] = 0;
}

#define LOG_PATH     L"C:\\Users\\Sisyphos\\AppData\\Local\\Temp\\opencode\\gw2proxy.log"
#define DUMP_DIR     L"C:\\Users\\Sisyphos\\AppData\\Local\\Temp\\opencode\\payloads"

#define LOG_CAP          20000   /* log lines before we go quiet          */
#define MAX_STREAMS      8       /* distinct write handles we follow       */
#define STREAM_CAP       (8u*1024u*1024u)
#define MAX_KNOWN_SIZES  64      /* remember payload sizes we have dumped */
#define MIN_DUMP         256     /* ignore tiny bookkeeping writes         */

typedef void* (*getinterface_fn)(int);

static HMODULE         g_real;
static getinterface_fn g_real_gi;
static void*           g_tables[6];
static CRITICAL_SECTION g_lock;
static LONG            g_lines;

typedef BOOL (WINAPI *pWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI *pWriteFileEx)(HANDLE, LPCVOID, DWORD, LPOVERLAPPED,
                                    LPOVERLAPPED_COMPLETION_ROUTINE);
typedef BOOL (WINAPI *pReadFileEx)(HANDLE, LPVOID, DWORD, LPOVERLAPPED,
                                   LPOVERLAPPED_COMPLETION_ROUTINE);
typedef HANDLE (WINAPI *pCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                      DWORD, DWORD, HANDLE);
/*
 * Added while hunting the button channel. The pipe is the only thing the
 * original hooks could see; if the soft-button event reaches GW2 through a
 * shared memory section, a named event, or a second pipe, these four would
 * expose it. They are observation only - each one forwards to the real
 * function untouched.
 */
typedef HANDLE (WINAPI *pCreateFileMappingW)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD,
                                             DWORD, DWORD, LPCWSTR);
typedef HANDLE (WINAPI *pOpenFileMappingW)(DWORD, BOOL, LPCWSTR);
typedef LPVOID (WINAPI *pMapViewOfFile)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
typedef HANDLE (WINAPI *pCreateEventW)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
typedef HANDLE (WINAPI *pOpenEventW)(DWORD, BOOL, LPCWSTR);

static pWriteFile    g_RealWriteFile;
static pWriteFileEx  g_RealWriteFileEx;
static pReadFileEx   g_RealReadFileEx;
static pCreateFileW  g_RealCreateFileW;
static pCreateFileMappingW g_RealCreateFileMappingW;
static pOpenFileMappingW   g_RealOpenFileMappingW;
static pMapViewOfFile      g_RealMapViewOfFile;
static pCreateEventW       g_RealCreateEventW;
static pOpenEventW         g_RealOpenEventW;

struct stream { HANDLE h; WCHAR path[MAX_PATH]; ULONGLONG bytes; DWORD writes; };
static struct stream  g_streams[MAX_STREAMS];
static DWORD         g_known_sizes[MAX_KNOWN_SIZES];
static DWORD         g_known_count;

static void logline(const char* fmt, ...)
{
    char buf[1200];
    char out[1500];
    int    n;
    va_list ap;
    out[0] = 0;
    va_start(ap, fmt);
    n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    buf[n] = 0;

    /*
     * Prefix with a wall-clock timestamp and a millisecond-since-load counter.
     *
     * Without this the log is a bare event sequence, and every timing question
     * - how often GW2 polls, whether a keepalive is a timer or a response to a
     * client read, how long a hang lasts - has to be reconstructed from the
     * payload dump filenames, which are capped and so are not a reliable
     * clock. An idle measurement run earlier had to be discarded for exactly
     * that reason.
     */
    {
        static long long base_ms = 0;
        static BOOL     have_base = FALSE;
        FILETIME   ft;
        LARGE_INTEGER li;

        GetSystemTimeAsFileTime(&ft);
        li.LowPart  = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        long long now_ms = (long long)(li.QuadPart / 10000);

        if (!have_base) {
            base_ms  = now_ms;
            have_base = TRUE;
        }

        SYSTEMTIME now;
        GetLocalTime(&now);

        char pre[64];
        int k = _snprintf(pre, sizeof(pre), "%02d:%02d:%02d.%03d +%lldms ",
                          now.wHour, now.wMinute, now.wSecond,
                          now.wMilliseconds, now_ms - base_ms);
        if (k > 0) {
            /*
             * Build the final line in `out` and track its real length. An
             * earlier version prepended the prefix in place and then wrote the
             * original message length, which cut the timestamp back into the
             * middle of the line and shredded the log - exactly the log we
             * needed for timing analysis.
             */
            if (k + n < (int)sizeof(out)) {
                memcpy(out, pre, (size_t)k);
                memcpy(out + k, buf, (size_t)n);
                memcpy(out + k + n, "\r\n", 2);
                n = k + n + 2;
                buf[0] = 0;                 /* buf no longer used */
            }
        }
        if (!out[0]) {
            memcpy(out, buf, (size_t)n);
            memcpy(out + n, "\r\n", 2);
            n += 2;
        }
    }

    if (InterlockedIncrement(&g_lines) > LOG_CAP) return;
    EnterCriticalSection(&g_lock);
    HANDLE f = CreateFileW(LOG_PATH, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(f, out, (DWORD)n, &w, NULL);
        CloseHandle(f);
    }
    LeaveCriticalSection(&g_lock);
}

static void hexhead(const unsigned char* p, DWORD n, char* out, int cap)
{
    int off = 0;
    out[0] = 0;
    for (DWORD i = 0; i < n && off < cap - 3; i++)
        off += _snprintf(out + off, (size_t)(cap - off), "%02x", p[i]);
    out[off] = 0;
}

static void hexdump_wide(const WCHAR* w, char* out, int cap)
{
    int off = 0;
    out[0] = 0;
    if (!w) { _snprintf(out, (size_t)cap, "(null)"); return; }
    for (int i = 0; w[i] && off < cap - 4; i++) {
        WCHAR c = w[i];
        char ch = (c < 32 || c > 126) ? '.' : (char)c;
        out[off++] = ch;
    }
    out[off] = 0;
}

/* MinGW 4.8's _snprintf has no %ls, so build wide paths by hand. Filenames
   are ASCII, which keeps this trivial. */
static void make_wpath(WCHAR* dst, int cap, const WCHAR* dir, const char* name)
{
    int i = 0;
    for (; dir[i] && i < cap - 2; i++) dst[i] = dir[i];
    if (i > 0 && dst[i - 1] != L'\\' && dst[i - 1] != L'/') dst[i++] = L'\\';
    for (const char* p = name; *p && i < cap - 1; p++, i++)
        dst[i] = (WCHAR)(unsigned char)*p;
    dst[i] = 0;
}

/* --- live publishing to the existing viewer -------------------------------
 * Contract consumed by the viewer (viewer/src/shim.rs, formerly lcdshim.py):
 *   mapping "Local\GW2LCDShim", 48-byte header of 12 uint32, then pixels.
 *   header = magic, version, width, height, bpp, active, sequence, connected,
 *            init_count, text_count, update_count, reserved
 *   active == 1 -> width*height*3 bytes of RGB follow the header.
 */
#define SHM_NAME   L"Local\\GW2LCDShim"
#define SHM_MAGIC  0x32313047u
#define SHM_VER    1u
#define SHM_HDR    48u
#define IMG_W      320u
#define IMG_H      240u
#define SHM_MAX    (IMG_W * IMG_H * 3u)          /* 230400, matches the reader */
#define PIPE_HDR   24u                            /* pipe message header        */
#define FRAME_BYTES (PIPE_HDR + IMG_W * IMG_H * 4u) /* 307224, what GW2 sends   */

static HANDLE   g_map;
static uint8_t* g_view;
static volatile LONG g_published;

static void ensure_shm(void)
{
    if (g_view) return;
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                               0, SHM_HDR + SHM_MAX, SHM_NAME);
    if (!g_map) { logline("shm CreateFileMapping failed %lu\n", (unsigned)GetLastError()); return; }
    g_view = (uint8_t*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, SHM_HDR + SHM_MAX);
    if (!g_view) {
        logline("shm MapViewOfFile failed %lu\n", (unsigned)GetLastError());
        CloseHandle(g_map);
        g_map = NULL;
        return;
    }
    uint32_t* h = (uint32_t*)g_view;
    h[0] = SHM_MAGIC; h[1] = SHM_VER; h[2] = IMG_W; h[3] = IMG_H;
    h[4] = 24; h[5] = 1; h[6] = 0; h[7] = 1;
    h[8] = 0; h[9] = 0; h[10] = 0; h[11] = 0;
    logline("shm ready %ls %ux%u rgb\n", SHM_NAME, IMG_W, IMG_H);
}

/* GW2 sends the frame as BGRA; the viewer wants RGB. */
static void publish_frame(const uint8_t* px)
{
    if (!g_view) return;
    uint8_t* dst = g_view + SHM_HDR;
    uint32_t n = IMG_W * IMG_H;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* s = px + i * 4;
        uint8_t* d = dst + i * 3;
        d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
    }
    uint32_t* h = (uint32_t*)g_view;
    h[10]++;                                   /* update_count */
    InterlockedIncrement((volatile LONG*)&h[6]);  /* sequence last: reader sees whole frames */
    if (InterlockedIncrement(&g_published) <= 5 || (g_published % 120) == 0)
        logline("published frame #%d\n", (int)g_published);
}

static void ensure_dump_dir(void)
{
    static LONG done = 0;
    if (InterlockedCompareExchange(&done, 1, 0) == 0)
        CreateDirectoryW(DUMP_DIR, NULL);
}

/* Record one write. Caller must NOT hold g_lock. */
static void note_write(HANDLE h, const void* buf, DWORD n)
{
    if (!buf || n == 0) return;

    /* the live path: publish the bitmap for the viewer */
    if (n == FRAME_BYTES) {
        ensure_shm();
        publish_frame((const uint8_t*)buf + PIPE_HDR);
    }

    char head[80];
    hexhead((const unsigned char*)buf, n < 24 ? n : 24, head, (int)sizeof(head));

    /* find or create the stream for this handle */
    int si = -1, free_slot = -1;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (g_streams[i].h == h) { si = i; break; }
        if (!g_streams[i].h && free_slot < 0) free_slot = i;
    }
    if (si < 0 && free_slot >= 0) {
        si = free_slot;
        g_streams[si].h = h;
        g_streams[si].bytes = 0;
        g_streams[si].writes = 0;
        char nm[64];
        _snprintf(nm, sizeof(nm), "stream_%d_%llx.bin", si,
                  (unsigned long long)(intptr_t)h);
        make_wpath(g_streams[si].path, MAX_PATH, DUMP_DIR, nm);
    }
    if (si >= 0 && g_streams[si].bytes < STREAM_CAP) {
        struct stream* s = &g_streams[si];
        HANDLE f = CreateFileW(s->path, FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(f, buf, n, &w, NULL);
            CloseHandle(f);
            s->bytes += w;
            s->writes++;
        }
    }
    ULONGLONG total = (si >= 0) ? g_streams[si].bytes : 0;
    DWORD nwrites = (si >= 0) ? g_streams[si].writes : 0;
    LeaveCriticalSection(&g_lock);

    /* dump the first payload of every distinct size: one sample per shape is
       enough to identify the bitmap offline, and it bounds the disk cost */
    if (n >= MIN_DUMP) {
        int known = 0, do_dump = 0;
        EnterCriticalSection(&g_lock);
        for (DWORD i = 0; i < g_known_count; i++)
            if (g_known_sizes[i] == n) { known = 1; break; }
        if (!known && g_known_count < MAX_KNOWN_SIZES) {
            g_known_sizes[g_known_count++] = n;
            do_dump = 1;
        }
        LeaveCriticalSection(&g_lock);
        if (do_dump) {
            ensure_dump_dir();
            char nm[64];
            _snprintf(nm, sizeof(nm), "payload_%u.bin", (unsigned)n);
            WCHAR p[MAX_PATH];
            make_wpath(p, MAX_PATH, DUMP_DIR, nm);
            HANDLE f = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD w = 0;
                WriteFile(f, buf, n, &w, NULL);
                CloseHandle(f);
            }
        }
    }

    logline("W h=%llx n=%u stream=%d total=%llu writes=%u head=%s\n",
            (unsigned long long)(intptr_t)h, n, si,
            (unsigned long long)total, nwrites, head);
}

static BOOL WINAPI hook_WriteFile(HANDLE h, LPCVOID b, DWORD n, LPDWORD w, LPOVERLAPPED o)
{
    note_write(h, b, n);
    return g_RealWriteFile(h, b, n, w, o);
}

static BOOL WINAPI hook_WriteFileEx(HANDLE h, LPCVOID b, DWORD n, LPOVERLAPPED o,
                                    LPOVERLAPPED_COMPLETION_ROUTINE c)
{
    note_write(h, b, n);
    return g_RealWriteFileEx(h, b, n, o, c);
}

/*
 * MinGW's minwinbase.h declares the completion routine as
 *   void(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED)
 * but the real Win32 ABI (and what LgLcdApi, an MSVC build, is compiled
 * against) is
 *   void(DWORD dwErrorCode, LPOVERLAPPED lpOverlapped, LPOVERLAPPED lpBytesTransferred)
 * On x86-64 both pass three register args, so trusting the header silently
 * hands the caller a pointer where it expects a count. Use our own typedef.
 */
/*
 * Capturing the server's replies WITHOUT touching the caller's state.
 *
 * An earlier attempt at this crashed GW2 twice. The crash report pinned the
 * cause exactly: the faulting instruction was
 *
 *     cmp dword [rdx-18h], <magic>     ; rdx = 10h
 *
 * where 18h is offsetof(rwrap_t, real_ov). So the second argument arrived as
 * 0x10 - a byte count of 16, not a pointer. The runtime invokes completion
 * routines in the order this MinGW header documents,
 *
 *     void(DWORD err, DWORD transferred, LPOVERLAPPED ov)
 *
 * and the "correction" to the modern documented order was what dereferenced a
 * count as a pointer. Note 16 is also the size LCore uses to answer GW2's
 * 560-byte read requests, so that number is a real observation, twice.
 *
 * The lesson is not the argument order, it is that we had no business
 * rewriting the caller's OVERLAPPED at all. This version therefore:
 *
 *   - passes the caller's buffer, the caller's OVERLAPPED and the caller's
 *     arguments through completely unmodified;
 *   - identifies its own context by matching the OVERLAPPED pointer against a
 *     small static table, so it never has to compute a pointer from an
 *     argument whose meaning it is guessing at;
 *   - reads the transferred count from the OVERLAPPED's own InternalHigh,
 *     which the kernel fills in regardless of completion argument order;
 *   - forwards the three arguments verbatim to the caller's routine, so
 *     whatever ABI is in force stays in force.
 *
 * If the lookup fails we still forward, so the worst case is a missed log
 * line rather than a lost notification.
 */
#define RCAP_SLOTS 64

typedef struct {
    LPOVERLAPPED                   ov;   /* the caller's OVERLAPPED */
    LPOVERLAPPED_COMPLETION_ROUTINE cb;
    LPVOID                         buf;
    DWORD                          n;
    LONG                           inuse;
} rcap_t;

static rcap_t           g_rcap[RCAP_SLOTS];
static CRITICAL_SECTION g_rcap_lock;

static void WINAPI read_cb(DWORD a1, DWORD a2, LPOVERLAPPED a3)
{
    LPOVERLAPPED_COMPLETION_ROUTINE cb = NULL;
    LPVOID   buf = NULL;
    LPOVERLAPPED ov = NULL;
    DWORD    want = 0, got = 0;

    EnterCriticalSection(&g_rcap_lock);
    for (int i = 0; i < RCAP_SLOTS; i++) {
        rcap_t *e = &g_rcap[i];
        if (!e->inuse) continue;
        /* The caller's OVERLAPPED is whichever argument is the pointer. */
        if ((ULONG_PTR)a2 == (ULONG_PTR)e->ov || (ULONG_PTR)a3 == (ULONG_PTR)e->ov) {
            e->inuse = 0;
            cb   = e->cb;
            buf  = e->buf;
            want = e->n;
            ov   = e->ov;
            break;
        }
    }
    LeaveCriticalSection(&g_rcap_lock);

    /* InternalHigh is set by the kernel and is independent of argument order. */
    if (ov && a1 == ERROR_SUCCESS)
        got = ov->InternalHigh;

    if (buf && got && got <= want && InterlockedIncrement(&g_lines) <= LOG_CAP) {
        char head[120];
        hexhead((const unsigned char*)buf, (got < 48) ? got : 48,
                head, (int)sizeof(head));
        logline("RP n=%u got=%u head=%s\n", want, got, head);
        static LONG rcnt = 0;
        LONG r = InterlockedIncrement(&rcnt);
        /*
         * Dump every reply, not a sample. The earlier limit of 8 left us
         * guessing: GW2 issues ~13 reads at startup and we had only the first
         * 8 replies, so the missing ones were exactly the ones we needed. The
         * dumps are tiny (12-32 bytes each) and the numbering keeps them in
         * wire order, which is what makes the sequence replayable.
         */
        if (r <= 64) {
            char nm[96];
            WCHAR wpath[MAX_PATH];
            _snprintf(nm, sizeof(nm), "read_%03ld_%u.bin", r, (unsigned)got);
            make_wpath(wpath, MAX_PATH, DUMP_DIR, nm);
            HANDLE f = CreateFileW(wpath, GENERIC_WRITE, FILE_SHARE_READ,
                                   NULL, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD ww = 0;
                WriteFile(f, buf, got, &ww, NULL);
                CloseHandle(f);
            }
        }
    }

    if (cb) cb(a1, a2, a3);       /* verbatim: preserves whatever ABI applies */
}

static BOOL WINAPI hook_ReadFileEx(HANDLE h, LPVOID b, DWORD n, LPOVERLAPPED o,
                                   LPOVERLAPPED_COMPLETION_ROUTINE c)
{
    logline("R h=%llx n=%u\n", (unsigned long long)(intptr_t)h, n);

    if (c && o && b && n && n <= 4096) {
        int slot = -1;
        EnterCriticalSection(&g_rcap_lock);
        for (int i = 0; i < RCAP_SLOTS; i++) {
            if (!g_rcap[i].inuse) { slot = i; break; }
        }
        if (slot >= 0) {
            g_rcap[slot].ov    = o;
            g_rcap[slot].cb    = c;
            g_rcap[slot].buf   = b;
            g_rcap[slot].n     = n;
            g_rcap[slot].inuse = 1;
        }
        LeaveCriticalSection(&g_rcap_lock);
        if (slot >= 0) {
            if (g_RealReadFileEx(h, b, n, o,
                                 (LPOVERLAPPED_COMPLETION_ROUTINE)(void*)read_cb))
                return TRUE;
            EnterCriticalSection(&g_rcap_lock);
            g_rcap[slot].inuse = 0;
            LeaveCriticalSection(&g_rcap_lock);
        }
    }
    return g_RealReadFileEx(h, b, n, o, c);
}

static HANDLE WINAPI hook_CreateFileW(LPCWSTR path, DWORD acc, DWORD share,
                                      LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                      DWORD flags, HANDLE tmpl)
{
    HANDLE h = g_RealCreateFileW(path, acc, share, sa, disp, flags, tmpl);
    char p[300];
    hexdump_wide(path, p, (int)sizeof(p));
    logline("C h=%llx acc=%08x share=%08x disp=%u flags=%08x path=%s\n",
            (unsigned long long)(intptr_t)h, acc, share, disp, flags, p);
    return h;
}

/* Named-section / event observation, for locating a non-pipe button channel. */
static HANDLE WINAPI hook_CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES sa,
                                             DWORD prot, DWORD hi, DWORD lo,
                                             LPCWSTR name)
{
    HANDLE h = g_RealCreateFileMappingW(file, sa, prot, hi, lo, name);
    char p[300];
    hexdump_wide(name, p, (int)sizeof(p));
    logline("M h=%llx prot=%08x size=%u name=%s\n",
            (unsigned long long)(intptr_t)h, prot, lo, p);
    return h;
}

static HANDLE WINAPI hook_OpenFileMappingW(DWORD acc, BOOL inherit, LPCWSTR name)
{
    HANDLE h = g_RealOpenFileMappingW(acc, inherit, name);
    char p[300];
    hexdump_wide(name, p, (int)sizeof(p));
    logline("OM h=%llx acc=%08x name=%s\n",
            (unsigned long long)(intptr_t)h, acc, p);
    return h;
}

static LPVOID WINAPI hook_MapViewOfFile(HANDLE map, DWORD acc, DWORD hi,
                                        DWORD lo, SIZE_T n)
{
    LPVOID v = g_RealMapViewOfFile(map, acc, hi, lo, n);
    logline("V h=%llx acc=%08x off=%u n=%u -> %p\n",
            (unsigned long long)(intptr_t)map, acc, lo, (unsigned)n, v);
    return v;
}

static HANDLE WINAPI hook_CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual,
                                       BOOL initial, LPCWSTR name)
{
    HANDLE h = g_RealCreateEventW(sa, manual, initial, name);
    char p[300];
    hexdump_wide(name, p, (int)sizeof(p));
    logline("E h=%llx manual=%u init=%u name=%s\n",
            (unsigned long long)(intptr_t)h, manual, initial, p);
    return h;
}

static HANDLE WINAPI hook_OpenEventW(DWORD acc, BOOL inherit, LPCWSTR name)
{
    HANDLE h = g_RealOpenEventW(acc, inherit, name);
    char p[300];
    hexdump_wide(name, p, (int)sizeof(p));
    logline("OE h=%llx acc=%08x name=%s\n",
            (unsigned long long)(intptr_t)h, acc, p);
    return h;
}

/* Replace one imported function pointer inside a module's IAT. */
static int patch_iat(HMODULE mod, const char* dllname, const char* fname,
                     void* hook, void** saved)
{
    BYTE* base = (BYTE*)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return 0;

    IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + rva);
    for (; d->Name; d++) {
        if (_stricmp((const char*)(base + d->Name), dllname) != 0) continue;
        if (!d->OriginalFirstThunk) return 0;      /* bound imports: no names */
        IMAGE_THUNK_DATA64* oft = (IMAGE_THUNK_DATA64*)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA64* ft  = (IMAGE_THUNK_DATA64*)(base + d->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL64(oft->u1.Ordinal)) continue;   /* ordinal */
            const char* nm = (const char*)(base + oft->u1.AddressOfData) + sizeof(WORD);
            if (strcmp(nm, fname) != 0) continue;
            *saved = (void*)(intptr_t)ft->u1.Function;
            DWORD old = 0;
            if (!VirtualProtect(ft, sizeof(ULONGLONG), PAGE_READWRITE, &old)) return 0;
            ft->u1.Function = (ULONGLONG)(intptr_t)hook;
            VirtualProtect(ft, sizeof(ULONGLONG), old, &old);
            return 1;
        }
        return 0;
    }
    return 0;
}

static void install_hooks(void)
{
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;

    g_RealWriteFile   = (pWriteFile)  GetProcAddress(k32, "WriteFile");
    g_RealWriteFileEx = (pWriteFileEx)GetProcAddress(k32, "WriteFileEx");
    g_RealReadFileEx  = (pReadFileEx) GetProcAddress(k32, "ReadFileEx");
    g_RealCreateFileW = (pCreateFileW)GetProcAddress(k32, "CreateFileW");
    g_RealCreateFileMappingW = (pCreateFileMappingW)GetProcAddress(k32, "CreateFileMappingW");
    g_RealOpenFileMappingW   = (pOpenFileMappingW)  GetProcAddress(k32, "OpenFileMappingW");
    g_RealMapViewOfFile      = (pMapViewOfFile)     GetProcAddress(k32, "MapViewOfFile");
    g_RealCreateEventW       = (pCreateEventW)      GetProcAddress(k32, "CreateEventW");
    g_RealOpenEventW         = (pOpenEventW)        GetProcAddress(k32, "OpenEventW");
    if (!g_RealWriteFile) { logline("FATAL no real WriteFile\n"); return; }

    struct { const char* name; void* hook; void** saved; } want[] = {
        { "WriteFile",   (void*)hook_WriteFile,   (void**)&g_RealWriteFile   },
        { "WriteFileEx", (void*)hook_WriteFileEx, (void**)&g_RealWriteFileEx },
        { "ReadFileEx",  (void*)hook_ReadFileEx,  (void**)&g_RealReadFileEx  },
        { "CreateFileW", (void*)hook_CreateFileW,  (void**)&g_RealCreateFileW  },
        { "CreateFileMappingW", (void*)hook_CreateFileMappingW, (void**)&g_RealCreateFileMappingW },
        { "OpenFileMappingW",   (void*)hook_OpenFileMappingW,   (void**)&g_RealOpenFileMappingW   },
        { "MapViewOfFile",      (void*)hook_MapViewOfFile,      (void**)&g_RealMapViewOfFile      },
        { "CreateEventW",       (void*)hook_CreateEventW,       (void**)&g_RealCreateEventW       },
        { "OpenEventW",         (void*)hook_OpenEventW,         (void**)&g_RealOpenEventW         },
    };

    for (DWORD i = 0; i < sizeof(want)/sizeof(want[0]); i++) {
        void* orig = NULL;
        if (patch_iat(g_real, "KERNEL32.dll", want[i].name, want[i].hook, &orig)) {
            /* keep the true original for forwarding, not our fallback */
            if (orig) *want[i].saved = orig;
            logline("hooked %s real=%p\n", want[i].name, orig);
        } else {
            logline("skip  %s (not imported)\n", want[i].name);
        }
    }
}

#ifdef WRAP_TABLE
/*
 * --- GetInterface() table wrapping: which client-API calls does GW2 make? ---
 *
 * The header that defines the struct returned by GetInterface() (the SDK's
 * LgLcdApi.h) is not publicly mirrored, so we recover the interface shape
 * empirically instead of guessing offsets. We copy the real table, replace
 * every entry that is a pointer into the real DLL's own image with a logging
 * wrapper, and hand the copy back. Non-code entries - a leading version DWORD,
 * padding, anything that is not a pointer into LgLcdApi.dll - are preserved
 * verbatim, so the game still sees a well-formed table no matter the layout.
 *
 * Every lgLcd* function takes at most three integer/pointer arguments and
 * returns a DWORD (see the public SDK header lglcd.h), so one generic
 * four-argument wrapper per slot is ABI-safe on x64: the fifth and later
 * argument slots are never used by this API, and there are no float args.
 */
#define NWRAP 32

static void* g_real_table[NWRAP];
static void* g_wrapped[NWRAP];
static int   g_wrapped_ready;

typedef uint64_t (*gen_fn)(uint64_t, uint64_t, uint64_t, uint64_t);

static uint64_t wrap_call(int idx, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    logline("CALL[%02d] a=%llx b=%llx c=%llx d=%llx\n", idx,
            (unsigned long long)a, (unsigned long long)b,
            (unsigned long long)c, (unsigned long long)d);
    return ((gen_fn)g_real_table[idx])(a, b, c, d);
}

#define WRAP_DEF(n) \
    static uint64_t wrap_##n(uint64_t a, uint64_t b, uint64_t c, uint64_t d) \
    { return wrap_call(n, a, b, c, d); }
WRAP_DEF(0)  WRAP_DEF(1)  WRAP_DEF(2)  WRAP_DEF(3)
WRAP_DEF(4)  WRAP_DEF(5)  WRAP_DEF(6)  WRAP_DEF(7)
WRAP_DEF(8)  WRAP_DEF(9)  WRAP_DEF(10) WRAP_DEF(11)
WRAP_DEF(12) WRAP_DEF(13) WRAP_DEF(14) WRAP_DEF(15)
WRAP_DEF(16) WRAP_DEF(17) WRAP_DEF(18) WRAP_DEF(19)
WRAP_DEF(20) WRAP_DEF(21) WRAP_DEF(22) WRAP_DEF(23)
WRAP_DEF(24) WRAP_DEF(25) WRAP_DEF(26) WRAP_DEF(27)
WRAP_DEF(28) WRAP_DEF(29) WRAP_DEF(30) WRAP_DEF(31)

static void* g_thunks[NWRAP] = {
    (void*)wrap_0,  (void*)wrap_1,  (void*)wrap_2,  (void*)wrap_3,
    (void*)wrap_4,  (void*)wrap_5,  (void*)wrap_6,  (void*)wrap_7,
    (void*)wrap_8,  (void*)wrap_9,  (void*)wrap_10, (void*)wrap_11,
    (void*)wrap_12, (void*)wrap_13, (void*)wrap_14, (void*)wrap_15,
    (void*)wrap_16, (void*)wrap_17, (void*)wrap_18, (void*)wrap_19,
    (void*)wrap_20, (void*)wrap_21, (void*)wrap_22, (void*)wrap_23,
    (void*)wrap_24, (void*)wrap_25, (void*)wrap_26, (void*)wrap_27,
    (void*)wrap_28, (void*)wrap_29, (void*)wrap_30, (void*)wrap_31,
};

/* The real table may be shorter than NWRAP; never read past a mapped page. */
static int range_readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const BYTE*)p + n <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

static int in_real_image(const void* p)
{
    BYTE* base = (BYTE*)g_real;
    IMAGE_DOS_HEADER*   dos;
    IMAGE_NT_HEADERS64* nt;
    if (!g_real || !p) return 0;
    dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return (const BYTE*)p >= base &&
           (const BYTE*)p < base + nt->OptionalHeader.SizeOfImage;
}

static void* wrap_table(void* t)
{
    void** tab = (void**)t;
    if (!g_wrapped_ready) {
        for (int i = 0; i < NWRAP; i++) {
            int code;
            if (!range_readable(&tab[i], sizeof(void*))) {
                logline("T[%02d] unreadable - stop\n", i);
                break;
            }
            g_real_table[i] = tab[i];
            code = in_real_image(tab[i]);
            g_wrapped[i] = code ? g_thunks[i] : tab[i];
            logline("T[%02d]=%p%s\n", i, tab[i], code ? "  <- code" : "");
        }
        g_wrapped_ready = 1;
    }
    return g_wrapped;
}
#endif /* WRAP_TABLE */

__declspec(dllexport) void* __stdcall GetInterface(int version)
{
    if (version < 1 || version > 5) return NULL;

    if (!g_real) {
        WCHAR real[MAX_PATH];
        char  rp[300];
        resolve_real_path(real, MAX_PATH);
        hexdump_wide(real, rp, (int)sizeof(rp));
        logline("real dll: %s\n", rp);
        g_real = LoadLibraryW(real);
        if (!g_real) {
            logline("FATAL LoadLibrary real failed %lu\n", (unsigned)GetLastError());
            return NULL;
        }
        g_real_gi = (getinterface_fn)GetProcAddress(g_real, "GetInterface");
        logline("real loaded base=%p GetInterface=%p\n", (void*)g_real, (void*)g_real_gi);
        install_hooks();
        ensure_shm();      /* publish the mapping up front so the viewer can attach */
    }
    if (!g_real_gi) return NULL;

    void* t = g_real_gi(version);
    g_tables[version] = t;
#ifdef WRAP_TABLE
    /* Instrumented build: hand back a copy whose code pointers log first. */
    if (t && version == 5) {
        void* w = wrap_table(t);
        logline("GetInterface(%d) -> %p (WRAPPED)\n", version, w);
        return w;
    }
#endif
    /* the real table is handed back untouched */
    logline("GetInterface(%d) -> %p (pass-through)\n", version, t);
    return t;
}

__declspec(dllexport) long __stdcall DllRegisterServer(void)   { return 0; }
__declspec(dllexport) long __stdcall DllUnregisterServer(void) { return 0; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_inst = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_lock);
        InitializeCriticalSection(&g_rcap_lock);
        logline("=== proxy loaded pid=%lu base=%p ===\n",
                (unsigned)GetCurrentProcessId(), (void*)inst);
    }
    return TRUE;
}
