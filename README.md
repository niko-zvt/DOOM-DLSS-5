# WinDoom

Windows x64 port of Linux DOOM 1.10. Same 320x200 software renderer,
shown in a D3D12 window at 1280x800. Color, depth, normals, and motion
are written out each frame.

Present folders under `build-win\Release\`:

- `windoom-original` — nearest 4x, no NVIDIA
- `windoom-ngx-dlss3.5` — Ray Reconstruction (`--ngx-dlss3.5`)
- `windoom-ngx-dlss4` — Super Resolution, preset K (`--ngx-dlss4`)
- `windoom-ngx-dlss4.5` — Super Resolution, preset L (`--ngx-dlss4.5`)
- `windoom-ngx-dlss5` — same SR; DLSS5-Swapper Native adds NR
- `windoom-anime4k` — Anime4K Fast Mode C (`--anime4k`)
- `windoom-fsr2` — AMD FSR 2.2 320→1280 (`--fsr2`)

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.
Original game code: id Software, 1993-1996.

Carmack's old note is in README.TXT.

## Build

    .\build.cmd

Nearest only (`windoom-original`). In PowerShell use `.\`
(cmd.exe can run `build.cmd` as-is).

    .\build.cmd --ngx-dlss3.5
    .\build.cmd --ngx-dlss4
    .\build.cmd --ngx-dlss4.5
    .\build.cmd --ngx-dlss5
    .\build.cmd --anime4k
    .\build.cmd --fsr2
    .\build.cmd --all

Flags can be combined. `--ngx-dlss*` fetches NVIDIA/DLSS into gitignored
`third_party/ngx/` and copies `nvngx_dlss.dll` (and `nvngx_dlssd.dll` if
present) into the four NGX folders. `--all` stages original + Anime4K +
FSR2 + all four NGX folders. `--fsr2` fetches FidelityFX-FSR2 v2.2.1
into gitignored `third_party/fsr2/`.
Details: `win32/README-NGX.md`, `win32/README-ANIME4K.md`,
`win32/README-FSR2.md`.

Or cmake by hand:

    cmake -S . -B build-win -A x64 -DWINDOOM_NGX=OFF -DWINDOOM_ANIME4K=OFF -DWINDOOM_FSR2=OFF
    cmake --build build-win --config Release

## Run

Put an IWAD in `wads/` (`freedoom2.wad`, `doom2.wad`, etc.), then:

    .\play-windoom.cmd
    .\play-windoom.cmd --ngx-dlss4
    .\play-windoom.cmd --ngx-dlss4.5
    .\play-windoom.cmd --ngx-dlss3.5
    .\play-windoom.cmd --ngx-dlss5
    .\play-windoom.cmd --anime4k
    .\play-windoom.cmd --fsr2

Each flag starts `windoom.exe` from its folder so local DLLs and
shaders load. Extra game args (`-playdemo`, `-nodlss`, `-nofsr2`,
`-depth`, `-normal`, `-velocity`, `-color`) are forwarded.

    .\play-windoom.cmd --ngx-dlss4 -nodlss

forces nearest even when NGX is built in.

    .\play-windoom.cmd --ngx-dlss4.5 -playdemo compare -depth

records / plays a demo on the depth debug view (nearest compose).
F1–F4 still switch the same views at runtime.

    .\screencast-windoom.cmd

runs that queue for a desktop recording: all present modes in
`-color`, then `--original` in `-depth` / `-normal` / `-velocity`.
Needs `.\build.cmd --all` and `compare.lmp` in `build-win\Release`.

## DLSS5-Swapper

Add `build-win\Release\windoom-ngx-dlss5` (not the Release root).
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
    Insert        show / hide status bar
    -color / -depth / -normal / -velocity
                  same views from the command line (for demos)

## Tree

    linuxdoom-1.10/   original sources
    win32/            Windows video, sound, G-buffers, NGX, Anime4K, FSR2
    wads/             IWADs
    CMakeLists.txt    `windoom` target

Buffer formats: `win32/README-GBUFFER.md`.
