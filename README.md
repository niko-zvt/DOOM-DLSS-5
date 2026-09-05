# WinDoom

Windows x64 port of Linux DOOM 1.10. Same 320x200 software renderer,
shown in a D3D11 window at 1280x800. Color, depth, normals, and motion
are written out each frame.

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.
Original game code: id Software, 1993-1996.

Carmack's old note is in README.TXT.

## Build

    cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64
    cmake --build build-win --config Release

Use whatever VS generator you have (17 2022, 18 2026, ...).

## Run

Put an IWAD in `wads/` (`freedoom2.wad`, `doom2.wad`, etc.), then:

    play-windoom.cmd

Or run `build-win\Release\windoom.exe` with `DOOMWADDIR` pointing at
that folder.

## Keys

    WASD          move
    arrows        also move
    mouse         turn
    fire          left button
    Esc           menu
    Y / N         quit prompt
    F1 F2 F3 F4   color, depth, normals, velocity

## Tree

    linuxdoom-1.10/   original sources
    win32/            Windows video, sound, G-buffers
    wads/             IWADs
    CMakeLists.txt    `windoom` target

Buffer formats: `win32/README-GBUFFER.md`.
