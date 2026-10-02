/*
 * cleanapi.c - a from-scratch implementation of the Logitech LCD client API
 * that Guild Wars 2 imports (CLSID {FE750200-B72E-11d9-829B-0050DA1A72D3}).
 *
 * This replaces both LgLcdApi.dll and LCore.exe: GW2 calls GetInterface(5) and
 * receives a function table we own. Only five entries are used by GW2 (see
 * docs/PROTOCOL.md, "Client API surface"):
 *
 *     slot 0   lgLcdInit
 *     slot 4   lgLcdConnect / lgLcdConnectEx
 *     slot 13  lgLcdUpdateBitmap
 *     slot 24  lgLcdSetAsLCDForegroundApp
 *     slot 26  lgLcdOpen / lgLcdOpenByType
 *
 * The remaining 24 slots are success stubs (GW2 never calls them, confirmed by
 * the WRAP_TABLE instrumentation). Frames are published to the Local\GW2LCDShim
 * section - the same contract the proxy uses - and soft buttons are read from
 * Local\LGLCDCtl and delivered through the onSoftbuttonsChanged callback that
 * GW2 registers at open time.
 *
 * No Logitech code is used. The API shape comes from the public SDK header
 * lglcd.h (shipped by mpc-hc / MPC-BE / mumble); the slot indices were
 * recovered empirically, not copied.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* --- Local\GW2LCDShim: 48-byte header of 12 u32, then width*height*3 RGB --- */
#define SHM_NAME   L"Local\\GW2LCDShim"
#define SHM_MAGIC  0x32313047u          /* "G012" */
#define SHM_VER    1u
#define SHM_HDR    48u
#define SHM_MAX    (320u * 240u * 3u)

/* --- Local\LGLCDCtl: 32 bytes, created here, written by the viewer -------- */
#define CTL_NAME   L"Local\\LGLCDCtl"
#define CTL_MAGIC  0x31544347u          /* "GCT1" */
#define CTL_BYTES  32u

#define DEVICE_HANDLE 0x65              /* matches what the real client reports */
#define CONN_HANDLE   1

/* lgLcdBitmap formats (lglcd.h) */
#define BMP_160x43x1  1u
#define BMP_QVGAx32   3u

/* --- diagnostics (compile with -DCLEAN_DEBUG to enable) ------------------- */
#include <stdarg.h>
#ifdef CLEAN_DEBUG
static void dbglog(const char* fmt, ...)
{
    char buf[512];
    WCHAR path[MAX_PATH];
    DWORD len, i, w = 0;
    int n;
    va_list ap;
    HANDLE f;
    const WCHAR* tail = L"cleanapi.log";
    va_start(ap, fmt);
    n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n] = 0;
    len = GetTempPathW(MAX_PATH, path);
    if (!len || len + 12 >= MAX_PATH) return;
    for (i = 0; tail[i]; i++) path[len + i] = tail[i];
    path[len + i] = 0;
    f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, buf, (DWORD)n, &w, NULL);
        CloseHandle(f);
    }
}
#else
#define dbglog(...) ((void)0)
#endif

static HANDLE   g_shm_map, g_ctl_map;
static uint8_t* g_shm;
static LONG*    g_ctl;
static volatile LONG g_ctl_seen;
static HANDLE   g_thread;
static volatile LONG g_stop;

typedef DWORD (WINAPI *softbtn_cb)(int device, DWORD buttons, const void* context);
static softbtn_cb  g_cb;
static const void* g_cb_ctx;
static int         g_device = -1;

typedef DWORD (WINAPI *notify_cb)(int connection, const void* context,
                                  DWORD code, DWORD p1, DWORD p2, DWORD p3, DWORD p4);
static notify_cb   g_notify;
static const void* g_notify_ctx;
static int         g_conn = -1;
static volatile LONG g_need_arrival;

#define NOTIFY_DEVICE_ARRIVAL 1u
#define DEVICE_QVGA           2u

/* ------------------------------------------------------------------ shim --- */

static void ensure_shm(void)
{
    uint32_t* h;
    if (g_shm) return;
    g_shm_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                   0, SHM_HDR + SHM_MAX, SHM_NAME);
    if (!g_shm_map) return;
    g_shm = (uint8_t*)MapViewOfFile(g_shm_map, FILE_MAP_ALL_ACCESS, 0, 0,
                                    SHM_HDR + SHM_MAX);
    if (!g_shm) { CloseHandle(g_shm_map); g_shm_map = NULL; return; }
    h = (uint32_t*)g_shm;
    h[0] = SHM_MAGIC; h[1] = SHM_VER; h[2] = 320; h[3] = 240;
    h[4] = 24; h[5] = 1; h[6] = 0; h[7] = 1;
    h[8] = 0; h[9] = 0; h[10] = 0; h[11] = 0;
}

/* GW2 sends the QVGA frame as BGRA; the viewer wants RGB. */
static void publish_color(const uint8_t* bgra)
{
    uint8_t* dst;
    uint32_t n, i;
    uint32_t* h;
    if (!g_shm) return;
    dst = g_shm + SHM_HDR;
    n = 320u * 240u;
    for (i = 0; i < n; i++) {
        const uint8_t* s = bgra + i * 4;
        dst[i * 3 + 0] = s[2];
        dst[i * 3 + 1] = s[1];
        dst[i * 3 + 2] = s[0];
    }
    h = (uint32_t*)g_shm;
    h[2] = 320; h[3] = 240; h[4] = 24; h[5] = 1;
    h[10]++;
    InterlockedIncrement((volatile LONG*)&h[6]);
}

static void publish_mono(const uint8_t* mono)
{
    uint32_t* h;
    if (!g_shm) return;
    memcpy(g_shm + SHM_HDR, mono, 160u * 43u);
    h = (uint32_t*)g_shm;
    h[2] = 160; h[3] = 43; h[4] = 1; h[5] = 2;
    h[10]++;
    InterlockedIncrement((volatile LONG*)&h[6]);
}

/* --------------------------------------------------------------- control --- */

static DWORD WINAPI worker_thread(LPVOID p)
{
    (void)p;
    while (!g_stop) {
        /* Deliver the device-arrival notification the real client would send
           after connect; GW2 only calls open() once it has seen this. */
        if (InterlockedExchange(&g_need_arrival, 0)) {
            if (g_notify) {
                dbglog("notify arrival -> %p\n", (void*)g_notify);
                g_notify(g_conn, g_notify_ctx, NOTIFY_DEVICE_ARRIVAL,
                         DEVICE_QVGA, 0, 0, 0);
            }
        }
        if (g_ctl && g_ctl[0] == (LONG)CTL_MAGIC) {
            LONG seen = g_ctl[1];
            LONG prev = InterlockedExchange(&g_ctl_seen, seen);
            if (seen != prev) {
                DWORD btn = (DWORD)g_ctl[2];
                dbglog("ctl counter=%d btn=%08x cb=%p dev=%d\n",
                       seen, btn, (void*)g_cb, g_device);
                if (btn && g_cb && g_device >= 0) {
                    g_cb(g_device, btn, g_cb_ctx);   /* press   */
                    Sleep(60);                       /* let GW2 see the press */
                    g_cb(g_device, 0,   g_cb_ctx);   /* release */
                }
            }
        }
        Sleep(15);
    }
    return 0;
}

static void ensure_thread(void)
{
    if (!g_thread)
        g_thread = CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);
}

static void ensure_ctl(void)
{
    if (!g_ctl) {
        g_ctl_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                       0, CTL_BYTES, CTL_NAME);
        if (g_ctl_map) {
            g_ctl = (LONG*)MapViewOfFile(g_ctl_map, FILE_MAP_ALL_ACCESS, 0, 0,
                                         CTL_BYTES);
            if (g_ctl) {
                if (g_ctl[0] != (LONG)CTL_MAGIC) {
                    g_ctl[0] = (LONG)CTL_MAGIC;
                    g_ctl[1] = 0; g_ctl[2] = 0; g_ctl[3] = 0;
                }
                g_ctl_seen = g_ctl[1];
            } else {
                CloseHandle(g_ctl_map);
                g_ctl_map = NULL;
            }
        }
    }
}

/* -------------------------------------------------------- interface slots --- */

static DWORD WINAPI api_init(void)
{
    dbglog("init\n");
    ensure_shm();
    ensure_ctl();
    ensure_thread();
    return 0;                                    /* ERROR_SUCCESS */
}

/*
 * The connection handle sits at offset 32 of lgLcdConnectContextW and of the
 * Ex variant (appFriendlyName, two BOOLs, a 16-byte configure context).
 */
static DWORD WINAPI api_connect(void* ctx)
{
    if (ctx) {
        dbglog("connect ctx=%p name=%p conn=%d cap=%08x r1=%08x notify=%p nctx=%p\n",
               ctx,
               *(void**)((uint8_t*)ctx + 0),
               *(int*)((uint8_t*)ctx + 32),
               *(DWORD*)((uint8_t*)ctx + 36),
               *(DWORD*)((uint8_t*)ctx + 40),
               *(void**)((uint8_t*)ctx + 48),
               *(void**)((uint8_t*)ctx + 56));
        *(int*)((uint8_t*)ctx + 32) = CONN_HANDLE;
        g_conn       = CONN_HANDLE;
        g_notify     = (notify_cb)*(void**)((uint8_t*)ctx + 48);
        g_notify_ctx = *(void**)((uint8_t*)ctx + 56);
        ensure_thread();
        InterlockedExchange(&g_need_arrival, 1);
    }
    return 0;
}

/*
 * lgLcdOpen and lgLcdOpenByType share a layout:
 *   { int connection; int index/deviceType; {cb, ctx}; int device; }
 * so one implementation serves slot 26 either way.
 */
static DWORD WINAPI api_open(void* ctx)
{
    if (ctx) {
        g_cb     = (softbtn_cb)*(void**)((uint8_t*)ctx + 8);
        g_cb_ctx = *(void**)((uint8_t*)ctx + 16);
        g_device = DEVICE_HANDLE;
        *(int*)((uint8_t*)ctx + 24) = DEVICE_HANDLE;
    }
    dbglog("open ctx=%p cb=%p cbctx=%p dev=%d\n",
         ctx, (void*)g_cb, g_cb_ctx, g_device);
    ensure_shm();
    ensure_ctl();
    return 0;
}

static DWORD WINAPI api_update_bitmap(int device, const void* bitmap, DWORD priority)
{
    const uint8_t* px;
    DWORD format;
    (void)device; (void)priority;
    if (!bitmap) return 0;
    format = *(const DWORD*)bitmap;              /* lgLcdBitmapHeader.Format */
    px = (const uint8_t*)bitmap + 4;
    dbglog("update dev=%d fmt=%u prio=%u\n", device, format, priority);
    if (format == BMP_QVGAx32)       publish_color(px);
    else if (format == BMP_160x43x1) publish_mono(px);
    return 0;
}

static DWORD WINAPI api_set_foreground(int device, int flag)
{
    (void)device; (void)flag;      /* only read by the debug log */
    dbglog("setfg dev=%d flag=%d\n", device, flag);
    return 0;
}

/* ------------------------------------------------------------- interface --- */

#define IFACE_SLOTS 29

/* One logging stub per slot, so the log shows every call GW2 makes. */
#define STUB_DEF(n) \
    static DWORD WINAPI api_stub_##n(void) { dbglog("slot %d (stub)\n", n); return 0; }
STUB_DEF(0)  STUB_DEF(1)  STUB_DEF(2)  STUB_DEF(3)  STUB_DEF(4)
STUB_DEF(5)  STUB_DEF(6)  STUB_DEF(7)  STUB_DEF(8)  STUB_DEF(9)
STUB_DEF(10) STUB_DEF(11) STUB_DEF(12) STUB_DEF(13) STUB_DEF(14)
STUB_DEF(15) STUB_DEF(16) STUB_DEF(17) STUB_DEF(18) STUB_DEF(19)
STUB_DEF(20) STUB_DEF(21) STUB_DEF(22) STUB_DEF(23) STUB_DEF(24)
STUB_DEF(25) STUB_DEF(26) STUB_DEF(27) STUB_DEF(28)

static void* g_stubs[IFACE_SLOTS] = {
    (void*)api_stub_0,  (void*)api_stub_1,  (void*)api_stub_2,  (void*)api_stub_3,
    (void*)api_stub_4,  (void*)api_stub_5,  (void*)api_stub_6,  (void*)api_stub_7,
    (void*)api_stub_8,  (void*)api_stub_9,  (void*)api_stub_10, (void*)api_stub_11,
    (void*)api_stub_12, (void*)api_stub_13, (void*)api_stub_14, (void*)api_stub_15,
    (void*)api_stub_16, (void*)api_stub_17, (void*)api_stub_18, (void*)api_stub_19,
    (void*)api_stub_20, (void*)api_stub_21, (void*)api_stub_22, (void*)api_stub_23,
    (void*)api_stub_24, (void*)api_stub_25, (void*)api_stub_26, (void*)api_stub_27,
    (void*)api_stub_28,
};

static void* g_iface[IFACE_SLOTS];

static void build_iface(void)
{
    int i;
    for (i = 0; i < IFACE_SLOTS; i++) g_iface[i] = g_stubs[i];
    g_iface[0]  = (void*)api_init;
    g_iface[4]  = (void*)api_connect;
    g_iface[13] = (void*)api_update_bitmap;
    g_iface[24] = (void*)api_set_foreground;
    g_iface[26] = (void*)api_open;
}

__declspec(dllexport) void* __stdcall GetInterface(int version)
{
    (void)version;
    return g_iface;
}

__declspec(dllexport) long __stdcall DllRegisterServer(void)   { return 0; }
__declspec(dllexport) long __stdcall DllUnregisterServer(void) { return 0; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        build_iface();
    } else if (reason == DLL_PROCESS_DETACH) {
        g_stop = 1;
        if (g_thread) {
            WaitForSingleObject(g_thread, 200);
            CloseHandle(g_thread);
            g_thread = NULL;
        }
        if (g_ctl)     { UnmapViewOfFile(g_ctl);   g_ctl = NULL; }
        if (g_ctl_map) { CloseHandle(g_ctl_map);   g_ctl_map = NULL; }
        if (g_shm)     { UnmapViewOfFile(g_shm);   g_shm = NULL; }
        if (g_shm_map) { CloseHandle(g_shm_map);   g_shm_map = NULL; }
    }
    return TRUE;
}
