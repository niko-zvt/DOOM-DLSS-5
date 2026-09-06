# NGX / DLSS

WinDoom is GPLv2. NVIDIA DLSS is **not** in this repo. `.\build.cmd
--ngx-dlss4` (or 3.5 / 4.5 / 5 / `--all`) downloads the SDK into
gitignored `third_party/ngx/` and copies `nvngx_dlss.dll` next to
the exe. A plain `.\build.cmd` is nearest only, even if that folder
already exists.

One NGX binary, four folders. A one-line `ngx.mode` file picks the path:

| Folder | `ngx.mode` | Path |
|---|---|---|
| `windoom-ngx-dlss3.5` | `rr` | Ray Reconstruction |
| `windoom-ngx-dlss4` | `k` | Super Resolution, preset K |
| `windoom-ngx-dlss4.5` | `l` | Super Resolution, preset L |
| `windoom-ngx-dlss5` | `dlss5` | Same SR as 4.5; then DLSS 5 NR if RenoDX is present |

Overrides (optional): `-ngx-rr`, `-ngx-k`, `-ngx-l`. Skip NGX:
`.\play-windoom.cmd --ngx-dlss4 -nodlss`.

Input is 320x200, output 1280x800, no jitter. The status bar is
copied on after DLSS so it stays sharp.

## 4 / 4.5 / 5

4 and 4.5 are one Super Resolution pass (320→1280). 5 does that
**same** 4.5 pass first (preset L). If `renodx-dlss5.addon64` sits
next to the exe, a second pass (DLAA) runs on that image so Swapper
NR can refine it. Color for that pass is never a nearest 4x stretch.
Without the addon, the 5 folder looks like 4.5.

## Ray Reconstruction (3.5)

DOOM has no path-traced lighting. RR still gets color, depth, motion,
and normals. Missing buffers are faked (black specular, rough=1).
If create/eval fails, the log says so and the exe falls back to
preset K.

## Swapper (Native)

1. Build with NGX (`.\build.cmd --ngx-dlss5`).
2. In DLSS5-Swapper add `build-win\Release\windoom-ngx-dlss5`.
3. DirectX 12, 64-bit, **Native**. Do not put your own `dxgi.dll`
   in that folder first.

Feeder also works. Native is the intended route.

## Manual cmake

    .\fetch-ngx.cmd
    cmake -S . -B build-win -A x64 -DWINDOOM_NGX=ON
    cmake --build build-win --config Release

Needs `include/nvsdk_ngx.h` and an `nvsdk_ngx*.lib`. `WINDOOM_NGX=ON`
without a SDK is a configure error. SDK tag: NVIDIA/DLSS v310.7.0.

Debug views (F1–F4 / `-depth` etc.) skip DLSS and use nearest so a
demo can record an aux buffer. See the root `README.md` for keys.
