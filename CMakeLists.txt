# WinDoom: Windows x64 build of Linux DOOM 1.10.
# Copyright (C) 2026 Nikolai Zhivotenko. Licensed under GPLv2 (LICENSE.TXT).
cmake_minimum_required(VERSION 3.16)
project(windoom C)

if(NOT WIN32)
  message(FATAL_ERROR "windoom is a Windows x64 target")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(DOOM_DIR ${CMAKE_SOURCE_DIR}/linuxdoom-1.10)

set(DOOM_SOURCES
  ${DOOM_DIR}/am_map.c
  ${DOOM_DIR}/d_items.c
  ${DOOM_DIR}/d_main.c
  ${DOOM_DIR}/d_net.c
  ${DOOM_DIR}/doomdef.c
  ${DOOM_DIR}/doomstat.c
  ${DOOM_DIR}/dstrings.c
  ${DOOM_DIR}/f_finale.c
  ${DOOM_DIR}/f_wipe.c
  ${DOOM_DIR}/g_game.c
  ${DOOM_DIR}/hu_lib.c
  ${DOOM_DIR}/hu_stuff.c
  ${DOOM_DIR}/info.c
  ${DOOM_DIR}/m_argv.c
  ${DOOM_DIR}/m_bbox.c
  ${DOOM_DIR}/m_cheat.c
  ${DOOM_DIR}/m_fixed.c
  ${DOOM_DIR}/m_menu.c
  ${DOOM_DIR}/m_misc.c
  ${DOOM_DIR}/m_random.c
  ${DOOM_DIR}/m_swap.c
  ${DOOM_DIR}/p_ceilng.c
  ${DOOM_DIR}/p_doors.c
  ${DOOM_DIR}/p_enemy.c
  ${DOOM_DIR}/p_floor.c
  ${DOOM_DIR}/p_inter.c
  ${DOOM_DIR}/p_lights.c
  ${DOOM_DIR}/p_map.c
  ${DOOM_DIR}/p_maputl.c
  ${DOOM_DIR}/p_mobj.c
  ${DOOM_DIR}/p_plats.c
  ${DOOM_DIR}/p_pspr.c
  ${DOOM_DIR}/p_saveg.c
  ${DOOM_DIR}/p_setup.c
  ${DOOM_DIR}/p_sight.c
  ${DOOM_DIR}/p_spec.c
  ${DOOM_DIR}/p_switch.c
  ${DOOM_DIR}/p_telept.c
  ${DOOM_DIR}/p_tick.c
  ${DOOM_DIR}/p_user.c
  ${DOOM_DIR}/r_bsp.c
  ${DOOM_DIR}/r_data.c
  ${DOOM_DIR}/r_draw.c
  ${DOOM_DIR}/r_main.c
  ${DOOM_DIR}/r_plane.c
  ${DOOM_DIR}/r_segs.c
  ${DOOM_DIR}/r_sky.c
  ${DOOM_DIR}/r_things.c
  ${DOOM_DIR}/s_sound.c
  ${DOOM_DIR}/sounds.c
  ${DOOM_DIR}/st_lib.c
  ${DOOM_DIR}/st_stuff.c
  ${DOOM_DIR}/tables.c
  ${DOOM_DIR}/v_video.c
  ${DOOM_DIR}/w_wad.c
  ${DOOM_DIR}/wi_stuff.c
  ${DOOM_DIR}/z_zone.c
)

add_executable(windoom
  ${DOOM_SOURCES}
  win32/gbuffer.c
  win32/i_main_win.c
  win32/i_net_win.c
  win32/i_sound_win.c
  win32/i_system_win.c
  win32/i_video_win.c
  win32/ngx_dlss.c
  win32/anime4k.c
)

target_include_directories(windoom PRIVATE
  ${DOOM_DIR}
  ${CMAKE_SOURCE_DIR}/win32
  ${CMAKE_SOURCE_DIR}/win32/compat
)

target_compile_definitions(windoom PRIVATE
  NORMALUNIX
  LINUX
  _CRT_SECURE_NO_WARNINGS
  NOMINMAX
)

if(MSVC)
  target_compile_options(windoom PRIVATE
    /W3
    /wd4244
    /wd4267
    /wd4018
    /wd4305
    /wd4996
    /wd4101
    /FIstrings.h
    /FIalloca.h
  )
endif()

target_link_libraries(windoom PRIVATE
  d3d12
  dxgi
  dxguid
  ole32
  uuid
)

option(WINDOOM_NGX "Link NVIDIA NGX (needs a local SDK; off by default)" OFF)
option(WINDOOM_ANIME4K "Compile Anime4K Fast present path (no NVIDIA)" OFF)
set(NGX_SDK_DIR "" CACHE PATH "NVIDIA NGX SDK root (headers and lib)")

if(WINDOOM_NGX AND WINDOOM_ANIME4K)
  message(FATAL_ERROR
    "WINDOOM_NGX and WINDOOM_ANIME4K cannot both be ON. "
    "They each own present. Build separate folders via build.cmd.")
endif()

if(WINDOOM_ANIME4K)
  message(STATUS "Anime4K Fast enabled")
  target_compile_definitions(windoom PRIVATE WINDOOM_HAS_ANIME4K)
  target_link_libraries(windoom PRIVATE d3dcompiler)
endif()

if(WINDOOM_NGX)
  if(NOT NGX_SDK_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/ngx/include/nvsdk_ngx.h")
    set(NGX_SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/ngx")
  endif()
  find_path(NGX_INCLUDE_DIR nvsdk_ngx.h
    HINTS "${NGX_SDK_DIR}"
    PATH_SUFFIXES include)
  find_library(NGX_LIBRARY NAMES nvsdk_ngx_d nvsdk_ngx_s nvsdk_ngx
    HINTS "${NGX_SDK_DIR}"
    PATH_SUFFIXES lib/Windows_x86_64/x64 lib/x64 lib)
  if(NGX_INCLUDE_DIR AND NGX_LIBRARY)
    message(STATUS "NGX enabled: ${NGX_INCLUDE_DIR}")
    target_compile_definitions(windoom PRIVATE WINDOOM_HAS_NGX)
    target_include_directories(windoom PRIVATE ${NGX_INCLUDE_DIR})
    target_link_libraries(windoom PRIVATE ${NGX_LIBRARY})
  else()
    message(FATAL_ERROR
      "WINDOOM_NGX=ON but the SDK was not found. Run fetch-ngx.cmd first "
      "or pass -DNGX_SDK_DIR=...")
  endif()
else()
  message(STATUS "NGX disabled. Use build.cmd --dlss-on / --anime4k.")
endif()
