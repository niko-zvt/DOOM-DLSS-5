/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomtype.h"

#define boolean BOOLEAN_WIN32_AVOID
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef boolean
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"
#include "d_net.h"
#include "g_game.h"
#include "i_system.h"

int mb_used = 32;

void I_Tactile(int on, int off, int total)
{
    on = off = total = 0;
}

ticcmd_t emptycmd;

ticcmd_t *I_BaseTiccmd(void)
{
    return &emptycmd;
}

int I_GetHeapSize(void)
{
    return mb_used * 1024 * 1024;
}

byte *I_ZoneBase(int *size)
{
    *size = mb_used * 1024 * 1024;
    return (byte *)malloc(*size);
}

int I_GetTime(void)
{
    static LARGE_INTEGER freq;
    static LARGE_INTEGER start;
    LARGE_INTEGER now;

    if (!freq.QuadPart)
    {
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);
    }
    QueryPerformanceCounter(&now);
    return (int)((now.QuadPart - start.QuadPart) * TICRATE / freq.QuadPart);
}

void I_Init(void)
{
    I_InitSound();
}

void I_Quit(void)
{
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    exit(0);
}

void I_WaitVBL(int count)
{
    if (count < 1)
	count = 1;
    Sleep((DWORD)(count * (1000 / 70)));
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

byte *I_AllocLow(int length)
{
    byte *mem;

    mem = (byte *)malloc(length);
    memset(mem, 0, length);
    return mem;
}

extern boolean demorecording;

void I_Error(char *error, ...)
{
    char buf[1024];
    va_list argptr;

    va_start(argptr, error);
    _vsnprintf(buf, sizeof(buf), error, argptr);
    buf[sizeof(buf) - 1] = 0;
    va_end(argptr);

    fprintf(stderr, "Error: %s\n", buf);
    fflush(stderr);
    MessageBoxA(NULL, buf, "WinDoom", MB_OK | MB_ICONERROR);

    if (demorecording)
	G_CheckDemoStatus();

    D_QuitNetGame();
    I_ShutdownGraphics();
    exit(-1);
}
