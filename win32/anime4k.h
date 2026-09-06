/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#ifndef WIN32_ANIME4K_H
#define WIN32_ANIME4K_H

#ifdef __cplusplus
extern "C" {
#endif

int  Anime4K_Init(void *device);
void Anime4K_Shutdown(void);
int  Anime4K_Ready(void);
int  Anime4K_Evaluate(void *cmdlist, void *color, void *output);

#ifdef __cplusplus
}
#endif

#endif
