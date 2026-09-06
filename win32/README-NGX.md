# NGX / DLSS

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.

WinDoom is GPLv2. NVIDIA DLSS / NGX is not in this tree. Default
build is nearest 4x. NGX is local only: `third_party/ngx/` (gitignored)
and `nvngx_dlss.dll` (plus `nvngx_dlssd.dll` when present) next to the exe.

One NGX exe (`WINDOOM_NGX=ON`), four staged folders. The folder's
`ngx.mode` file picks the path:

| Folder | `ngx.mode` | Meaning |
|---|---|---|
| `windoom-ngx-dlss3.5` | `rr` | Ray Reconstruction (`CREATE_DLSSD_EXT`) |
| `windoom-ngx-dlss4` | `k` | Super Resolution, preset **K** |
| `windoom-ngx-dlss4.5` | `l` | Super Resolution, preset **L** |
| `windoom-ngx-dlss5` | `dlss5` | Super Resolution (SDK default / L) |

Command-line overrides: `-ngx-rr`, `-ngx-k`, `-ngx-l`.

## Build

    build.cmd                   nearest, no NVIDIA
    build.cmd --ngx-dlss3.5
    build.cmd --ngx-dlss4
    build.cmd --ngx-dlss4.5
    build.cmd --ngx-dlss5
    build.cmd --all

`--ngx-dlss*` runs `fetch-ngx.cmd` (NVIDIA/DLSS tag v310.7.0 into
`third_party/ngx/`) then cmake with `-DWINDOOM_NGX=ON`. A plain
`build.cmd` always passes `-DWINDOOM_NGX=OFF`, even if that folder
already exists. `--all` stages original + Anime4K + FSR2 + all four NGX
folders.

Manual:

    fetch-ngx.cmd
    cmake -S . -B build-win -A x64 -DWINDOOM_NGX=ON
    cmake --build build-win --config Release

Need `include/nvsdk_ngx.h` and `nvsdk_ngx_d.lib` (or `_s` / `nvsdk_ngx`).
`WINDOOM_NGX=ON` without a SDK is a configure error.

    play-windoom.cmd --ngx-dlss4 -nodlss

skips NGX at runtime.

Input 320x200, output 1280x800, jitter 0. HUD (depth == 0) is
nearest-blitted after evaluate.

## Presets 4 and 4.5

Before `CREATE_DLSS_EXT`, every quality slot gets the same hint
(`UltraPerformance`, `Quality`, `Balanced`, `Performance`, `DLAA`):

- mode `k` → `NVSDK_NGX_DLSS_Hint_Render_Preset_K` (transformer DLSS 4)
- mode `l` → `NVSDK_NGX_DLSS_Hint_Render_Preset_L` (default Ultra Perf / 4.5)

## Ray Reconstruction (3.5)

DOOM is a software renderer: there is no path-traced specular. RR still
runs with what we have:

- color, depth, motion (same as SR)
- normals from `g_tex_normal`
- diffuse albedo = a copy of color
- specular albedo = black 320×200
- roughness = constant ~1 (fully matte, unpacked)

If create or evaluate fails, the log says so and the exe falls back to
SR preset K so the folder is not dead.

## Demo keys

F1–F4 still switch color / depth / normals / velocity. The same views
can be selected from the command line after `GB_Init`:

    play-windoom.cmd --fsr2 -playdemo compare -depth
    play-windoom.cmd --ngx-dlss4 -playdemo compare -normal
    play-windoom.cmd --ngx-dlss4.5 -playdemo compare -velocity
    play-windoom.cmd --ngx-dlss3.5 -playdemo compare -color

NGX / FSR2 / Anime4K only run on the color view. Debug views use nearest
compose so a demo can be recorded in an aux buffer.

## Swapper Native (`windoom-ngx-dlss5`)

1. `fetch-ngx.cmd` and a Release build with NGX found (`WINDOOM_HAS_NGX`).
2. `nvngx_dlss.dll` beside `windoom.exe` in `windoom-ngx-dlss5`.
3. DLSS5-Swapper: Add folder → `build-win\Release\windoom-ngx-dlss5`.
4. Expect DirectX 12, 64-bit, **Native**.
5. Install Native. Do not drop your own `dxgi.dll` in that folder first.

Feeder still works if you pick it. Native is the intended route.
If `renodx-dlss5.addon64` sits next to the exe, this folder uses hi-res
DLAA evaluate (HUD is blitted after NR).
