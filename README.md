# WinDoom

Windows x64 port of Linux DOOM 1.10. Same 320x200 software renderer,
shown in a D3D12 window at 1280x800. Color, depth, normals, and motion
are written out each frame.

Four present folders under `build-win\Release\`:

- `windoom-original` — nearest 4x, no NVIDIA
- `windoom-dlss` — NGX upscale 320→1280 (`--dlss-on`)
- `windoom-dlss5` — same NGX exe; DLSS5-Swapper Native adds NR
- `windoom-anime4k` — Anime4K Fast Mode C (`--anime4k`)

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.
Original game code: id Software, 1993-1996.

Carmack's old note is in README.TXT.

## Build

    .\build.cmd

Nearest only (`windoom-original`). In PowerShell use `.\`
(cmd.exe can run `build.cmd` as-is).

    .\build.cmd --dlss-on
    .\build.cmd --dlss5
    .\build.cmd --anime4k

Flags can be combined. `--dlss-on` fetches NVIDIA/DLSS into gitignored
`third_party/ngx/` and copies `nvngx_dlss.dll` into the DLSS folders.
Details: `win32/README-NGX.md`, `win32/README-ANIME4K.md`.

Or cmake by hand:

    cmake -S . -B build-win -A x64 -DWINDOOM_NGX=OFF -DWINDOOM_ANIME4K=OFF
    cmake --build build-win --config Release

## Run

Put an IWAD in `wads/` (`freedoom2.wad`, `doom2.wad`, etc.), then:

    .\play-windoom.cmd
    .\play-windoom.cmd --dlss-on
    .\play-windoom.cmd --dlss5
    .\play-windoom.cmd --anime4k

Each flag starts `windoom.exe` from its folder so local DLLs and
shaders load. Extra game args (`-playdemo`, `-nodlss`) are forwarded.

    .\play-windoom.cmd --dlss-on -nodlss

forces nearest even when NGX is built in.

## DLSS5-Swapper

Add `build-win\Release\windoom-dlss5` (not the Release root).
The exe is DirectX 12. Swapper should offer **Native**.
Do not put your own `dxgi.dll` in that folder first.
Details: `win32/README-NGX.md`.

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
    win32/            Windows video, sound, G-buffers, NGX, Anime4K
    wads/             IWADs
    CMakeLists.txt    `windoom` target

Buffer formats: `win32/README-GBUFFER.md`.
