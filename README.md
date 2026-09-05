# WinDoom

Windows x64 port of Linux DOOM 1.10. Same 320x200 software renderer,
shown in a D3D12 window at 1280x800. Color, depth, normals, and motion
are written out each frame. If an NVIDIA NGX SDK and `nvngx_dlss.dll`
are present, the 3D view is upscaled through DLSS; otherwise nearest.

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.
Original game code: id Software, 1993-1996.

Carmack's old note is in README.TXT.

## Build

    cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64
    cmake --build build-win --config Release

Optional NGX headers/libs (not required to compile):

    cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64 -DNGX_SDK_DIR=C:\path\to\ngx

Use whatever VS generator you have (17 2022, 18 2026, ...).

## Run

Put an IWAD in `wads/` (`freedoom2.wad`, `doom2.wad`, etc.), then:

    play-windoom.cmd

The script starts `windoom.exe` from its own folder so `nvngx_dlss.dll`
next to the exe can load. Or run `build-win\Release\windoom.exe` with
`DOOMWADDIR` pointing at `wads\`.

    play-windoom.cmd -nodlss

forces nearest even when NGX is built in.

## DLSS5-Swapper

Add the folder that contains `windoom.exe` (usually `build-win\Release`).
The exe is DirectX 12. Copy `nvngx_dlss.dll` there yourself (not in git).
Swapper should offer **Native**. Do not put your own `dxgi.dll` in that
folder. Details: `win32/README-NGX.md`.

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
    win32/            Windows video, sound, G-buffers, NGX
    wads/             IWADs
    CMakeLists.txt    `windoom` target

Buffer formats: `win32/README-GBUFFER.md`.
