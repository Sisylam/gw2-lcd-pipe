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

#define CHECK(cond, ...) do {                        \
    printf(cond ? "ok   : " : "FAIL : ");            \
    printf(__VA_ARGS__);                             \
    printf("\n");                                    \
    if (!(cond)) g_fail = 1;                         \
} while (0)

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

    p = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (p == INVALID_HANDLE_VALUE) {
        printf("FAIL : connect to %ls (err %lu) - is lgpipe_server4.exe running?\n",
               PIPE_NAME, (unsigned long)GetLastError());
        return 1;
    }
    CHECK(1, "connected to %ls", PIPE_NAME);

    buf = (unsigned char*)malloc(1 << 20);
    if (!buf) { printf("FAIL : out of memory\n"); CloseHandle(p); return 1; }

    /* Read one message at a time, not a coalesced byte stream. */
    {
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(p, &mode, NULL, NULL))
            printf("  (message read mode not set: err %lu)\n",
                   (unsigned long)GetLastError());
    }

    CHECK(send_msg(p, 0, C_NEGOTIATE, 1084), "sent NEGOTIATE (1084 B)");
    CHECK(send_msg(p, 1, C_SHORT, 16),       "sent SHORT (16 B)");
    CHECK(send_msg(p, 2, C_REGION, 20),      "sent REGION (20 B)");
    CHECK(send_msg(p, 3, C_FRAME, 307224),   "sent FRAME (307224 B)");
    CHECK(send_msg(p, 4, C_FRAME, 307224),   "sent FRAME (307224 B)");

    for (i = 0; i < 32; i++) {
        DWORD got = read_msg(p, buf, 1 << 20, 250);
        if (got == 0) break;
        if (got < 4) {
            printf("  malformed reply: %lu bytes (<4)\n", (unsigned long)got);
            bad++;
            continue;
        }
        { DWORD len; memcpy(&len, buf, 4);
          if (len != got) {
              printf("  malformed reply: len=%u got=%lu head=%02x%02x%02x%02x\n",
                     (unsigned)len, (unsigned long)got, buf[0], buf[1], buf[2], buf[3]);
              bad++;
          } }
        replies++;
    }

    CHECK(replies >= 6, "received >= 6 replies (%d)", replies);
    CHECK(bad == 0, "all replies were length-prefixed (%d malformed)", bad);

    CloseHandle(p);
    free(buf);
    printf("\nRESULT: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
