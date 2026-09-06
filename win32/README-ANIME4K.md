# Anime4K Fast

Optional present path: 320x200 → 1280x800 through
[Anime4K](https://github.com/bloc97/Anime4K) Mode C Fast (MIT, bloc97).
Lives in its own folder (`windoom-anime4k`).

    .\build.cmd --anime4k
    .\play-windoom.cmd --anime4k

CMake: `-DWINDOOM_ANIME4K=ON`. If the compute shaders fail to compile,
present falls back to nearest.

Mode C Fast (clean upscale, no “restore”): clamp highlights → CNN
denoise×2 to 640x400 → CNN×2 to 1280x800 → clamp again. The extra
official downscale passes are skipped because 320×4 is already 1280.
The status bar is copied on after the CNN, same as NGX.

Weights come from official mpv GLSL (`third_party/anime4k/glsl/`) via
`scripts/convert-anime4k.py` → `win32/shaders/anime4k/`. License:
`third_party/anime4k/LICENSE`. Magpie HLSL is not used (GPL-3).
