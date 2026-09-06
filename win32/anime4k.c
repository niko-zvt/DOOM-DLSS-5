/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "anime4k.h"

#include <stdio.h>
#include <string.h>

#ifdef WINDOOM_HAS_ANIME4K
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include "shaders/anime4k/mode_c_fast.inc.h"

#define A4K_IN_W 320
#define A4K_IN_H 200
#define A4K_MID_W 640
#define A4K_MID_H 400
#define A4K_OUT_W 1280
#define A4K_OUT_H 800
#define A4K_SRV_SLOTS 8
#define A4K_PASS_DESCS 9
#define A4K_MAX_PASSES 20

enum {
    PSO_CLAMP_H,
    PSO_CLAMP_V,
    PSO_D2S,
    PSO_CLAMP_FINAL,
    PSO_D0,
    PSO_D1,
    PSO_D2,
    PSO_D3,
    PSO_D4,
    PSO_D5,
    PSO_D6,
    PSO_D1X1,
    PSO_S0,
    PSO_S1,
    PSO_S2,
    PSO_S3,
    PSO_COUNT
};

enum {
    T_STATS0,
    T_STATS1,
    T_D0,
    T_D1,
    T_D2,
    T_D3,
    T_D4,
    T_D5,
    T_D6,
    T_DLAST,
    T_MID,
    T_S0,
    T_S1,
    T_S2,
    T_SLAST,
    T_HI,
    T_COUNT
};

#define T_COLOR (-1)
#define T_OUTPUT (-2)

static const char *g_pso_names[PSO_COUNT] = {
    "CS_ClampH", "CS_ClampV", "CS_D2S", "CS_ClampFinal",
    "CS_Denoise0", "CS_Denoise1", "CS_Denoise2", "CS_Denoise3",
    "CS_Denoise4", "CS_Denoise5", "CS_Denoise6", "CS_Denoise1x1",
    "CS_Upscale0", "CS_Upscale1", "CS_Upscale2", "CS_Upscale3"
};

static int g_ready;
static ID3D12Device *g_dev;
static ID3D12RootSignature *g_rs;
static ID3D12PipelineState *g_pso[PSO_COUNT];
static ID3D12DescriptorHeap *g_heap_cpu;
static ID3D12DescriptorHeap *g_heap_gpu;
static UINT g_desc_size;
static ID3D12Resource *g_tex[T_COUNT];
static D3D12_RESOURCE_STATES g_st[T_COUNT];
static DXGI_FORMAT g_fmt[T_COUNT];
static ID3D12Resource *g_color;
static ID3D12Resource *g_output;
static int g_pass;

static ID3D12Resource *make_tex(UINT w, UINT h, DXGI_FORMAT fmt)
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
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(ID3D12Device_CreateCommittedResource(
	    g_dev, &heap, D3D12_HEAP_FLAG_NONE, &desc,
	    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL,
	    &IID_ID3D12Resource, (void **)&res)))
	return NULL;
    return res;
}

static void trans(ID3D12GraphicsCommandList *cl, ID3D12Resource *res,
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

static void uav_bar(ID3D12GraphicsCommandList *cl, ID3D12Resource *res)
{
    D3D12_RESOURCE_BARRIER b;

    memset(&b, 0, sizeof(b));
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &b);
}

static int compile_pso(int i)
{
    ID3DBlob *blob = NULL;
    ID3DBlob *err = NULL;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd;
    HRESULT hr;

    hr = D3DCompile(g_a4k_hlsl, strlen(g_a4k_hlsl), "mode_c_fast.hlsl",
		    NULL, NULL, g_pso_names[i], "cs_5_0",
		    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
    if (FAILED(hr) || !blob)
    {
	if (err)
	{
	    fprintf(stderr, "Anime4K: compile %s failed: %s\n",
		    g_pso_names[i], (char *)ID3D10Blob_GetBufferPointer(err));
	    ID3D10Blob_Release(err);
	}
	else
	    fprintf(stderr, "Anime4K: compile %s failed (0x%08lx)\n",
		    g_pso_names[i], (unsigned long)hr);
	return 0;
    }
    memset(&pd, 0, sizeof(pd));
    pd.pRootSignature = g_rs;
    pd.CS.pShaderBytecode = ID3D10Blob_GetBufferPointer(blob);
    pd.CS.BytecodeLength = ID3D10Blob_GetBufferSize(blob);
    hr = ID3D12Device_CreateComputePipelineState(
	g_dev, &pd, &IID_ID3D12PipelineState, (void **)&g_pso[i]);
    ID3D10Blob_Release(blob);
    if (FAILED(hr))
    {
	fprintf(stderr, "Anime4K: PSO %s failed (0x%08lx)\n",
		g_pso_names[i], (unsigned long)hr);
	return 0;
    }
    return 1;
}

static int make_rootsig(void)
{
    D3D12_ROOT_PARAMETER rp[3];
    D3D12_DESCRIPTOR_RANGE srv_r;
    D3D12_DESCRIPTOR_RANGE uav_r;
    D3D12_STATIC_SAMPLER_DESC samp;
    D3D12_ROOT_SIGNATURE_DESC desc;
    ID3DBlob *blob = NULL;
    ID3DBlob *err = NULL;
    HRESULT hr;

    memset(&srv_r, 0, sizeof(srv_r));
    srv_r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_r.NumDescriptors = A4K_SRV_SLOTS;
    srv_r.BaseShaderRegister = 0;
    memset(&uav_r, 0, sizeof(uav_r));
    uav_r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uav_r.NumDescriptors = 1;
    uav_r.BaseShaderRegister = 0;

    memset(rp, 0, sizeof(rp));
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants.ShaderRegister = 0;
    rp[0].Constants.Num32BitValues = 4;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &srv_r;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[2].DescriptorTable.NumDescriptorRanges = 1;
    rp[2].DescriptorTable.pDescriptorRanges = &uav_r;
    rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    memset(&samp, 0, sizeof(samp));
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    memset(&desc, 0, sizeof(desc));
    desc.NumParameters = 3;
    desc.pParameters = rp;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &samp;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
				     &blob, &err);
    if (FAILED(hr) || !blob)
    {
	if (err)
	{
	    fprintf(stderr, "Anime4K: rootsig: %s\n",
		    (char *)ID3D10Blob_GetBufferPointer(err));
	    ID3D10Blob_Release(err);
	}
	return 0;
    }
    hr = ID3D12Device_CreateRootSignature(
	g_dev, 0, ID3D10Blob_GetBufferPointer(blob),
	ID3D10Blob_GetBufferSize(blob),
	&IID_ID3D12RootSignature, (void **)&g_rs);
    ID3D10Blob_Release(blob);
    return SUCCEEDED(hr);
}

static D3D12_CPU_DESCRIPTOR_HANDLE cpu_at(UINT index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h;

    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_heap_cpu, &h);
    h.ptr += (SIZE_T)index * g_desc_size;
    return h;
}

static void write_srv(UINT index, ID3D12Resource *res, DXGI_FORMAT fmt)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d;

    memset(&d, 0, sizeof(d));
    d.Format = fmt;
    d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(g_dev, res, &d, cpu_at(index));
}

static void write_uav(UINT index, ID3D12Resource *res, DXGI_FORMAT fmt)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d;

    memset(&d, 0, sizeof(d));
    d.Format = fmt;
    d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(g_dev, res, NULL, &d, cpu_at(index));
}

static int make_resources(void)
{
    D3D12_DESCRIPTOR_HEAP_DESC hd;
    int i;

    g_fmt[T_STATS0] = g_fmt[T_STATS1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    for (i = T_D0; i <= T_DLAST; i++)
	g_fmt[i] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    g_fmt[T_MID] = DXGI_FORMAT_R8G8B8A8_UNORM;
    g_fmt[T_S0] = g_fmt[T_S1] = g_fmt[T_S2] = g_fmt[T_SLAST] =
	DXGI_FORMAT_R16G16B16A16_FLOAT;
    g_fmt[T_HI] = DXGI_FORMAT_R8G8B8A8_UNORM;

    for (i = T_STATS0; i <= T_DLAST; i++)
	g_tex[i] = make_tex(A4K_IN_W, A4K_IN_H, g_fmt[i]);
    g_tex[T_MID] = make_tex(A4K_MID_W, A4K_MID_H, g_fmt[T_MID]);
    for (i = T_S0; i <= T_SLAST; i++)
	g_tex[i] = make_tex(A4K_MID_W, A4K_MID_H, g_fmt[i]);
    g_tex[T_HI] = make_tex(A4K_OUT_W, A4K_OUT_H, g_fmt[T_HI]);
    for (i = 0; i < T_COUNT; i++)
    {
	if (!g_tex[i])
	    return 0;
	g_st[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    g_desc_size = ID3D12Device_GetDescriptorHandleIncrementSize(
	g_dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    memset(&hd, 0, sizeof(hd));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = T_COUNT * 2 + 4;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(
	    g_dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&g_heap_cpu)))
	return 0;
    hd.NumDescriptors = A4K_MAX_PASSES * A4K_PASS_DESCS;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(
	    g_dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&g_heap_gpu)))
	return 0;

    for (i = 0; i < T_COUNT; i++)
    {
	write_srv((UINT)i, g_tex[i], g_fmt[i]);
	write_uav((UINT)(T_COUNT + i), g_tex[i], g_fmt[i]);
    }
    return 1;
}

static ID3D12Resource *tex_res(int id)
{
    if (id == T_COLOR)
	return g_color;
    if (id == T_OUTPUT)
	return g_output;
    if (id >= 0 && id < T_COUNT)
	return g_tex[id];
    return NULL;
}

static D3D12_RESOURCE_STATES *tex_st(int id, D3D12_RESOURCE_STATES *scratch)
{
    if (id >= 0 && id < T_COUNT)
	return &g_st[id];
    return scratch;
}

static void copy_desc(D3D12_CPU_DESCRIPTOR_HANDLE dst,
		      D3D12_CPU_DESCRIPTOR_HANDLE src)
{
    ID3D12Device_CopyDescriptorsSimple(
	g_dev, 1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

static void dispatch_pass(ID3D12GraphicsCommandList *cl, int pso,
			  UINT in_w, UINT in_h, UINT out_w, UINT out_h,
			  int nsrv, const int *srvs, int uav)
{
    D3D12_CPU_DESCRIPTOR_HANDLE gpu_cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_gpu;
    D3D12_RESOURCE_STATES ext_color = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES ext_out = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    UINT params[4];
    ID3D12DescriptorHeap *heaps[1];
    int i;
    int first = srvs[0];

    for (i = 0; i < nsrv; i++)
    {
	ID3D12Resource *r = tex_res(srvs[i]);
	D3D12_RESOURCE_STATES *st = tex_st(srvs[i],
	    srvs[i] == T_COLOR ? &ext_color : &ext_out);
	if (srvs[i] == T_COLOR)
	    continue;
	trans(cl, r, st,
	      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (uav >= 0)
    {
	trans(cl, g_tex[uav], &g_st[uav],
	      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	uav_bar(cl, g_tex[uav]);
    }

    write_srv(T_COUNT * 2, g_color, DXGI_FORMAT_R8G8B8A8_UNORM);
    write_uav(T_COUNT * 2 + 1, g_output, DXGI_FORMAT_B8G8R8A8_UNORM);

    if (g_pass >= A4K_MAX_PASSES)
	return;
    {
	UINT base = (UINT)g_pass * A4K_PASS_DESCS;

	g_pass++;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_heap_gpu,
								 &gpu_cpu);
	ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(g_heap_gpu,
								 &gpu_gpu);
	gpu_cpu.ptr += (SIZE_T)base * g_desc_size;
	gpu_gpu.ptr += (SIZE_T)base * g_desc_size;
    }
    for (i = 0; i < A4K_SRV_SLOTS; i++)
    {
	int id = (i < nsrv) ? srvs[i] : first;
	D3D12_CPU_DESCRIPTOR_HANDLE src;
	D3D12_CPU_DESCRIPTOR_HANDLE dst;

	if (id == T_COLOR)
	    src = cpu_at(T_COUNT * 2);
	else
	    src = cpu_at((UINT)id);
	dst = gpu_cpu;
	dst.ptr += (SIZE_T)i * g_desc_size;
	copy_desc(dst, src);
    }
    {
	D3D12_CPU_DESCRIPTOR_HANDLE src;
	D3D12_CPU_DESCRIPTOR_HANDLE dst;

	if (uav == T_OUTPUT)
	    src = cpu_at(T_COUNT * 2 + 1);
	else
	    src = cpu_at((UINT)(T_COUNT + uav));
	dst = gpu_cpu;
	dst.ptr += (SIZE_T)A4K_SRV_SLOTS * g_desc_size;
	copy_desc(dst, src);
    }

    params[0] = in_w;
    params[1] = in_h;
    params[2] = out_w;
    params[3] = out_h;
    heaps[0] = g_heap_gpu;
    ID3D12GraphicsCommandList_SetComputeRootSignature(cl, g_rs);
    ID3D12GraphicsCommandList_SetPipelineState(cl, g_pso[pso]);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(cl, 1, heaps);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(cl, 0, 4, params, 0);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cl, 1, gpu_gpu);
    {
	D3D12_GPU_DESCRIPTOR_HANDLE uav_gpu = gpu_gpu;
	uav_gpu.ptr += (SIZE_T)A4K_SRV_SLOTS * g_desc_size;
	ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cl, 2, uav_gpu);
    }
    ID3D12GraphicsCommandList_Dispatch(
	cl, (out_w + 7) / 8, (out_h + 7) / 8, 1);
    if (uav >= 0)
	uav_bar(cl, g_tex[uav]);
    else
	uav_bar(cl, g_output);
}

int Anime4K_Init(void *device)
{
    int i;

    Anime4K_Shutdown();
    g_dev = (ID3D12Device *)device;
    if (!g_dev)
	return 0;
    if (!make_rootsig())
    {
	fprintf(stderr, "Anime4K: root signature failed, nearest\n");
	Anime4K_Shutdown();
	return 0;
    }
    for (i = 0; i < PSO_COUNT; i++)
    {
	if (!compile_pso(i))
	{
	    Anime4K_Shutdown();
	    return 0;
	}
    }
    if (!make_resources())
    {
	fprintf(stderr, "Anime4K: textures failed, nearest\n");
	Anime4K_Shutdown();
	return 0;
    }
    g_ready = 1;
    fprintf(stderr, "Anime4K: Fast Mode C ready (320->640->1280)\n");
    return 1;
}

void Anime4K_Shutdown(void)
{
    int i;

    g_ready = 0;
    for (i = 0; i < T_COUNT; i++)
    {
	if (g_tex[i])
	    ID3D12Resource_Release(g_tex[i]);
	g_tex[i] = NULL;
    }
    if (g_heap_gpu)
	ID3D12DescriptorHeap_Release(g_heap_gpu);
    if (g_heap_cpu)
	ID3D12DescriptorHeap_Release(g_heap_cpu);
    g_heap_gpu = g_heap_cpu = NULL;
    for (i = 0; i < PSO_COUNT; i++)
    {
	if (g_pso[i])
	    ID3D12PipelineState_Release(g_pso[i]);
	g_pso[i] = NULL;
    }
    if (g_rs)
	ID3D12RootSignature_Release(g_rs);
    g_rs = NULL;
    g_dev = NULL;
    g_color = g_output = NULL;
}

int Anime4K_Ready(void)
{
    return g_ready;
}

int Anime4K_Evaluate(void *cmdlist, void *color, void *output)
{
    ID3D12GraphicsCommandList *cl = (ID3D12GraphicsCommandList *)cmdlist;
    int s;

    if (!g_ready || !cl || !color || !output)
	return 0;
    g_color = (ID3D12Resource *)color;
    g_output = (ID3D12Resource *)output;
    g_pass = 0;

    s = T_COLOR;
    dispatch_pass(cl, PSO_CLAMP_H, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_STATS0);
    s = T_STATS0;
    dispatch_pass(cl, PSO_CLAMP_V, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_STATS1);

    s = T_COLOR;
    dispatch_pass(cl, PSO_D0, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_D0);
    s = T_D0;
    dispatch_pass(cl, PSO_D1, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_D1);
    s = T_D1;
    dispatch_pass(cl, PSO_D2, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_D2);
    s = T_D2;
    dispatch_pass(cl, PSO_D3, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_D3);
    s = T_D3;
    dispatch_pass(cl, PSO_D4, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_D4);
    s = T_D4;
    dispatch_pass(cl, PSO_D5, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_D5);
    s = T_D5;
    dispatch_pass(cl, PSO_D6, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		  1, &s, T_D6);
    {
	int layers[7] = { T_D0, T_D1, T_D2, T_D3, T_D4, T_D5, T_D6 };
	dispatch_pass(cl, PSO_D1X1, A4K_IN_W, A4K_IN_H, A4K_IN_W, A4K_IN_H,
		      7, layers, T_DLAST);
    }
    {
	int d2s[2] = { T_DLAST, T_COLOR };
	dispatch_pass(cl, PSO_D2S, A4K_IN_W, A4K_IN_H, A4K_MID_W, A4K_MID_H,
		      2, d2s, T_MID);
    }

    s = T_MID;
    dispatch_pass(cl, PSO_S0, A4K_MID_W, A4K_MID_H, A4K_MID_W, A4K_MID_H,
		  1, &s, T_S0);
    s = T_S0;
    dispatch_pass(cl, PSO_S1, A4K_MID_W, A4K_MID_H, A4K_MID_W, A4K_MID_H,
		  1, &s, T_S1);
    s = T_S1;
    dispatch_pass(cl, PSO_S2, A4K_MID_W, A4K_MID_H, A4K_MID_W, A4K_MID_H,
		  1, &s, T_S2);
    s = T_S2;
    dispatch_pass(cl, PSO_S3, A4K_MID_W, A4K_MID_H, A4K_MID_W, A4K_MID_H,
		  1, &s, T_SLAST);
    {
	int d2s[2] = { T_SLAST, T_MID };
	dispatch_pass(cl, PSO_D2S, A4K_MID_W, A4K_MID_H, A4K_OUT_W, A4K_OUT_H,
		      2, d2s, T_HI);
    }
    {
	int fin[2] = { T_HI, T_STATS1 };
	dispatch_pass(cl, PSO_CLAMP_FINAL, A4K_OUT_W, A4K_OUT_H,
		      A4K_OUT_W, A4K_OUT_H, 2, fin, T_OUTPUT);
    }
    return 1;
}

#else /* !WINDOOM_HAS_ANIME4K */

int Anime4K_Init(void *device)
{
    (void)device;
    return 0;
}

void Anime4K_Shutdown(void)
{
}

int Anime4K_Ready(void)
{
    return 0;
}

int Anime4K_Evaluate(void *cmdlist, void *color, void *output)
{
    (void)cmdlist;
    (void)color;
    (void)output;
    return 0;
}

#endif
