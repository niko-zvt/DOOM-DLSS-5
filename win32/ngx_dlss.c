/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "ngx_dlss.h"

#include <stdio.h>

#include "m_argv.h"

#ifdef WINDOOM_HAS_NGX
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wchar.h>
#include <string.h>
#include <stdbool.h>
#include <d3d12.h>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssd.h>
#endif

enum
{
    NGX_MODE_DLSS5 = 0,
    NGX_MODE_K,
    NGX_MODE_L,
    NGX_MODE_RR
};

int Ngx_Wanted(void)
{
    return M_CheckParm("-nodlss") == 0;
}

#ifdef WINDOOM_HAS_NGX

static int g_inited;
static int g_hires;
static int g_mode;
static int g_using_rr;
static int g_want_rr;
static NVSDK_NGX_Handle *g_handle;
static NVSDK_NGX_Parameter *g_params;
static ID3D12Device *g_dev;
static ID3D12Resource *g_tex_spec;
static ID3D12Resource *g_tex_rough;
static ID3D12Resource *g_up_spec;
static ID3D12Resource *g_up_rough;
static D3D12_RESOURCE_STATES g_st_spec;
static D3D12_RESOURCE_STATES g_st_rough;
static int g_const_ready;

static const char *ngx_mode_name(int mode)
{
    if (mode == NGX_MODE_RR)
	return "rr";
    if (mode == NGX_MODE_K)
	return "k";
    if (mode == NGX_MODE_L)
	return "l";
    return "dlss5";
}

static int ngx_parse_mode_token(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
	s++;
    if (!_strnicmp(s, "rr", 2))
	return NGX_MODE_RR;
    if (!_strnicmp(s, "dlss5", 5))
	return NGX_MODE_DLSS5;
    if (s[0] == 'k' || s[0] == 'K')
	return NGX_MODE_K;
    if (s[0] == 'l' || s[0] == 'L')
	return NGX_MODE_L;
    return NGX_MODE_DLSS5;
}

static int ngx_read_mode_file(const wchar_t *dir)
{
    wchar_t path[MAX_PATH];
    FILE *f;
    char buf[32];

    if (!dir)
	return NGX_MODE_DLSS5;
    if (_snwprintf(path, MAX_PATH, L"%s\\ngx.mode", dir) < 0)
	return NGX_MODE_DLSS5;
    f = _wfopen(path, L"r");
    if (!f)
	return NGX_MODE_DLSS5;
    if (!fgets(buf, sizeof(buf), f))
    {
	fclose(f);
	return NGX_MODE_DLSS5;
    }
    fclose(f);
    return ngx_parse_mode_token(buf);
}

static int ngx_resolve_mode(const wchar_t *dir)
{
    int mode = ngx_read_mode_file(dir);

    if (M_CheckParm("-ngx-rr"))
	return NGX_MODE_RR;
    if (M_CheckParm("-ngx-k"))
	return NGX_MODE_K;
    if (M_CheckParm("-ngx-l"))
	return NGX_MODE_L;
    return mode;
}

static int ngx_renodx_present(const wchar_t *dir)
{
    wchar_t addon[MAX_PATH];

    if (!dir)
	return 0;
    if (_snwprintf(addon, MAX_PATH, L"%s\\renodx-dlss5.addon64", dir) < 0)
	return 0;
    return GetFileAttributesW(addon) != INVALID_FILE_ATTRIBUTES;
}

static void ngx_release_feature(void)
{
    if (g_handle)
	NVSDK_NGX_D3D12_ReleaseFeature(g_handle);
    g_handle = NULL;
    g_using_rr = 0;
}

static void ngx_release_const(void)
{
    if (g_tex_spec)
	ID3D12Resource_Release(g_tex_spec);
    if (g_tex_rough)
	ID3D12Resource_Release(g_tex_rough);
    if (g_up_spec)
	ID3D12Resource_Release(g_up_spec);
    if (g_up_rough)
	ID3D12Resource_Release(g_up_rough);
    g_tex_spec = NULL;
    g_tex_rough = NULL;
    g_up_spec = NULL;
    g_up_rough = NULL;
    g_const_ready = 0;
}

static void ngx_teardown(void)
{
    ngx_release_feature();
    ngx_release_const();
    if (g_params)
	NVSDK_NGX_D3D12_DestroyParameters(g_params);
    g_params = NULL;
    if (g_dev)
	NVSDK_NGX_D3D12_Shutdown1(g_dev);
    g_dev = NULL;
    g_inited = 0;
    g_hires = 0;
    g_want_rr = 0;
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

static void ngx_pin_sr_preset(unsigned preset)
{
    NVSDK_NGX_Parameter_SetUI(g_params,
	NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, preset);
    NVSDK_NGX_Parameter_SetUI(g_params,
	NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, preset);
    NVSDK_NGX_Parameter_SetUI(g_params,
	NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, preset);
    NVSDK_NGX_Parameter_SetUI(g_params,
	NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, preset);
    NVSDK_NGX_Parameter_SetUI(g_params,
	NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, preset);
}

static void ngx_apply_sr_hint(void)
{
    if (g_mode == NGX_MODE_K)
	ngx_pin_sr_preset(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    else if (g_mode == NGX_MODE_L)
	ngx_pin_sr_preset(NVSDK_NGX_DLSS_Hint_Render_Preset_L);
}

static ID3D12Resource *ngx_make_tex(UINT w, UINT h, DXGI_FORMAT fmt)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *res = NULL;

    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = fmt;
    desc.SampleDesc.Count = 1;
    if (FAILED(ID3D12Device_CreateCommittedResource(
	    g_dev, &heap, D3D12_HEAP_FLAG_NONE, &desc,
	    D3D12_RESOURCE_STATE_COPY_DEST, NULL,
	    &IID_ID3D12Resource, (void **)&res)))
	return NULL;
    return res;
}

static ID3D12Resource *ngx_make_upload(UINT64 size)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *res = NULL;

    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(ID3D12Device_CreateCommittedResource(
	    g_dev, &heap, D3D12_HEAP_FLAG_NONE, &desc,
	    D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
	    &IID_ID3D12Resource, (void **)&res)))
	return NULL;
    return res;
}

static UINT64 ngx_upload_bytes(UINT w, UINT h, UINT bpp)
{
    UINT pitch = (w * bpp + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
	~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    return (UINT64)pitch * h + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
}

static void ngx_barrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *res,
			D3D12_RESOURCE_STATES *cur, D3D12_RESOURCE_STATES next)
{
    D3D12_RESOURCE_BARRIER b;

    if (!res || *cur == next)
	return;
    memset(&b, 0, sizeof(b));
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = *cur;
    b.Transition.StateAfter = next;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &b);
    *cur = next;
}

static void ngx_upload_solid(ID3D12GraphicsCommandList *cl,
			     ID3D12Resource *tex, ID3D12Resource *upload,
			     UINT w, UINT h,
			     unsigned char r, unsigned char g,
			     unsigned char b, unsigned char a)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_RESOURCE_DESC desc;
    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    D3D12_TEXTURE_COPY_LOCATION src_loc;
    unsigned char *mapped = NULL;
    UINT y;
    UINT x;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ID3D12Device_GetCopyableFootprints(g_dev, &desc, 0, 1, 0,
				       &fp, NULL, NULL, NULL);
    if (FAILED(ID3D12Resource_Map(upload, 0, NULL, (void **)&mapped)))
	return;
    for (y = 0; y < h; y++)
    {
	unsigned char *row = mapped + fp.Offset + y * fp.Footprint.RowPitch;
	for (x = 0; x < w; x++)
	{
	    row[x * 4 + 0] = r;
	    row[x * 4 + 1] = g;
	    row[x * 4 + 2] = b;
	    row[x * 4 + 3] = a;
	}
    }
    ID3D12Resource_Unmap(upload, 0, NULL);

    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource = tex;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource = upload;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(cl, &dst_loc, 0, 0, 0,
						&src_loc, NULL);
}

static int ngx_ensure_const(ID3D12GraphicsCommandList *cl)
{
    UINT64 bytes;

    if (g_const_ready)
	return 1;
    if (!g_dev || !cl)
	return 0;

    bytes = ngx_upload_bytes(320, 200, 4);
    g_tex_spec = ngx_make_tex(320, 200, DXGI_FORMAT_R8G8B8A8_UNORM);
    g_tex_rough = ngx_make_tex(320, 200, DXGI_FORMAT_R8G8B8A8_UNORM);
    g_up_spec = ngx_make_upload(bytes);
    g_up_rough = ngx_make_upload(bytes);
    if (!g_tex_spec || !g_tex_rough || !g_up_spec || !g_up_rough)
    {
	fprintf(stderr, "NGX: RR helper textures failed\n");
	ngx_release_const();
	return 0;
    }
    g_st_spec = D3D12_RESOURCE_STATE_COPY_DEST;
    g_st_rough = D3D12_RESOURCE_STATE_COPY_DEST;
    ngx_upload_solid(cl, g_tex_spec, g_up_spec, 320, 200, 0, 0, 0, 255);
    ngx_upload_solid(cl, g_tex_rough, g_up_rough, 320, 200, 255, 255, 255, 255);
    ngx_barrier(cl, g_tex_spec, &g_st_spec,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ngx_barrier(cl, g_tex_rough, &g_st_rough,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g_const_ready = 1;
    return 1;
}

static int ngx_create_sr(ID3D12GraphicsCommandList *cl)
{
    NVSDK_NGX_DLSS_Create_Params create;
    NVSDK_NGX_Result r;

    memset(&create, 0, sizeof(create));
    ngx_apply_sr_hint();
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
    g_using_rr = 0;
    fprintf(stderr, "NGX: DLSS SR created (%s)\n", ngx_mode_name(g_mode));
    return 1;
}

static int ngx_create_rr(ID3D12GraphicsCommandList *cl)
{
    NVSDK_NGX_DLSSD_Create_Params create;
    NVSDK_NGX_Result r;
    int available = 0;

    NVSDK_NGX_Parameter_GetI(g_params,
	    NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available);
    if (!available)
	fprintf(stderr, "NGX: Ray Reconstruction not advertised, trying anyway\n");

    memset(&create, 0, sizeof(create));
    create.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
    create.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;
    create.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
    create.InWidth = 320;
    create.InHeight = 200;
    create.InTargetWidth = 1280;
    create.InTargetHeight = 800;
    create.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    create.InFeatureCreateFlags =
	NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
	NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    create.InEnableOutputSubrects = false;

    r = NGX_D3D12_CREATE_DLSSD_EXT(cl, 1, 1, &g_handle, g_params, &create);
    if (NVSDK_NGX_FAILED(r) || !g_handle)
    {
	fprintf(stderr,
		"NGX: CREATE_DLSSD_EXT failed (0x%08x %ls), "
		"falling back to SR preset K\n",
		(unsigned)r, GetNGXResultAsString(r));
	g_handle = NULL;
	return 0;
    }
    g_using_rr = 1;
    fprintf(stderr, "NGX: Ray Reconstruction created (synthetic albedo/roughness)\n");
    return 1;
}

static void ngx_fallback_sr_k(void)
{
    ngx_release_feature();
    g_want_rr = 0;
    g_mode = NGX_MODE_K;
    g_hires = 0;
}

static int ngx_create_feature(ID3D12GraphicsCommandList *cl)
{
    if (g_handle)
	return 1;
    if (!cl || !g_params)
	return 0;

    if (g_want_rr)
    {
	if (ngx_create_rr(cl))
	    return 1;
	ngx_fallback_sr_k();
    }
    return ngx_create_sr(cl);
}

int Ngx_Init(void *device, void *queue)
{
    NVSDK_NGX_Result r;
    wchar_t path[MAX_PATH];

    g_inited = 0;
    g_hires = 0;
    g_handle = NULL;
    g_params = NULL;
    g_using_rr = 0;
    g_dev = (ID3D12Device *)device;
    if (!g_dev)
	return 0;

    GetModuleFileNameW(NULL, path, MAX_PATH);
    {
	wchar_t *slash = wcsrchr(path, L'\\');
	if (slash)
	    *slash = 0;
    }

    g_mode = ngx_resolve_mode(path);
    g_want_rr = (g_mode == NGX_MODE_RR);
    fprintf(stderr, "NGX: mode %s\n", ngx_mode_name(g_mode));

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

    g_hires = (g_mode == NGX_MODE_DLSS5) && ngx_renodx_present(path);
    if (g_hires)
	fprintf(stderr,
		"NGX: RenoDX addon present; hi-res DLAA + evaluate "
		"(HUD is blitted after NR)\n");

    (void)queue;
    fprintf(stderr,
	    "NGX: runtime ready (%s, feature created on first evaluate)\n",
	    g_want_rr ? "rr" : (g_hires ? "dlss5-dlaa" : "dlss-upscale"));
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
    return g_inited && g_hires && !g_want_rr;
}

int Ngx_ShowEvalOutput(void)
{
    return g_inited;
}

static int ngx_eval_sr(ID3D12GraphicsCommandList *cl,
		       ID3D12Resource *color, ID3D12Resource *depth,
		       ID3D12Resource *velocity, ID3D12Resource *output,
		       int reset)
{
    NVSDK_NGX_D3D12_DLSS_Eval_Params ev;
    NVSDK_NGX_Result r;
    unsigned sub_w;
    unsigned sub_h;

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
    ev.Feature.pInColor = color;
    ev.Feature.pInOutput = output;
    ev.pInDepth = depth;
    ev.pInMotionVectors = velocity;
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

static int ngx_eval_rr(ID3D12GraphicsCommandList *cl,
		       ID3D12Resource *color, ID3D12Resource *depth,
		       ID3D12Resource *velocity, ID3D12Resource *normal,
		       ID3D12Resource *output, int reset)
{
    NVSDK_NGX_D3D12_DLSSD_Eval_Params ev;
    NVSDK_NGX_Result r;

    if (!normal)
	return 0;
    if (!ngx_ensure_const(cl))
	return 0;

    memset(&ev, 0, sizeof(ev));
    ev.pInColor = color;
    ev.pInOutput = output;
    ev.pInDepth = depth;
    ev.pInMotionVectors = velocity;
    ev.pInNormals = normal;
    ev.pInDiffuseAlbedo = color;
    ev.pInSpecularAlbedo = g_tex_spec;
    ev.pInRoughness = g_tex_rough;
    ev.InJitterOffsetX = 0.0f;
    ev.InJitterOffsetY = 0.0f;
    ev.InReset = reset ? 1 : 0;
    ev.InRenderSubrectDimensions.Width = 320;
    ev.InRenderSubrectDimensions.Height = 200;
    ev.InMVScaleX = 1.0f;
    ev.InMVScaleY = 1.0f;
    ev.InFrameTimeDeltaInMsec = 1000.0f / 35.0f;

    r = NGX_D3D12_EVALUATE_DLSSD_EXT(cl, g_handle, g_params, &ev);
    if (NVSDK_NGX_FAILED(r))
    {
	fprintf(stderr,
		"NGX: EVALUATE_DLSSD_EXT failed (0x%08x %ls), "
		"falling back to SR preset K\n",
		(unsigned)r, GetNGXResultAsString(r));
	return 0;
    }
    return 1;
}

int Ngx_Evaluate(void *cmdlist, void *color, void *depth, void *velocity,
		 void *normal, void *output, int reset)
{
    ID3D12GraphicsCommandList *cl = (ID3D12GraphicsCommandList *)cmdlist;

    if (!g_inited || !cl || !color || !depth || !velocity || !output)
	return 0;
    if (!ngx_create_feature(cl))
    {
	g_inited = 0;
	return 0;
    }

    if (g_using_rr)
    {
	if (ngx_eval_rr(cl, (ID3D12Resource *)color, (ID3D12Resource *)depth,
			(ID3D12Resource *)velocity, (ID3D12Resource *)normal,
			(ID3D12Resource *)output, reset))
	    return 1;
	ngx_fallback_sr_k();
	if (!ngx_create_feature(cl))
	{
	    g_inited = 0;
	    return 0;
	}
    }

    return ngx_eval_sr(cl, (ID3D12Resource *)color, (ID3D12Resource *)depth,
		       (ID3D12Resource *)velocity, (ID3D12Resource *)output,
		       reset);
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
		 void *normal, void *output, int reset)
{
    (void)cmdlist;
    (void)color;
    (void)depth;
    (void)velocity;
    (void)normal;
    (void)output;
    (void)reset;
    return 0;
}

#endif
