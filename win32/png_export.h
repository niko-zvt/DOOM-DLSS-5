/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#ifndef WIN32_PNG_EXPORT_H
#define WIN32_PNG_EXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Writes a 24bpp BGR image (w*3 bytes per row, top-down) as PNG through
   Windows Imaging Component. Returns 1 on success, 0 on failure. */
int  Png_Write(const char *path, int w, int h, const unsigned char *bgr24);
void Png_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
