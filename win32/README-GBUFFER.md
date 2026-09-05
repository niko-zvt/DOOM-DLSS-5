# G-buffer notes

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.

The game still draws 320x200. Present is 1280x800 (nearest integer 4x
until NGX is wired). Do not raise SCREENWIDTH/HEIGHT; the HUD and menus
are hard-wired to 320x200.

## Debug views

    F1  color
    F2  depth
    F3  normals
    F4  velocity

Handled in the Win32 window. They never reach the DOOM menu.

## CPU buffers (320x200)

Origin is top-left. +X right, +Y down (DOOM screen). Written in
`win32/gbuffer.c`.

Color
    RGBA8. PLAYPAL after gamma. A = 255.

Depth
    float32 linear view-Z in map units. Farther is larger (not inverted).
    1.0 is one map unit.
    Walls:    projection / rw_scale
    Flats:    planeheight * yslope
    Sprites:  projection / spryscale
    Sky:      8192 (far)
    2D / HUD: 0

    NGX: not DepthInverted. Sky and HUD stay non-3D (0 / 8192).

Normal
    RGBA8, world space, Y up. Stored as n * 0.5 + 0.5.
    Wall:    (cos(rw_normalangle), 0, sin(...))
    Floor:   (0, 1, 0)
    Ceiling: (0, -1, 0)
    Sprite:  faces the camera
    Sky:     (0, 1, 0)

Velocity
    RG32F, NGX MVLowRes: pixel delta at 320x200.
    mv = uv_now - uv_prev.
    +X right, +Y down (same as the color buffer).
    Camera reprojection from depth and the last
    viewx/viewy/viewz/viewangle, plus per-sprite object motion
    when the renderer sets it.
    Not optical flow. Menu / TITLEPIC / sky stay 0.
    GB_RequestReset() zeros motion on the next frame (level load).

Map axes: +X east, +Y north, +Z up. viewangle is BAM; 0 looks at +X.

## GPU texture names

Created in I_InitGraphics, updated every frame:

    GB_Color     R8G8B8A8_UNORM
    GB_Depth     R32_FLOAT          (linear view-Z, not inverted)
    GB_Normal    R8G8B8A8_UNORM
    GB_Velocity  R32G32_FLOAT       (pixel delta at 320x200, +Y down)

Swapchain is B8G8R8A8_UNORM, 1280x800, windowed. Present shows whichever
debug view is selected.
