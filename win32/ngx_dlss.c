/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "ngx_dlss.h"

#include <stdio.h>

#include "m_argv.h"

#ifdef WINDOOM_HAS_NGX
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <wchar.h>
#include <string.h>
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

static int g_inited;
static int g_hires;
static NVSDK_NGX_Handle *g_handle;
static NVSDK_NGX_Parameter *g_params;
static ID3D12Device *g_dev;

static int ngx_renodx_present(const wchar_t *dir)
{
    wchar_t addon[MAX_PATH];

    if (!dir)
	return 0;
    if (_snwprintf(addon, MAX_PATH, L"%s\\renodx-dlss5.addon64", dir) < 0)
	return 0;
    return GetFileAttributesW(addon) != INVALID_FILE_ATTRIBUTES;
}

static void ngx_teardown(void)
{
    if (g_handle)
	NVSDK_NGX_D3D12_ReleaseFeature(g_handle);
    g_handle = NULL;
    if (g_params)
	NVSDK_NGX_D3D12_DestroyParameters(g_params);
    g_params = NULL;
    if (g_dev)
	NVSDK_NGX_D3D12_Shutdown1(g_dev);
    g_dev = NULL;
    g_inited = 0;
    g_hires = 0;
}

static int ngx_check_dlss(void)
{
    int needs_driver = 0;
    unsigned int min_maj = 0;
    unsigned int min_min = 0;
    int available = 0;
    NVSDK_NGX_Result r_drv;
    NVSDK_NGX_Result r_maj;
    NVSDK_NGX_Result r_min;
    NVSDK_NGX_Result r_av;
    NVSDK_NGX_Result feat = NVSDK_NGX_Result_Fail;

    r_drv = NVSDK_NGX_Parameter_GetI(g_params,
		NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
		&needs_driver);
    r_maj = NVSDK_NGX_Parameter_GetUI(g_params,
		NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
		&min_maj);
    r_min = NVSDK_NGX_Parameter_GetUI(g_params,
		NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,
		&min_min);
    if (r_drv == NVSDK_NGX_Result_Success &&
	r_maj == NVSDK_NGX_Result_Success &&
	r_min == NVSDK_NGX_Result_Success)
    {
	if (needs_driver)
	{
	    fprintf(stderr,
		    "NGX: driver too old, need %u.%u, using nearest\n",
		    min_maj, min_min);
	    return 0;
	}
	fprintf(stderr, "NGX: min driver %u.%u\n", min_maj, min_min);
    }

    r_av = NVSDK_NGX_Parameter_GetI(g_params,
		NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (r_av != NVSDK_NGX_Result_Success || !available)
    {
	NVSDK_NGX_Parameter_GetI(g_params,
		NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
		(int *)&feat);
	fprintf(stderr, "NGX: DLSS unavailable (0x%08x %ls), using nearest\n",
		(unsigned)feat, GetNGXResultAsString(feat));
	return 0;
    }
    return 1;
}

static void ngx_log_optimal(void)
{
    unsigned int opt_w = 0;
    unsigned int opt_h = 0;
    unsigned int max_w = 0;
    unsigned int max_h = 0;
    unsigned int min_w = 0;
    unsigned int min_h = 0;
    float sharp = 0.0f;
    NVSDK_NGX_Result r;

    r = NGX_DLSS_GET_OPTIMAL_SETTINGS(g_params, 1280, 800,
		NVSDK_NGX_PerfQuality_Value_UltraPerformance,
		&opt_w, &opt_h, &max_w, &max_h, &min_w, &min_h, &sharp);
    if (NVSDK_NGX_FAILED(r))
    {
	fprintf(stderr, "NGX: optimal settings query failed (0x%08x %ls)\n",
		(unsigned)r, GetNGXResultAsString(r));
	return;
    }
    fprintf(stderr,
	    "NGX: optimal %ux%u (min %ux%u max %ux%u), create stays 320x200\n",
	    opt_w, opt_h, min_w, min_h, max_w, max_h);
}

static int ngx_create_feature(ID3D12GraphicsCommandList *cl)
{
    NVSDK_NGX_DLSS_Create_Params create;
    NVSDK_NGX_Result r;

    if (g_handle)
	return 1;
    if (!cl || !g_params)
	return 0;

    memset(&create, 0, sizeof(create));
    if (g_hires)
    {
	create.Feature.InWidth = 1280;
	create.Feature.InHeight = 800;
	create.Feature.InTargetWidth = 1280;
	create.Feature.InTargetHeight = 800;
	create.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
	create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    }
    else
    {
	create.Feature.InWidth = 320;
	create.Feature.InHeight = 200;
	create.Feature.InTargetWidth = 1280;
	create.Feature.InTargetHeight = 800;
	create.Feature.InPerfQualityValue =
	    NVSDK_NGX_PerfQuality_Value_UltraPerformance;
	create.InFeatureCreateFlags =
	    NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
	    NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    }

    r = NGX_D3D12_CREATE_DLSS_EXT(cl, 1, 1, &g_handle, g_params, &create);
    if (NVSDK_NGX_FAILED(r) || !g_handle)
    {
	fprintf(stderr, "NGX: CREATE_DLSS_EXT failed (0x%08x %ls)\n",
		(unsigned)r, GetNGXResultAsString(r));
	g_handle = NULL;
	return 0;
    }
    fprintf(stderr, "NGX: DLSS feature created on first evaluate\n");
    return 1;
}

int Ngx_Init(void *device, void *queue)
{
    NVSDK_NGX_Result r;
    wchar_t path[MAX_PATH];

    g_inited = 0;
    g_hires = 0;
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
	fprintf(stderr, "NGX: init failed (0x%08x %ls), using nearest\n",
		(unsigned)r, GetNGXResultAsString(r));
	g_dev = NULL;
	return 0;
    }

    r = NVSDK_NGX_D3D12_GetCapabilityParameters(&g_params);
    if (NVSDK_NGX_FAILED(r) || !g_params)
    {
	fprintf(stderr, "NGX: no capability parameters, using nearest\n");
	NVSDK_NGX_D3D12_Shutdown1(g_dev);
	g_dev = NULL;
	return 0;
    }

    if (!ngx_check_dlss())
    {
	ngx_teardown();
	return 0;
    }
    ngx_log_optimal();

    g_hires = ngx_renodx_present(path);
    if (g_hires)
	fprintf(stderr,
		"NGX: RenoDX addon present; hi-res DLAA + evaluate "
		"(HUD is blitted after NR)\n");

    (void)queue;
    fprintf(stderr,
	    "NGX: runtime ready (%s, feature created on first evaluate)\n",
	    g_hires ? "dlss5-dlaa" : "dlss-upscale");
    g_inited = 1;
    return 1;
}

void Ngx_Shutdown(void)
{
    ngx_teardown();
}

int Ngx_Ready(void)
{
    return g_inited;
}

int Ngx_WantsHiRes(void)
{
    return g_inited && g_hires;
}

int Ngx_ShowEvalOutput(void)
{
    return g_inited;
}

int Ngx_Evaluate(void *cmdlist, void *color, void *depth, void *velocity,
		 void *output, int reset)
{
    ID3D12GraphicsCommandList *cl = (ID3D12GraphicsCommandList *)cmdlist;
    NVSDK_NGX_D3D12_DLSS_Eval_Params ev;
    NVSDK_NGX_Result r;
    unsigned sub_w;
    unsigned sub_h;

    if (!g_inited || !cl || !color || !depth || !velocity || !output)
	return 0;
    if (!ngx_create_feature(cl))
    {
	g_inited = 0;
	return 0;
    }

    /* DLAA create is 1280x800. A smaller InRenderSubrect is InvalidParameter. */
    if (g_hires)
    {
	sub_w = 1280;
	sub_h = 800;
    }
    else
    {
	sub_w = 320;
	sub_h = 200;
    }

    memset(&ev, 0, sizeof(ev));
    ev.Feature.pInColor = (ID3D12Resource *)color;
    ev.Feature.pInOutput = (ID3D12Resource *)output;
    ev.pInDepth = (ID3D12Resource *)depth;
    ev.pInMotionVectors = (ID3D12Resource *)velocity;
    ev.InJitterOffsetX = 0.0f;
    ev.InJitterOffsetY = 0.0f;
    ev.InReset = reset ? 1 : 0;
    ev.InRenderSubrectDimensions.Width = sub_w;
    ev.InRenderSubrectDimensions.Height = sub_h;
    if (g_hires)
    {
	static int hires_warm;

	if (hires_warm < 8)
	{
	    ev.InReset = 1;
	    hires_warm++;
	}
	ev.InMVScaleX = 4.0f;
	ev.InMVScaleY = 4.0f;
    }
    else
    {
	ev.InMVScaleX = 1.0f;
	ev.InMVScaleY = 1.0f;
    }
    ev.InFrameTimeDeltaInMsec = 1000.0f / 35.0f;

    r = NGX_D3D12_EVALUATE_DLSS_EXT(cl, g_handle, g_params, &ev);
    if (NVSDK_NGX_FAILED(r))
    {
	fprintf(stderr, "NGX: EVALUATE_DLSS_EXT failed (0x%08x %ls)\n",
		(unsigned)r, GetNGXResultAsString(r));
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

int Ngx_WantsHiRes(void)
{
    return 0;
}

int Ngx_ShowEvalOutput(void)
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
