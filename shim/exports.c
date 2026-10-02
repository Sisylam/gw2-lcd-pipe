
#include "shim.h"

/* ---------------------------------------------------------------- exports */

#define EXPORT __declspec(dllexport)

EXPORT uint32_t LogiLcdInit(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                            uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    g_inits++;
    memset(g_color, 0, sizeof(g_color));
    memset(g_mono, 0, sizeof(g_mono));
    logline("LogiLcdInit()");
    return 1;
}

EXPORT uint32_t LogiLcdShutdown(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                 uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    g_active = 0;
    logline("LogiLcdShutdown()");
    return 1;
}

/* Report "connected" so the game actually renders its screens. */
EXPORT uint32_t LogiLcdIsConnected(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                   uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 1;
}

EXPORT uint32_t LogiLcdIsButtonPressed(uintptr_t b, uintptr_t a2, uintptr_t a3,
                                       uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)b; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0;
}

EXPORT uint32_t LogiLcdUpdate(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                              uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    publish();
    return 1;
}

/* ---- monochrome ---- */

EXPORT uint32_t LogiLcdMonoSetBackground(uintptr_t r, uintptr_t g, uintptr_t b,
                                         uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a4; (void)a5; (void)a6;
    g_active = 2;
    memset(g_mono, (r || g || b) ? 0xFF : 0x00, sizeof(g_mono));
    logline("MonoSetBackground(%u,%u,%u)", (unsigned)r, (unsigned)g, (unsigned)b);
    return 1;
}

EXPORT uint32_t LogiLcdMonoResetBackgroundUDK(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                             uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    g_active = 2;
    memset(g_mono, 0, sizeof(g_mono));
    return 1;
}

EXPORT uint32_t LogiLcdMonoSetBackgroundUDK(uintptr_t x, uintptr_t y, uintptr_t px,
                                            uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a4; (void)a5; (void)a6;
    g_active = 2;
    if (x < MONO_W && y < MONO_H) g_mono[y * MONO_W + x] = px ? 0xFF : 0x00;
    return 1;
}

EXPORT uint32_t LogiLcdMonoSetText(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                   uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    char s[512];
    uintptr_t args[6];
    int i, found = 0;
    args[0] = a1; args[1] = a2; args[2] = a3; args[3] = a4; args[4] = a5; args[5] = a6;
    g_active = 2;
    g_text++;
    s[0] = '\0';
    for (i = 0; i < 6; i++)
        if (probe_string(args[i], s, sizeof(s)) > 0) { found = 1; break; }
    logline("MonoSetText(raw=%u,%u,%u | str_arg=%d '%s')",
            (unsigned)a1, (unsigned)a2, (unsigned)a3, found ? i : -1, s);
    return 1;
}

/* ---- colour ---- */

EXPORT uint32_t LogiLcdColorSetBackground(uintptr_t r, uintptr_t g, uintptr_t b,
                                          uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    size_t i;
    (void)a4; (void)a5; (void)a6;
    g_active = 1;
    for (i = 0; i < COLOR_W * COLOR_H; i++) {
        g_color[i*3+0] = (uint8_t)r; /* R */
        g_color[i*3+1] = (uint8_t)g; /* G */
        g_color[i*3+2] = (uint8_t)b; /* B */
    }
    logline("ColorSetBackground(%u,%u,%u)", (unsigned)r, (unsigned)g, (unsigned)b);
    return 1;
}

EXPORT uint32_t LogiLcdColorResetBackgroundUDK(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                              uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    g_active = 1;
    memset(g_color, 0, sizeof(g_color));
    return 1;
}

/* Pixel setter. GW2's pixel encoding is discovered from the log. */
EXPORT uint32_t LogiLcdColorSetBackgroundUDK(uintptr_t x, uintptr_t y, uintptr_t px,
                                             uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    (void)a4; (void)a5; (void)a6;
    g_active = 1;
    if (x < COLOR_W && y < COLOR_H) {
        /* px assumed to pack 0x00RRGGBB; verified against the real DLL's log */
        g_color[(y * COLOR_W + x) * 3 + 0] = (uint8_t)((px >> 16) & 0xFF);
        g_color[(y * COLOR_W + x) * 3 + 1] = (uint8_t)((px >> 8) & 0xFF);
        g_color[(y * COLOR_W + x) * 3 + 2] = (uint8_t)(px & 0xFF);
    }
    return 1;
}

static uint32_t set_text(const char *fn, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                         uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    uintptr_t args[6];
    char s[512];
    int i, found = 0;
    args[0] = a1; args[1] = a2; args[2] = a3; args[3] = a4; args[4] = a5; args[5] = a6;
    g_active = 1;
    g_text++;
    s[0] = '\0';
    for (i = 0; i < 6; i++)
        if (probe_string(args[i], s, sizeof(s)) > 0) { found = 1; break; }
    logline("%s(raw=%u,%u,%u,%u | str_arg=%d len=%u '%s')", fn,
            (unsigned)a1, (unsigned)a2, (unsigned)a3, (unsigned)a4,
            found ? i : -1, (unsigned)strlen(s), s);
    return 1;
}

EXPORT uint32_t LogiLcdColorSetText(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                    uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    return set_text("ColorSetText", a1, a2, a3, a4, a5, a6);
}

EXPORT uint32_t LogiLcdColorSetTitle(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                     uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    return set_text("ColorSetTitle", a1, a2, a3, a4, a5, a6);
}

/* The shim doubles as the COM server so the CLSID resolves. */
EXPORT uint32_t DllRegisterServer(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                  uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    HKEY clsid, sub;
    wchar_t self[MAX_PATH];
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!g_module) return 0;
    if (!GetModuleFileNameW(g_module, self, MAX_PATH)) return 0;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Classes\\CLSID\\{d0e790a5-01a7-49ae-ae0b-e986bdd0c21b}",
            0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &clsid, NULL) != ERROR_SUCCESS)
        return 0;
    /* ServerBinary is a SUBKEY whose default value is the DLL path. */
    if (RegCreateKeyExW(clsid, L"ServerBinary", 0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, NULL, &sub, NULL) != ERROR_SUCCESS) {
        RegCloseKey(clsid);
        return 0;
    }
    if (RegSetValueExW(sub, NULL, 0, REG_SZ, (const BYTE *)self,
                       (DWORD)((wcslen(self) + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) {
        RegCloseKey(sub);
        RegCloseKey(clsid);
        return 0;
    }
    RegCloseKey(sub);
    RegCloseKey(clsid);
    logline("DllRegisterServer -> %ls", self);
    return 1;
}

EXPORT uint32_t DllUnregisterServer(uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                    uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    HKEY clsid;
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Classes\\CLSID\\{d0e790a5-01a7-49ae-ae0b-e986bdd0c21b}",
            0, KEY_WRITE, &clsid) != ERROR_SUCCESS)
        return 0;
    RegDeleteKeyW(clsid, L"ServerBinary");
    RegCloseKey(clsid);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_lock);
        create_mapping();
        if (g_view) memset(g_view, 0, sizeof(ShimHeader) + sizeof(g_color));
        logline("--- shim loaded (pid %u) ---", (unsigned)GetCurrentProcessId());
    }
    return TRUE;
}
