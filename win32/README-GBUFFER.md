# G-buffer

The game still draws 320x200. The window is 1280x800. Do not raise
`SCREENWIDTH` / `SCREENHEIGHT`: HUD and menus are hard-wired to 320x200.

Debug views (Win32 window only, not the DOOM menu):

    F1  color
    F2  depth
    F3  normals
    F4  velocity

## CPU buffers (320x200)

Origin top-left, +X right, +Y down. Written in `win32/gbuffer.c` at
the same `columnofs` / `ylookup` address as `screens[0]` (the bezel
hole). Bezel pixels clamp from that rect. Insert hides the status
bar; then the pad also covers y=168..199 so the upscaler does not
see the HUD.

Color — RGBA8, PLAYPAL after gamma, A=255.

Depth — float32 linear view-Z in map units (farther is larger, not
inverted). Walls: `projection / rw_scale`. Flats: `planeheight * yslope`.
Sprites: `projection / spryscale`. Sky: 8192. 2D / HUD: 0.
NGX / FSR2: not DepthInverted.

Normal — RGBA8, world space, Y up, stored as `n * 0.5 + 0.5`.
Walls use `rw_normalangle`. Floor `(0,1,0)`, ceiling `(0,-1,0)`.
Sprites face the camera. Sky `(0,1,0)`.

Velocity — RG32F, pixel delta at 320x200 for NGX `MVLowRes`.
`mv = prev - cur` (+X right, +Y down). Camera reprojection from
depth and the last view pose, plus per-sprite motion when the
renderer sets it. Not optical flow. Menu / TITLEPIC / sky / HUD
stay 0. `GB_RequestReset()` zeros motion on the next frame.

Map: +X east, +Y north, +Z up. `viewangle` is BAM; 0 looks at +X.

## GPU

Created in `I_InitGraphics`, updated every frame:

    GB_Color     R8G8B8A8_UNORM
    GB_Depth     R32_FLOAT
    GB_Normal    R8G8B8A8_UNORM
    GB_Velocity  R32G32_FLOAT

Swapchain is B8G8R8A8, 1280x800, windowed.
