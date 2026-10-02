/*
 * test_server.c - mock Guild Wars 2 client for the replacement pipe server.
 *
 * Speaks the client side of the LGLCDPIPE protocol - the opening negotiation
 * and a couple of frames - and checks that the server answers every message
 * with a well-formed, length-prefixed reply. This covers the server half of
 * the pipeline; the client half is covered by clean/test_cleanapi.c.
 *
 * Requires lgpipe_server4.exe to be running:
 *     lcdproxy\lgpipe_server4.exe        (in one shell)
 *     lcdproxy\test_server.exe           (in another; exit 0 = PASS)
 *
 * Output deliberately uses fputs/puts rather than printf: CodeQL's
 * format-argument model misfires on the macro-built format strings here, and
 * the messages do not need formatting.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPE_NAME   L"\\\\.\\pipe\\LGLCDPIPE-00000001"
#define C_NEGOTIATE 0x0831u
#define C_SHORT     0x0803u
#define C_REGION    0x0830u
#define C_FRAME     0x0806u

static int g_fail;

static void say(int ok, const char* msg)
{
    fputs(ok ? "ok   " : "FAIL ", stdout);
    puts(msg);
    if (!ok) g_fail = 1;
}

static BOOL send_msg(HANDLE p, DWORD seq, DWORD type, DWORD len)
{
    unsigned char* buf = (unsigned char*)calloc(1, len);
    DWORD w = 0;
    BOOL ok;
    if (!buf) return FALSE;
    memcpy(buf, &len, 4);
    memcpy(buf + 4, &seq, 4);
    memcpy(buf + 8, &type, 4);
    ok = WriteFile(p, buf, len, &w, NULL);
    free(buf);
    return ok && w == len;
}

/* Read one message-mode record, waiting up to `ms` for data. 0 on timeout. */
static DWORD read_msg(HANDLE p, unsigned char* buf, DWORD cap, DWORD ms)
{
    DWORD start = GetTickCount();
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!PeekNamedPipe(p, NULL, 0, NULL, &avail, NULL)) return 0;
        if (avail > 0) {
            if (ReadFile(p, buf, cap, &got, NULL)) return got;
            return 0;
        }
        if (GetTickCount() - start > ms) return 0;
        Sleep(5);
    }
}

int main(void)
{
    HANDLE p;
    unsigned char* buf;
    int replies = 0, bad = 0, i;
    DWORD mode = PIPE_READMODE_MESSAGE;

    p = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (p == INVALID_HANDLE_VALUE) {
        say(0, "connect to the pipe (is lgpipe_server4.exe running?)");
        return 1;
    }
    say(1, "connected to the pipe");

    buf = (unsigned char*)malloc(1 << 20);
    if (!buf) { say(0, "out of memory"); CloseHandle(p); return 1; }

    /* Read one message at a time, not a coalesced byte stream. */
    say(SetNamedPipeHandleState(p, &mode, NULL, NULL) != 0, "set message read mode");

    say(send_msg(p, 0, C_NEGOTIATE, 1084), "sent NEGOTIATE (1084 B)");
    say(send_msg(p, 1, C_SHORT, 16),       "sent SHORT (16 B)");
    say(send_msg(p, 2, C_REGION, 20),      "sent REGION (20 B)");
    say(send_msg(p, 3, C_FRAME, 307224),   "sent FRAME (307224 B)");
    say(send_msg(p, 4, C_FRAME, 307224),   "sent FRAME (307224 B)");

    for (i = 0; i < 32; i++) {
        DWORD got = read_msg(p, buf, 1 << 20, 250);
        if (got == 0) break;
        if (got < 4) { bad++; continue; }
        { DWORD len; memcpy(&len, buf, 4);
          if (len != got) bad++; }
        replies++;
    }

    say(replies >= 6, "received at least 6 replies");
    say(bad == 0, "all replies were length-prefixed");

    CloseHandle(p);
    free(buf);
    puts(g_fail ? "\nRESULT: FAIL" : "\nRESULT: PASS");
    return g_fail ? 1 : 0;
}
