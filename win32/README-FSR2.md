# FSR 2

Optional present path: 320x200 → 1280x800 through
[AMD FidelityFX Super Resolution 2.2.1](https://github.com/GPUOpen-Effects/FidelityFX-FSR2)
(MIT). Lives in its own folder (`windoom-fsr2`), not mixed with NGX
or Anime4K.

    .\build.cmd --fsr2
    .\play-windoom.cmd --fsr2
    .\play-windoom.cmd --fsr2 -nofsr2

`--fsr2` fetches GPUOpen-Effects/FidelityFX-FSR2 v2.2.1 into
gitignored `third_party/fsr2/` and builds with `-DWINDOOM_FSR2=ON`.
`-nofsr2` is nearest fallback.

Same G-buffer as NGX (color, linear view-Z, 320x200 motion). The
status bar is copied on after FSR2. Halton jitter is applied to the
3D camera only and restored before motion is computed. Reactive mask
and RCAS are off.

License: MIT, AMD. Sources appear under `third_party/fsr2/` after
`fetch-fsr2.cmd`. The newer FidelityFX-SDK / FSR 3 loader is not used.
