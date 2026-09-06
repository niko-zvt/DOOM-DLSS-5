/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#ifndef WIN32_FSR2_H
#define WIN32_FSR2_H

#ifdef __cplusplus
extern "C" {
#endif

int  Fsr2_Wanted(void);
int  Fsr2_Init(void *device);
void Fsr2_Shutdown(void);
int  Fsr2_Ready(void);
void Fsr2_ApplyRasterJitter(void);
void Fsr2_RestoreCamera(void);
int  Fsr2_Evaluate(void *cmdlist,
		   void *color, void *depth, void *velocity, void *output,
		   int reset);

#ifdef __cplusplus
}
#endif

#endif
