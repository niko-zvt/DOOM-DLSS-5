# Anime4K Fast

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.

WinDoom can present the 320x200 framebuffer through
[Anime4K](https://github.com/bloc97/Anime4K) Mode C Fast (MIT, bloc97).
This is a separate binary from NGX. Do not enable both in one exe.

## Build / run

    .\build.cmd --anime4k
    .\play-windoom.cmd --anime4k

CMake: `-DWINDOOM_ANIME4K=ON -DWINDOOM_NGX=OFF`. Output folder:
`build-win\Release\windoom-anime4k\`.

## Pipeline

Official lower-end **Mode C Fast**:

1. `Clamp_Highlights` statistics on 320x200
2. `Upscale_Denoise_CNN_x2_M` → 640x400
3. `Upscale_CNN_x2_S` → 1280x800
4. Clamp highlights on the 1280 output (BT.709 luma)

`AutoDownscalePre_x2/x4` are omitted: 320×4 is exactly 1280x800.

Mode C is the “clean image / no degradation” path. DOOM’s software
frame is not 1080p anime, so Mode A/B Restore is not used.

The status bar is nearest-blitted from `screens[0]` after the CNN,
same as the NGX present path.

## Sources

Weights are ported from official mpv GLSL (`third_party/anime4k/glsl/`)
to HLSL compute (`win32/shaders/anime4k/mode_c_fast.hlsl`) by
`scripts/convert-anime4k.py`. License: `third_party/anime4k/LICENSE`.

HLSL from [Magpie](https://github.com/Blinue/Magpie) is **not** used
(GPL-3; cannot mix into this GPLv2 tree).

If PSO compile fails at startup, present falls back to nearest.
