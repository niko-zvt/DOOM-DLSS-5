# FSR 2

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.

WinDoom can present the 320x200 framebuffer through
[AMD FidelityFX Super Resolution 2.2.1](https://github.com/GPUOpen-Effects/FidelityFX-FSR2)
(MIT). This is a separate binary from NGX and Anime4K. Do not enable
more than one present owner in one exe.

The new FidelityFX-SDK loader / FSR 3 DLL path is not used. WinDoom
links the standalone 2.2.1 C API and DX12 backend.

## Build / run

    .\build.cmd --fsr2
    .\play-windoom.cmd --fsr2

`build.cmd --fsr2` runs `fetch-fsr2.cmd` (GPUOpen-Effects/FidelityFX-FSR2
tag v2.2.1 into gitignored `third_party/fsr2/`) then cmake with
`-DWINDOOM_FSR2=ON`. Output folder: `build-win\Release\windoom-fsr2\`.

    .\play-windoom.cmd --fsr2 -nofsr2

skips FSR2 at runtime (nearest fallback).

## Pipeline

Same G-buffer contract as NGX: color RGBA8, linear view-Z, RG32F
motion at 320x200. FSR2 reconstructs 1280x800. The status bar is
nearest-blitted after evaluate.

Halton jitter is applied to the software camera for the 3D view only
(`viewangle` / `centeryfrac`) and restored before `GB_EndFrame`, so
motion vectors stay unjittered.

Reactive mask and RCAS sharpening are off in this first path.

## Sources

Fetched locally, not committed. License: MIT, AMD, 2023.
See `third_party/fsr2/ffx-fsr2-api/` after `fetch-fsr2.cmd`.
