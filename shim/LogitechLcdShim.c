/*
 * LogitechLcdShim - intercepts the Logitech LCD framebuffer that Guild Wars 2
 * renders, so it can be shown on any surface (window, MCU, ...).
 *
 * GW2 finds the real DLL by reading
 *   HKCR\HKLM\SOFTWARE\Classes\CLSID\{d0e790a5-01a7-49ae-ae0b-e986bdd0c21b}\ServerBinary
 * then loading that path and GetProcAddress'ing the LogiLcd* exports.
 *
 * ABI trick: on x64 the first four integer args arrive in RCX/RDX/R8/R9 and the
 * rest on the stack, so declaring every export as taking six uintptr_t captures
 * the real arguments without knowing the true prototypes. Any argument that
 * looks like a pointer is probed for a readable ASCII string, which identifies
 * the text argument of the Set*Text functions.
 */

#include "shim.h"

uint8_t g_color[COLOR_W * COLOR_H * 3];
uint8_t g_mono[MONO_W * MONO_H];
int g_active = 0; /* 1 colour, 2 mono */
uint32_t g_seq = 0, g_text = 0, g_updates = 0, g_inits = 0;
HANDLE g_file = INVALID_HANDLE_VALUE;
void *g_view = NULL;
HMODULE g_module = NULL;
CRITICAL_SECTION g_lock;

void logline(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    DWORD w = 0;
    va_start(ap, fmt);
    if (vsnprintf(buf, sizeof(buf), fmt, ap) < 0) {
        va_end(ap);
        return;
    }
    va_end(ap);
    if (g_file == INVALID_HANDLE_VALUE) {
        g_file = CreateFileA(LOG_PATH, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_file == INVALID_HANDLE_VALUE) return;
    }
    WriteFile(g_file, buf, (DWORD)strlen(buf), &w, NULL);
    WriteFile(g_file, "\r\n", 2, &w, NULL);
}

/* Safely try to read a NUL-terminated printable ASCII string.
 * Uses VirtualQuery so it needs no compiler-specific SEH. */
int probe_string(uintptr_t p, char *out, size_t outsz) {
    size_t i;
    MEMORY_BASIC_INFORMATION mbi;
    if (p < 0x10000) return 0;
    out[0] = '\0';
    if (VirtualQuery((const void *)p, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return 0;
    for (i = 0; i < outsz - 1 && i < 400; i++) {
        char c = ((const char *)p)[i];
        if (c == '\0') { out[i] = '\0'; return (int)i; }
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) { out[i] = '\0'; return 0; }
        out[i] = c;
    }
    out[outsz - 1] = '\0';
    return 0;
}

void publish(void) {
    ShimHeader *h;
    uint8_t *body;
    if (!g_view) return;
    EnterCriticalSection(&g_lock);
    h = (ShimHeader *)g_view;
    body = (uint8_t *)g_view + sizeof(ShimHeader);
    h->sequence = ++g_seq;
    h->init_count = g_inits;
    h->text_count = g_text;
    h->update_count = ++g_updates;
    h->connected = 1;
    if (g_active == 1) {
        h->width = COLOR_W; h->height = COLOR_H; h->bpp = 3;
        memcpy(body, g_color, sizeof(g_color));
    } else if (g_active == 2) {
        h->width = MONO_W; h->height = MONO_H; h->bpp = 1;
        memcpy(body, g_mono, sizeof(g_mono));
    }
    h->active = (uint32_t)g_active;
    h->version = SHM_VER;
    h->magic = SHM_MAGIC;
    LeaveCriticalSection(&g_lock);
}

void create_mapping(void) {
    size_t total = sizeof(ShimHeader) + sizeof(g_color);
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                  (DWORD)(total >> 32), (DWORD)(total & 0xFFFFFFFFu),
                                  SHM_NAME);
    if (!m) return;
    g_view = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
}
