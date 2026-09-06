/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#ifndef WIN32_GBUFFER_H
#define WIN32_GBUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    GB_KIND_WALL = 0,
    GB_KIND_FLOOR,
    GB_KIND_CEILING,
    GB_KIND_SPRITE,
    GB_KIND_SKY
};

enum
{
    GB_VIEW_COLOR = 0,
    GB_VIEW_DEPTH,
    GB_VIEW_NORMAL,
    GB_VIEW_VELOCITY
};

#define GB_WIDTH   320
#define GB_HEIGHT  200

void GB_Init(void);
void GB_Shutdown(void);

void GB_BeginFrame(void);
void GB_SetColumn(int x, float z, float nx, float ny, float nz, int kind);
void GB_SetObjectMotion(float du, float dv);
void GB_WriteColumn(int x, int yl, int yh);
void GB_WriteSpan(int y, int x1, int x2, float z, float nx, float ny, float nz);
void GB_RequestReset(void);
int  GB_ConsumeReset(void);
void GB_EndFrame(void);

void GB_SetPaletteRGB(const unsigned char *rgb768);
void GB_ConvertColor(const unsigned char *src8);

void GB_SetDebugView(int view);
int  GB_GetDebugView(void);

void GB_ToggleHud(void);
int  GB_HudVisible(void);

const unsigned char *GB_ColorRGBA(void);
const float         *GB_Depth(void);
const unsigned char *GB_NormalRGBA(void);
const float         *GB_VelocityRG(void);

void GB_ComposePresent(unsigned char *dst_bgra, int dst_w, int dst_h);
int  GB_HasScenePixels(void);
void GB_OverlayHud(unsigned char *dst_bgra, int dst_w, int dst_h);

#ifdef __cplusplus
}
#endif

#endif
