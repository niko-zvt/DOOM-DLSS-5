/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#ifndef WIN32_NGX_DLSS_H
#define WIN32_NGX_DLSS_H

#ifdef __cplusplus
extern "C" {
#endif

int  Ngx_Wanted(void);
int  Ngx_Init(void *device, void *queue);
void Ngx_Shutdown(void);
int  Ngx_Ready(void);
int  Ngx_WantsHiRes(void);
int  Ngx_ShowEvalOutput(void);
int  Ngx_Evaluate(void *cmdlist,
		  void *color, void *depth, void *velocity, void *normal,
		  void *output, int reset);
/* SR 320→1280 (preset L) then DLAA/NR on that color. depth_hi/vel_hi
   are nearest 4x; color for pass 2 is the SR output, never nearest. */
int  Ngx_EvaluateStack(void *cmdlist,
		       void *color, void *depth, void *velocity,
		       void *depth_hi, void *velocity_hi, void *normal,
		       void *output, int reset);

#ifdef __cplusplus
}
#endif

#endif
