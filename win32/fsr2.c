/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "fsr2.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "m_argv.h"
#ifdef __cplusplus
}
#endif

#ifdef WINDOOM_HAS_FSR2
#ifdef __cplusplus
extern "C" {
#endif
#include "doomdef.h"
#include "r_main.h"
#include "r_state.h"
#include "tables.h"
#ifdef __cplusplus
}
#endif

#define boolean BOOLEAN_WIN32_AVOID
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <string.h>
#include <d3d12.h>
#undef boolean

#include <ffx_fsr2.h>
#include <dx12/ffx_fsr2_dx12.h>
#endif

int Fsr2_Wanted(void)
{
    return M_CheckParm("-nofsr2") == 0;
}

#ifdef WINDOOM_HAS_FSR2

#define FSR2_IN_W 320
#define FSR2_IN_H 200
#define FSR2_OUT_W 1280
#define FSR2_OUT_H 800

static int g_inited;
static int g_jittered;
static int g_jitter_index;
static float g_jx;
static float g_jy;
static angle_t g_saved_viewangle;
static int g_saved_centery;
static fixed_t g_saved_centeryfrac;
static fixed_t g_saved_viewsin;
static fixed_t g_saved_viewcos;
static void *g_scratch;
static FfxFsr2Context g_ctx;
static FfxFsr2Interface g_iface;

static void fsr2_msg(FfxFsr2MsgType type, const wchar_t *message)
{
    fprintf(stderr, "FSR2: %s %ls\n",
	    type == FFX_FSR2_MESSAGE_TYPE_ERROR ? "error" : "warn",
	    message ? message : L"");
}

static void fsr2_teardown(void)
{
    if (g_inited)
	ffxFsr2ContextDestroy(&g_ctx);
    g_inited = 0;
    g_jittered = 0;
    if (g_scratch)
    {
	free(g_scratch);
	g_scratch = NULL;
    }
}

int Fsr2_Init(void *device)
{
    ID3D12Device *dev = (ID3D12Device *)device;
    FfxFsr2ContextDescription desc;
    size_t scratch;
    FfxErrorCode err;

    g_inited = 0;
    g_jitter_index = 0;
    g_jx = 0.0f;
    g_jy = 0.0f;
    if (!dev || !Fsr2_Wanted())
	return 0;

    scratch = ffxFsr2GetScratchMemorySizeDX12();
    g_scratch = malloc(scratch);
    if (!g_scratch)
    {
	fprintf(stderr, "FSR2: scratch alloc failed, using nearest\n");
	return 0;
    }
    memset(g_scratch, 0, scratch);
    memset(&g_iface, 0, sizeof(g_iface));
    err = ffxFsr2GetInterfaceDX12(&g_iface, dev, g_scratch, scratch);
    if (err != FFX_OK)
    {
	fprintf(stderr, "FSR2: GetInterfaceDX12 failed (0x%08x)\n",
		(unsigned)err);
	fsr2_teardown();
	return 0;
    }

    memset(&desc, 0, sizeof(desc));
    desc.flags = FFX_FSR2_ENABLE_AUTO_EXPOSURE;
    desc.maxRenderSize.width = FSR2_IN_W;
    desc.maxRenderSize.height = FSR2_IN_H;
    desc.displaySize.width = FSR2_OUT_W;
    desc.displaySize.height = FSR2_OUT_H;
    desc.callbacks = g_iface;
    desc.device = ffxGetDeviceDX12(dev);
    desc.fpMessage = fsr2_msg;

    memset(&g_ctx, 0, sizeof(g_ctx));
    err = ffxFsr2ContextCreate(&g_ctx, &desc);
    if (err != FFX_OK)
    {
	fprintf(stderr, "FSR2: ContextCreate failed (0x%08x), using nearest\n",
		(unsigned)err);
	fsr2_teardown();
	return 0;
    }

    g_inited = 1;
    fprintf(stderr, "FSR2: ready 320x200 -> 1280x800 (Halton jitter)\n");
    return 1;
}

void Fsr2_Shutdown(void)
{
    fsr2_teardown();
}

int Fsr2_Ready(void)
{
    return g_inited;
}

void Fsr2_ApplyRasterJitter(void)
{
    int32_t phases;
    int cx;
    float pixel_ang;
    float yoff;

    if (!g_inited || g_jittered)
	return;

    phases = ffxFsr2GetJitterPhaseCount(FSR2_IN_W, FSR2_OUT_W);
    if (phases < 1)
	phases = 1;
    g_jx = 0.0f;
    g_jy = 0.0f;
    ffxFsr2GetJitterOffset(&g_jx, &g_jy, g_jitter_index, phases);
    g_jitter_index++;

    g_saved_viewangle = viewangle;
    g_saved_centery = centery;
    g_saved_centeryfrac = centeryfrac;
    g_saved_viewsin = viewsin;
    g_saved_viewcos = viewcos;

    cx = centerx;
    if (cx < 0)
	cx = 0;
    if (cx >= SCREENWIDTH)
	cx = SCREENWIDTH - 1;
    pixel_ang = (float)(int)(xtoviewangle[cx] - xtoviewangle[cx + 1]);
    viewangle += (angle_t)(g_jx * pixel_ang);
    viewsin = finesine[viewangle >> ANGLETOFINESHIFT];
    viewcos = finecosine[viewangle >> ANGLETOFINESHIFT];

    yoff = g_jy * (float)FRACUNIT;
    centeryfrac += (fixed_t)yoff;
    centery = centeryfrac >> FRACBITS;
    g_jittered = 1;
}

void Fsr2_RestoreCamera(void)
{
    if (!g_jittered)
	return;
    viewangle = g_saved_viewangle;
    centery = g_saved_centery;
    centeryfrac = g_saved_centeryfrac;
    viewsin = g_saved_viewsin;
    viewcos = g_saved_viewcos;
    g_jittered = 0;
}

int Fsr2_Evaluate(void *cmdlist, void *color, void *depth, void *velocity,
		  void *output, int reset)
{
    ID3D12GraphicsCommandList *cl = (ID3D12GraphicsCommandList *)cmdlist;
    FfxFsr2DispatchDescription d;
    FfxErrorCode err;
    float half_h;
    float proj;

    if (!g_inited || !cl || !color || !depth || !velocity || !output)
	return 0;

    memset(&d, 0, sizeof(d));
    d.commandList = ffxGetCommandListDX12((ID3D12CommandList *)cl);
    d.color = ffxGetResourceDX12(&g_ctx, (ID3D12Resource *)color, L"Color",
				 FFX_RESOURCE_STATE_COMPUTE_READ);
    d.depth = ffxGetResourceDX12(&g_ctx, (ID3D12Resource *)depth, L"Depth",
				 FFX_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors = ffxGetResourceDX12(&g_ctx, (ID3D12Resource *)velocity,
					 L"Velocity",
					 FFX_RESOURCE_STATE_COMPUTE_READ);
    d.output = ffxGetResourceDX12(&g_ctx, (ID3D12Resource *)output, L"Output",
				  FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.jitterOffset.x = g_jx;
    d.jitterOffset.y = g_jy;
    d.motionVectorScale.x = 1.0f;
    d.motionVectorScale.y = 1.0f;
    d.renderSize.width = FSR2_IN_W;
    d.renderSize.height = FSR2_IN_H;
    d.enableSharpening = false;
    d.sharpness = 0.0f;
    d.frameTimeDelta = 1000.0f / 35.0f;
    d.preExposure = 1.0f;
    d.reset = reset ? true : false;
    d.cameraNear = 1.0f;
    d.cameraFar = 8192.0f;
    half_h = (viewheight > 0) ? (viewheight * 0.5f) : 100.0f;
    proj = (float)projection / (float)FRACUNIT;
    if (proj < 1.0f)
	proj = 1.0f;
    d.cameraFovAngleVertical = 2.0f * atanf(half_h / proj);
    d.viewSpaceToMetersFactor = 1.0f;

    err = ffxFsr2ContextDispatch(&g_ctx, &d);
    if (err != FFX_OK)
    {
	fprintf(stderr, "FSR2: ContextDispatch failed (0x%08x)\n",
		(unsigned)err);
	return 0;
    }
    return 1;
}

#else /* !WINDOOM_HAS_FSR2 */

int Fsr2_Init(void *device)
{
    (void)device;
    fprintf(stderr, "FSR2: built without SDK, nearest fallback\n");
    return 0;
}

void Fsr2_Shutdown(void)
{
}

int Fsr2_Ready(void)
{
    return 0;
}

void Fsr2_ApplyRasterJitter(void)
{
}

void Fsr2_RestoreCamera(void)
{
}

int Fsr2_Evaluate(void *cmdlist, void *color, void *depth, void *velocity,
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
