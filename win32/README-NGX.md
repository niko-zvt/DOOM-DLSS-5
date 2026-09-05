# NGX / DLSS5-Swapper

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.

WinDoom is GPLv2. NVIDIA DLSS / NGX is not in this tree. Default
build is nearest 4x. NGX is local only: `third_party/ngx/` (gitignored)
and `nvngx_dlss.dll` next to the exe.

## Build

    .\build.cmd              nearest, no NVIDIA
    .\build.cmd --dlss-on    fetch SDK, link NGX, copy the DLL
    .\play-windoom.cmd

`--dlss-on` runs `fetch-ngx.cmd` (NVIDIA/DLSS tag v310.7.0 into
`third_party/ngx/`) then cmake with `-DWINDOOM_NGX=ON`. A plain
`build.cmd` always passes `-DWINDOOM_NGX=OFF`, even if that folder
already exists.

Manual:

    .\fetch-ngx.cmd
    cmake -S . -B build-win -A x64 -DWINDOOM_NGX=ON
    cmake --build build-win --config Release

Need `include/nvsdk_ngx.h`, `include/nvsdk_ngx_helpers.h`, and
`nvsdk_ngx_d.lib` (or `_s` / `nvsdk_ngx`). `WINDOOM_NGX=ON` without a
SDK is a configure error.

    .\play-windoom.cmd -nodlss

skips NGX at runtime.

## Runtime

Init: capability check, log `NGX_DLSS_GET_OPTIMAL_SETTINGS` for
1280x800 UltraPerformance, then `NGX_D3D12_CREATE_DLSS_EXT` on a
oneshot command list. Input stays 320x200.

Evaluate: `NGX_D3D12_EVALUATE_DLSS_EXT`. Color, linear view-Z depth,
MVLowRes motion, jitter 0, AutoExposure. Output 1280x800.
HUD (depth == 0) is nearest-blitted after evaluate.

## Swapper Native

1. `fetch-ngx.cmd` and a Release build with NGX found (`WINDOOM_HAS_NGX`).
2. `nvngx_dlss.dll` beside `build-win\Release\windoom.exe`.
3. DLSS5-Swapper: Add folder → `build-win\Release`.
4. Expect DirectX 12, 64-bit, **Native**.
5. Install Native. Do not drop your own `dxgi.dll` in that folder first.

Feeder still works if you pick it. Native is the intended route.
