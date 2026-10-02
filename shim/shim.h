#ifndef SHIM_H
#define SHIM_H

#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define COLOR_W 320
#define COLOR_H 240
#define MONO_W  128
#define MONO_H  48

#define SHM_NAME  L"Local\\GW2LCDShim"
#define SHM_MAGIC 0x32313047u
#define SHM_VER   1

#define LOG_PATH "C:\\Users\\Sisyphos\\AppData\\Local\\Temp\\opencode\\gw2lcd.log"

typedef struct ShimHeader {
    uint32_t magic, version, width, height, bpp, active, sequence;
    uint32_t connected, init_count, text_count, update_count, reserved;
} ShimHeader;

extern uint8_t g_color[COLOR_W * COLOR_H * 3];
extern uint8_t g_mono[MONO_W * MONO_H];
extern int g_active;      /* 1 colour, 2 mono */
extern uint32_t g_seq, g_text, g_updates, g_inits;
extern HANDLE g_file;
extern void *g_view;
extern HMODULE g_module;
extern CRITICAL_SECTION g_lock;

void logline(const char *fmt, ...);
int probe_string(uintptr_t p, char *out, size_t outsz);
void publish(void);
void create_mapping(void);

#endif
