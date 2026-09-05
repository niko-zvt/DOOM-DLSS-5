# G-buffer notes

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.

The game still draws 320x200. D3D11 presents that at 1280x800 (nearest,
integer 4x). Do not raise SCREENWIDTH/HEIGHT; the HUD and menus are
hard-wired to 320x200.

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
    float32, map units. 1.0 is one map unit.
    Walls:    projection / rw_scale
    Flats:    planeheight * yslope
    Sprites:  projection / spryscale
    Sky / 2D: 0 or 8192 (far)

Normal
    RGBA8, world space, Y up. Stored as n * 0.5 + 0.5.
    Wall:    (cos(rw_normalangle), 0, sin(...))
    Floor:   (0, 1, 0)
    Ceiling: (0, -1, 0)
    Sprite:  faces the camera
    Sky:     (0, 1, 0)

Velocity
    RG32F, in 320x200 pixels: mv = uv_now - uv_prev.
    Reprojected from depth and the last viewx/viewy/viewz/viewangle.
    Not optical flow. Menu / TITLEPIC stay 0.

Map axes: +X east, +Y north, +Z up. viewangle is BAM; 0 looks at +X.

## D3D11 names

Created in I_InitGraphics, updated every frame:

    GB_Color     R8G8B8A8_UNORM
    GB_Depth     R32_FLOAT
    GB_Normal    R8G8B8A8_UNORM
    GB_Velocity  R32G32_FLOAT   (pixel delta at 320x200)

Swapchain is B8G8R8A8_UNORM, 1280x800, windowed. Present shows whichever
debug view is selected.

Not done here:

    - per-mobj velocity (camera only; monsters would subtract a tick of
      momx/momy)
    - frame-to-frame optical flow on the paletted image (too noisy)
