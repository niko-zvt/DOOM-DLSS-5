/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "ngx_dlss.h"

#include <stdio.h>

#include "m_argv.h"

#ifdef WINDOOM_HAS_NGX
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <wchar.h>
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_helpers.h>
#endif

int Ngx_Wanted(void)
{
    return M_CheckParm("-nodlss") == 0;
}

#ifdef WINDOOM_HAS_NGX

static int g_ready;
static NVSDK_NGX_Handle *g_handle;
static NVSDK_NGX_Parameter *g_params;
static ID3D12Device *g_dev;

int Ngx_Init(void *device, void *queue)
{
    NVSDK_NGX_Result r;
    wchar_t path[MAX_PATH];

    (void)queue;
    g_ready = 0;
    g_handle = NULL;
    g_params = NULL;
    g_dev = (ID3D12Device *)device;
    if (!g_dev)
	return 0;

    GetModuleFileNameW(NULL, path, MAX_PATH);
    {
	wchar_t *slash = wcsrchr(path, L'\\');
	if (slash)
	    *slash = 0;
    }

    r = NVSDK_NGX_D3D12_Init(0x57444f4dull, path, g_dev, NULL,
			     NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(r))
    {
	fprintf(stderr, "NGX: init failed (0x%08x), using nearest\n",
		(unsigned)r);
	return 0;
    }

    r = NVSDK_NGX_D3D12_GetCapabilityParameters(&g_params);
    if (NVSDK_NGX_FAILED(r) || !g_params)
    {
	fprintf(stderr, "NGX: no capability parameters, using nearest\n");
	NVSDK_NGX_D3D12_Shutdown1(g_dev);
	return 0;
    }

    fprintf(stderr, "NGX: runtime ready (feature created on first evaluate)\n");
    g_ready = 1;
    return 1;
}

void Ngx_Shutdown(void)
{
    if (g_handle && g_params)
	NVSDK_NGX_D3D12_ReleaseFeature(g_handle);
    g_handle = NULL;
    if (g_params)
	NVSDK_NGX_D3D12_DestroyParameters(g_params);
    g_params = NULL;
    if (g_dev)
	NVSDK_NGX_D3D12_Shutdown1(g_dev);
    g_dev = NULL;
    g_ready = 0;
}

int Ngx_Ready(void)
{
    return g_ready;
}

static int ngx_ensure_feature(ID3D12GraphicsCommandList *cl)
{
    NVSDK_NGX_Result r;
    int flags;

    if (g_handle)
	return 1;
    if (!g_params || !cl)
	return 0;

    flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
	    NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_Parameter_Width, 320);
    NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_Parameter_Height, 200);
    NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_Parameter_OutWidth, 1280);
    NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_Parameter_OutHeight, 800);
    NVSDK_NGX_Parameter_SetI(g_params, NVSDK_NGX_Parameter_PerfQualityValue,
			     NVSDK_NGX_PerfQuality_Value_UltraPerformance);
    NVSDK_NGX_Parameter_SetI(g_params, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
			     flags);

    r = NVSDK_NGX_D3D12_CreateFeature(cl, NVSDK_NGX_Feature_SuperSampling,
				      g_params, &g_handle);
    if (NVSDK_NGX_FAILED(r) || !g_handle)
    {
	fprintf(stderr, "NGX: CreateFeature failed (0x%08x)\n", (unsigned)r);
	g_handle = NULL;
	g_ready = 0;
	return 0;
    }
    return 1;
}

int Ngx_Evaluate(void *cmdlist, void *color, void *depth, void *velocity,
		 void *output, int reset)
{
    ID3D12GraphicsCommandList *cl = (ID3D12GraphicsCommandList *)cmdlist;
    NVSDK_NGX_Result r;

    if (!g_ready || !cl || !color || !depth || !velocity || !output)
	return 0;
    if (!ngx_ensure_feature(cl))
	return 0;

    NVSDK_NGX_Parameter_SetD3d12Resource(g_params, NVSDK_NGX_Parameter_Color,
					 (ID3D12Resource *)color);
    NVSDK_NGX_Parameter_SetD3d12Resource(g_params, NVSDK_NGX_Parameter_Depth,
					 (ID3D12Resource *)depth);
    NVSDK_NGX_Parameter_SetD3d12Resource(g_params, NVSDK_NGX_Parameter_MotionVectors,
					 (ID3D12Resource *)velocity);
    NVSDK_NGX_Parameter_SetD3d12Resource(g_params, NVSDK_NGX_Parameter_Output,
					 (ID3D12Resource *)output);
    NVSDK_NGX_Parameter_SetF(g_params, NVSDK_NGX_Parameter_Jitter_Offset_X, 0.0f);
    NVSDK_NGX_Parameter_SetF(g_params, NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
    NVSDK_NGX_Parameter_SetF(g_params, NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
    NVSDK_NGX_Parameter_SetF(g_params, NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
    NVSDK_NGX_Parameter_SetI(g_params, NVSDK_NGX_Parameter_Reset, reset ? 1 : 0);
    NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, 320);
    NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, 200);

    r = NVSDK_NGX_D3D12_EvaluateFeature_C(cl, g_handle, g_params, NULL);
    if (NVSDK_NGX_FAILED(r))
    {
	fprintf(stderr, "NGX: Evaluate failed (0x%08x)\n", (unsigned)r);
	return 0;
    }
    return 1;
}

#else /* !WINDOOM_HAS_NGX */

int Ngx_Init(void *device, void *queue)
{
    (void)device;
    (void)queue;
    fprintf(stderr, "NGX: built without SDK, nearest fallback\n");
    return 0;
}

void Ngx_Shutdown(void)
{
}

int Ngx_Ready(void)
{
    return 0;
}

int Ngx_Evaluate(void *cmdlist, void *color, void *depth, void *velocity,
		 void *output, int reset)
{
    (void)cmdlist;
    (void)color;
    (void)depth;
    (void)velocity;
    (void)output;
    (void)reset;
    return 0;
}

#endif
