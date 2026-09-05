# NGX / DLSS5-Swapper

Copyright (C) 2026 Nikolai Zhivotenko. GPLv2, see LICENSE.TXT.

WinDoom calls NVIDIA NGX itself (D3D12 Super Resolution). Swapper can
then replace `nvngx_dlss.dll`. The NVIDIA SDK and DLLs are **not** in
this repo.

## Local NGX files

1. Get the NVIDIA NGX / DLSS SDK (developer.nvidia.com).
2. Point CMake at it:

       cmake -S . -B build-win -A x64 -DNGX_SDK_DIR=C:\path\to\NGX_SDK

   Need `nvsdk_ngx.h` and `nvsdk_ngx_d.lib` (or `nvsdk_ngx_s` / `nvsdk_ngx`).
   Without this, the exe still builds and presents nearest.

3. Copy `nvngx_dlss.dll` next to `windoom.exe`
   (`build-win\Release\`). Git ignores `nvngx*.dll` and `third_party/ngx/`.

4. Run from that folder (`play-windoom.cmd` does). `-nodlss` skips NGX.

Input is 320x200. Output is 1280x800. Jitter is 0. HUD pixels
(depth == 0) are nearest-blitted after evaluate.

## Swapper Native

1. Build Release.
2. Put `nvngx_dlss.dll` beside the exe.
3. In DLSS5-Swapper: Add folder → `build-win\Release` (the exe dir).
4. Scanner should report DirectX 12, 64-bit, and Native available.
5. Install Native. Swapper replaces `nvngx_dlss.dll` and adds
   `nvngx_dlssnr.dll` / RenoDX. Do not drop a `dxgi.dll` of your own
   into that folder first.

Feeder still works if you pick it, but Native is the intended route.
