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

#ifdef __cplusplus
}
#endif

#endif
