/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "png_export.h"

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

static IWICImagingFactory *g_factory;
static int g_com_inited;
static int g_failed;

static int png_ensure_factory(void)
{
    HRESULT hr;

    if (g_factory)
	return 1;
    if (g_failed)
	return 0;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr))
	g_com_inited = 1;
    else if (hr != RPC_E_CHANGED_MODE)
    {
	g_failed = 1;
	return 0;
    }

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
			  CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
			  (void **)&g_factory);
    if (FAILED(hr) || !g_factory)
    {
	g_factory = NULL;
	g_failed = 1;
	return 0;
    }
    return 1;
}

int Png_Write(const char *path, int w, int h, const unsigned char *bgr24)
{
    IWICStream *stream = NULL;
    IWICBitmapEncoder *enc = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    wchar_t wpath[MAX_PATH];
    HRESULT hr;
    int ok = 0;

    if (!path || !bgr24 || w <= 0 || h <= 0)
	return 0;
    if (!png_ensure_factory())
	return 0;
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH))
	return 0;

    hr = IWICImagingFactory_CreateStream(g_factory, &stream);
    if (FAILED(hr))
	goto done;
    hr = IWICStream_InitializeFromFilename(stream, wpath, GENERIC_WRITE);
    if (FAILED(hr))
	goto done;
    hr = IWICImagingFactory_CreateEncoder(g_factory, &GUID_ContainerFormatPng,
					  NULL, &enc);
    if (FAILED(hr))
	goto done;
    hr = IWICBitmapEncoder_Initialize(enc, (IStream *)stream,
				      WICBitmapEncoderNoCache);
    if (FAILED(hr))
	goto done;
    hr = IWICBitmapEncoder_CreateNewFrame(enc, &frame, NULL);
    if (FAILED(hr))
	goto done;
    hr = IWICBitmapFrameEncode_Initialize(frame, NULL);
    if (FAILED(hr))
	goto done;
    hr = IWICBitmapFrameEncode_SetSize(frame, (UINT)w, (UINT)h);
    if (FAILED(hr))
	goto done;
    hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &fmt);
    if (FAILED(hr))
	goto done;
    if (!IsEqualGUID(&fmt, &GUID_WICPixelFormat24bppBGR))
	goto done;
    hr = IWICBitmapFrameEncode_WritePixels(frame, (UINT)h, (UINT)(w * 3),
					   (UINT)(w * 3 * h),
					   (BYTE *)bgr24);
    if (FAILED(hr))
	goto done;
    hr = IWICBitmapFrameEncode_Commit(frame);
    if (FAILED(hr))
	goto done;
    hr = IWICBitmapEncoder_Commit(enc);
    if (FAILED(hr))
	goto done;
    ok = 1;

done:
    if (frame)
	IWICBitmapFrameEncode_Release(frame);
    if (enc)
	IWICBitmapEncoder_Release(enc);
    if (stream)
	IWICStream_Release(stream);
    return ok;
}

void Png_Shutdown(void)
{
    if (g_factory)
    {
	IWICImagingFactory_Release(g_factory);
	g_factory = NULL;
    }
    if (g_com_inited)
    {
	CoUninitialize();
	g_com_inited = 0;
    }
    g_failed = 0;
}
