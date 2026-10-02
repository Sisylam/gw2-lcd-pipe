/*
 * test_cleanapi.c - offline integration test for LgLcdApi.clean.dll.
 *
 * Loads the DLL, fetches the GetInterface(5) table and drives it exactly as
 * Guild Wars 2 would, then checks the shared-memory frame and the soft-button
 * callback. No GW2, no Logitech binary and no server process are involved.
 *
 * Run with GW2 closed: the test shares Local\GW2LCDShim and Local\LGLCDCtl
 * with any running instance.
 *
 *   gcc -O2 -o test_cleanapi.exe test_cleanapi.c
 *   test_cleanapi.exe [path\to\LgLcdApi.clean.dll]
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void* (__stdcall *gi_fn)(int);

static int            g_fail;
static volatile LONG  g_cb_calls;
static volatile DWORD g_cb_btn;
static volatile int   g_cb_dev;

static DWORD WINAPI on_buttons(int device, DWORD buttons, const void* ctx)
{
    (void)ctx;
    g_cb_dev = device;
    if (buttons) g_cb_btn = buttons;
    InterlockedIncrement(&g_cb_calls);
    return 0;
}

#define CHECK(cond, ...) do {                        \
    printf(cond ? "ok   : " : "FAIL : ");            \
    printf(__VA_ARGS__);                             \
    printf("\n");                                    \
    if (!(cond)) g_fail = 1;                         \
} while (0)

static unsigned rd32(const void* p) { unsigned v; memcpy(&v, p, 4); return v; }
static void     wr32(void* p, unsigned v) { memcpy(p, &v, 4); }

int main(int argc, char** argv)
{
    const char* dll = (argc > 1) ? argv[1] : "LgLcdApi.clean.dll";
    WCHAR   wpath[MAX_PATH];
    HMODULE h;
    gi_fn   gi;
    void**  t;
    DWORD (WINAPI *p_init)(void);
    DWORD (WINAPI *p_connect)(void*);
    DWORD (WINAPI *p_open)(void*);
    DWORD (WINAPI *p_update)(int, const void*, DWORD);
    DWORD (WINAPI *p_fg)(int, int);
    unsigned char cctx[64], octx[64];
    HANDLE  shm, ctl;
    int     i;
    DWORD   dev;

    MultiByteToWideChar(CP_ACP, 0, dll, -1, wpath, MAX_PATH);
    h = LoadLibraryW(wpath);
    CHECK(h != NULL, "LoadLibrary(%s)", dll);
    if (!h) return 1;
    gi = (gi_fn)(void*)GetProcAddress(h, "GetInterface");
    CHECK(gi != NULL, "GetProcAddress(GetInterface)");
    if (!gi) return 1;

    t = (void**)gi(5);
    CHECK(t != NULL, "GetInterface(5) returns a table");
    if (!t) return 1;

    {
        void* used[5];
        int ok = 1, j;
        used[0]=t[0]; used[1]=t[4]; used[2]=t[13]; used[3]=t[24]; used[4]=t[26];
        for (i = 0; i < 5; i++) {
            if (!used[i]) ok = 0;
            for (j = i + 1; j < 5; j++)
                if (used[i] == used[j]) ok = 0;
        }
        for (i = 0; i < 29; i++) {
            if (i == 0 || i == 4 || i == 13 || i == 24 || i == 26) continue;
            for (j = 0; j < 5; j++)
                if (t[i] == used[j]) ok = 0;
        }
        CHECK(ok, "slots 0/4/13/24/26 are distinct, non-null and not aliased");
    }

    p_init    = (void*)t[0];
    p_connect = (void*)t[4];
    p_update  = (void*)t[13];
    p_fg      = (void*)t[24];
    p_open    = (void*)t[26];

    CHECK(p_init() == 0, "init() -> 0");

    memset(cctx, 0, sizeof(cctx));
    CHECK(p_connect(cctx) == 0, "connect() -> 0");
    CHECK(rd32(cctx + 32) != 0xffffffffu,
          "connect() set a connection handle (%u)", rd32(cctx + 32));

    memset(octx, 0, sizeof(octx));
    wr32(octx + 0, rd32(cctx + 32));
    wr32(octx + 4, 2);                         /* LGLCD_DEVICE_QVGA */
    *(void**)(octx + 8)  = (void*)on_buttons;  /* onSoftbuttonsChanged */
    *(void**)(octx + 16) = NULL;
    CHECK(p_open(octx) == 0, "open() -> 0");
    dev = rd32(octx + 24);
    CHECK(dev != 0xffffffffu, "open() set a device handle (%u)", (unsigned)dev);

    /* --- colour frame (LGLCD_BMP_FORMAT_QVGAx32) --- */
    {
        DWORD n = 4 + 320u * 240u * 4u;
        unsigned char* bmp = (unsigned char*)malloc(n);
        wr32(bmp, 3);
        for (i = 0; i < 320 * 240; i++) {
            bmp[4 + i*4 + 0] = 0x10;   /* B */
            bmp[4 + i*4 + 1] = 0x20;   /* G */
            bmp[4 + i*4 + 2] = 0x30;   /* R */
            bmp[4 + i*4 + 3] = 0xff;   /* A */
        }
        CHECK(p_update(dev, bmp, 0x80) == 0, "updateBitmap(colour) -> 0");
        free(bmp);
    }

    shm = OpenFileMappingW(FILE_MAP_READ, 0, L"Local\\GW2LCDShim");
    CHECK(shm != NULL, "shim section Local\\GW2LCDShim exists");
    if (shm) {
        unsigned char* sv = (unsigned char*)MapViewOfFile(shm, FILE_MAP_READ, 0, 0,
                                                          48 + 320u*240u*3u);
        CHECK(sv != NULL, "shim maps");
        if (sv) {
            CHECK(rd32(sv + 0) == 0x32313047u, "shim magic G012");
            CHECK(rd32(sv+8)==320 && rd32(sv+12)==240 && rd32(sv+16)==24,
                  "shim colour 320x240x24");
            CHECK(rd32(sv + 20) == 1, "shim active=1 (colour)");
            CHECK(sv[48]==0x30 && sv[49]==0x20 && sv[50]==0x10,
                  "BGRA->RGB pixel = 30 20 10 (got %02x %02x %02x)",
                  sv[48], sv[49], sv[50]);

            /* --- mono frame (LGLCD_BMP_FORMAT_160x43x1) --- */
            {
                DWORD seq = rd32(sv + 24);
                DWORD n = 4 + 160u * 43u;
                unsigned char* bmp = (unsigned char*)malloc(n);
                wr32(bmp, 1);
                for (i = 0; i < 160 * 43; i++) bmp[4 + i] = (i & 1) ? 0xff : 0x00;
                CHECK(p_update(dev, bmp, 0x80) == 0, "updateBitmap(mono) -> 0");
                free(bmp);
                CHECK(rd32(sv+8)==160 && rd32(sv+12)==43 && rd32(sv+16)==1,
                      "shim mono 160x43x1");
                CHECK(rd32(sv + 20) == 2, "shim active=2 (mono)");
                CHECK(sv[48]==0x00 && sv[49]==0xff, "mono pixel copied (got %02x %02x)",
                      sv[48], sv[49]);
                CHECK(rd32(sv + 24) != seq, "sequence advanced");
            }
            UnmapViewOfFile(sv);
        }
        CloseHandle(shm);
    }

    /* --- control channel drives the soft-button callback --- */
    ctl = OpenFileMappingW(FILE_MAP_ALL_ACCESS, 0, L"Local\\LGLCDCtl");
    CHECK(ctl != NULL, "control section Local\\LGLCDCtl exists");
    if (ctl) {
        volatile LONG* cv = (volatile LONG*)MapViewOfFile(ctl, FILE_MAP_ALL_ACCESS,
                                                          0, 0, 32);
        CHECK(cv != NULL, "control maps");
        if (cv) {
            CHECK(cv[0] == 0x31544347, "control magic GCT1");
            cv[2] = 0x200;                              /* [>] */
            InterlockedIncrement((volatile LONG*)&cv[1]);
            for (i = 0; i < 100 && g_cb_calls < 2; i++) Sleep(10);
            CHECK(g_cb_calls >= 2, "soft-button callback fired (%ld calls)", g_cb_calls);
            CHECK(g_cb_dev == (int)dev, "callback device = %d (got %d)", (int)dev, g_cb_dev);
            CHECK(g_cb_btn == 0x200u, "callback button = 0x200 (got 0x%x)",
                  (unsigned)g_cb_btn);
            UnmapViewOfFile((void*)cv);
        }
        CloseHandle(ctl);
    }

    CHECK(p_fg(dev, 1) == 0, "setAsLCDForegroundApp() -> 0");

    printf("\nRESULT: %s\n", g_fail ? "FAIL" : "PASS");
    FreeLibrary(h);
    return g_fail ? 1 : 0;
}
