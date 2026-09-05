/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>

#include "doomdef.h"
#include "doomtype.h"

#define boolean BOOLEAN_WIN32_AVOID
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef boolean
#include "m_argv.h"
#include "d_main.h"

static char *dupstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    memcpy(d, s, n);
    return d;
}

static int file_exists(const char *path)
{
    return _access(path, 0) == 0;
}

static int dir_has_iwad(const char *dir)
{
    char buf[MAX_PATH];
    _snprintf(buf, sizeof(buf), "%s\\freedoom1.wad", dir);
    if (file_exists(buf))
	return 1;
    _snprintf(buf, sizeof(buf), "%s\\freedoom2.wad", dir);
    if (file_exists(buf))
	return 1;
    _snprintf(buf, sizeof(buf), "%s\\doom2.wad", dir);
    if (file_exists(buf))
	return 1;
    _snprintf(buf, sizeof(buf), "%s\\doom.wad", dir);
    return file_exists(buf);
}

static int try_set_waddir(const char *dir)
{
    char wadsub[MAX_PATH];

    if (!dir || !dir[0])
	return 0;
    if (dir_has_iwad(dir))
    {
	_putenv_s("DOOMWADDIR", dir);
	return 1;
    }
    _snprintf(wadsub, sizeof(wadsub), "%s\\wads", dir);
    if (dir_has_iwad(wadsub))
    {
	_putenv_s("DOOMWADDIR", wadsub);
	return 1;
    }
    return 0;
}

static void setup_env(void)
{
    char exe[MAX_PATH];
    char dir[MAX_PATH];
    char parent[MAX_PATH];
    char repo[MAX_PATH];
    char cwd[MAX_PATH];
    char *slash;
    const char *home;

    home = getenv("USERPROFILE");
    if (home && !getenv("HOME"))
	_putenv_s("HOME", home);

    if (getenv("DOOMWADDIR"))
	return;

    GetModuleFileNameA(NULL, exe, MAX_PATH);
    strncpy(dir, exe, MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    slash = strrchr(dir, '\\');
    if (slash)
	*slash = 0;

    strncpy(parent, dir, MAX_PATH - 1);
    parent[MAX_PATH - 1] = 0;
    slash = strrchr(parent, '\\');
    if (slash)
	*slash = 0;
    strncpy(repo, parent, MAX_PATH - 1);
    repo[MAX_PATH - 1] = 0;
    slash = strrchr(repo, '\\');
    if (slash)
	*slash = 0;

    GetCurrentDirectoryA(MAX_PATH, cwd);

    if (try_set_waddir(cwd))
	return;
    if (try_set_waddir(dir))
	return;
    if (try_set_waddir(parent))
	return;
    if (try_set_waddir(repo))
	return;
    try_set_waddir(".");
}

int main(int argc, char **argv)
{
    int i;

    setup_env();
    myargc = argc;
    myargv = (char **)malloc((argc + 1) * sizeof(char *));
    for (i = 0; i < argc; i++)
	myargv[i] = dupstr(argv[i]);
    myargv[argc] = NULL;

    D_DoomMain();
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
		   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main(__argc, __argv);
}
