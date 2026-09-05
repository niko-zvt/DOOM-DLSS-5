/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "doomdef.h"
#include "doomtype.h"

#define boolean BOOLEAN_WIN32_AVOID
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef boolean

#include "d_event.h"
#include "d_main.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "gbuffer.h"
#include "ngx_dlss.h"

#define WIN_SCALE 4
#define WIN_W (SCREENWIDTH * WIN_SCALE)
#define WIN_H (SCREENHEIGHT * WIN_SCALE)
#define FRAME_COUNT 2

static HWND g_hwnd;
static int g_mouse_grab;
static int g_mouse_buttons;
static int g_have_focus = 1;

static ID3D12Device *g_dev;
static ID3D12CommandQueue *g_queue;
static ID3D12CommandAllocator *g_alloc;
static ID3D12GraphicsCommandList *g_cmd;
static ID3D12Fence *g_fence;
static HANDLE g_fence_ev;
static UINT64 g_fence_val;
static IDXGISwapChain3 *g_swap;
static ID3D12Resource *g_bb[FRAME_COUNT];
static ID3D12Resource *g_tex_color;
static ID3D12Resource *g_tex_depth;
static ID3D12Resource *g_tex_normal;
static ID3D12Resource *g_tex_velocity;
static ID3D12Resource *g_up_color;
static ID3D12Resource *g_up_depth;
static ID3D12Resource *g_up_normal;
static ID3D12Resource *g_up_velocity;
static ID3D12Resource *g_up_present;
static D3D12_RESOURCE_STATES g_bb_state[FRAME_COUNT];

static unsigned char g_present[WIN_W * WIN_H * 4];
static unsigned char g_palette[768];

static int xlatekey(WPARAM vk)
{
    switch (vk)
    {
      case VK_RIGHT: return KEY_RIGHTARROW;
      case VK_LEFT: return KEY_LEFTARROW;
      case VK_UP: return KEY_UPARROW;
      case VK_DOWN: return KEY_DOWNARROW;
      case VK_ESCAPE: return KEY_ESCAPE;
      case VK_RETURN: return KEY_ENTER;
      case VK_TAB: return KEY_TAB;
      case VK_F1: return KEY_F1;
      case VK_F2: return KEY_F2;
      case VK_F3: return KEY_F3;
      case VK_F4: return KEY_F4;
      case VK_F5: return KEY_F5;
      case VK_F6: return KEY_F6;
      case VK_F7: return KEY_F7;
      case VK_F8: return KEY_F8;
      case VK_F9: return KEY_F9;
      case VK_F10: return KEY_F10;
      case VK_F11: return KEY_F11;
      case VK_F12: return KEY_F12;
      case VK_BACK: return KEY_BACKSPACE;
      case VK_PAUSE: return KEY_PAUSE;
      case VK_OEM_PLUS: return KEY_EQUALS;
      case VK_OEM_MINUS: return KEY_MINUS;
      case VK_SHIFT:
      case VK_LSHIFT:
      case VK_RSHIFT: return KEY_RSHIFT;
      case VK_CONTROL:
      case VK_LCONTROL:
      case VK_RCONTROL: return KEY_RCTRL;
      case VK_MENU:
      case VK_LMENU:
      case VK_RMENU: return KEY_RALT;
      case VK_SPACE: return ' ';
      default:
	if (vk >= 'A' && vk <= 'Z')
	    return (int)(vk - 'A' + 'a');
	if (vk >= '0' && vk <= '9')
	    return (int)vk;
	{
	    int ch = (int)(MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_CHAR) & 255);
	    if (ch >= 'A' && ch <= 'Z')
		ch = ch - 'A' + 'a';
	    return ch;
	}
    }
}

static void post_mouse(int dx, int dy)
{
    event_t ev;

    (void)dy;
    ev.type = ev_mouse;
    ev.data1 = g_mouse_buttons;
    ev.data2 = dx;
    ev.data3 = 0;
    D_PostEvent(&ev);
}

static void grab_mouse(int grab)
{
    RECT rc;
    POINT pt;

    g_mouse_grab = grab;
    if (!g_hwnd)
	return;
    if (grab)
    {
	GetClientRect(g_hwnd, &rc);
	pt.x = rc.right / 2;
	pt.y = rc.bottom / 2;
	ClientToScreen(g_hwnd, &pt);
	SetCursorPos(pt.x, pt.y);
	ShowCursor(FALSE);
    }
    else
	ShowCursor(TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    event_t ev;

    switch (msg)
    {
      case WM_CLOSE:
	I_Quit();
	return 0;
      case WM_DESTROY:
	PostQuitMessage(0);
	return 0;
      case WM_SETFOCUS:
	g_have_focus = 1;
	grab_mouse(1);
	return 0;
      case WM_KILLFOCUS:
	g_have_focus = 0;
	grab_mouse(0);
	return 0;
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
	if (wparam == VK_F1) { GB_SetDebugView(GB_VIEW_COLOR); return 0; }
	if (wparam == VK_F2) { GB_SetDebugView(GB_VIEW_DEPTH); return 0; }
	if (wparam == VK_F3) { GB_SetDebugView(GB_VIEW_NORMAL); return 0; }
	if (wparam == VK_F4) { GB_SetDebugView(GB_VIEW_VELOCITY); return 0; }
	if (!(lparam & (1 << 30)))
	{
	    ev.type = ev_keydown;
	    ev.data1 = xlatekey(wparam);
	    D_PostEvent(&ev);
	}
	return 0;
      case WM_KEYUP:
      case WM_SYSKEYUP:
	if (wparam >= VK_F1 && wparam <= VK_F4)
	    return 0;
	ev.type = ev_keyup;
	ev.data1 = xlatekey(wparam);
	D_PostEvent(&ev);
	return 0;
      case WM_LBUTTONDOWN:
	g_mouse_buttons |= 1;
	post_mouse(0, 0);
	return 0;
      case WM_LBUTTONUP:
	g_mouse_buttons &= ~1;
	post_mouse(0, 0);
	return 0;
      case WM_RBUTTONDOWN:
	g_mouse_buttons |= 4;
	post_mouse(0, 0);
	return 0;
      case WM_RBUTTONUP:
	g_mouse_buttons &= ~4;
	post_mouse(0, 0);
	return 0;
      case WM_MBUTTONDOWN:
	g_mouse_buttons |= 2;
	post_mouse(0, 0);
	return 0;
      case WM_MBUTTONUP:
	g_mouse_buttons &= ~2;
	post_mouse(0, 0);
	return 0;
      default:
	return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

static void wait_gpu(void)
{
    UINT64 v;

    if (!g_queue || !g_fence)
	return;
    v = ++g_fence_val;
    if (FAILED(ID3D12CommandQueue_Signal(g_queue, g_fence, v)))
	return;
    if (ID3D12Fence_GetCompletedValue(g_fence) < v)
    {
	ID3D12Fence_SetEventOnCompletion(g_fence, v, g_fence_ev);
	WaitForSingleObject(g_fence_ev, INFINITE);
    }
}

static void barrier(ID3D12Resource *res, D3D12_RESOURCE_STATES *cur,
		    D3D12_RESOURCE_STATES next)
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
    ID3D12GraphicsCommandList_ResourceBarrier(g_cmd, 1, &b);
    *cur = next;
}

static ID3D12Resource *make_tex(UINT w, UINT h, DXGI_FORMAT fmt,
				const wchar_t *name)
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
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(ID3D12Device_CreateCommittedResource(
	    g_dev, &heap, D3D12_HEAP_FLAG_NONE, &desc,
	    D3D12_RESOURCE_STATE_COPY_DEST, NULL,
	    &IID_ID3D12Resource, (void **)&res)))
	return NULL;
    if (name)
	ID3D12Object_SetName((ID3D12Object *)res, name);
    return res;
}

static ID3D12Resource *make_upload(UINT64 size)
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

static UINT64 upload_bytes(UINT w, UINT h, UINT bpp)
{
    UINT pitch = (w * bpp + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
	~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    return (UINT64)pitch * h + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
}

static void upload_tex(ID3D12Resource *dst, ID3D12Resource *upload,
		       const void *src, UINT w, UINT h, UINT bpp,
		       DXGI_FORMAT fmt)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    D3D12_TEXTURE_COPY_LOCATION src_loc;
    D3D12_RESOURCE_DESC desc;
    UINT num_rows;
    UINT64 row_size;
    UINT64 total;
    unsigned char *mapped = NULL;
    UINT y;
    UINT src_pitch = w * bpp;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = fmt;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ID3D12Device_GetCopyableFootprints(g_dev, &desc, 0, 1, 0,
				       &fp, &num_rows, &row_size, &total);
    if (FAILED(ID3D12Resource_Map(upload, 0, NULL, (void **)&mapped)))
	return;
    for (y = 0; y < h; y++)
	memcpy(mapped + fp.Offset + y * fp.Footprint.RowPitch,
	       (const unsigned char *)src + y * src_pitch, src_pitch);
    ID3D12Resource_Unmap(upload, 0, NULL);

    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource = dst;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.PlacedFootprint = fp;
    dst_loc.SubresourceIndex = 0;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource = upload;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(g_cmd, &dst_loc, 0, 0, 0,
						&src_loc, NULL);
}

static void init_d3d(HWND hwnd)
{
    IDXGIFactory4 *factory = NULL;
    IDXGISwapChain1 *swap1 = NULL;
    D3D12_COMMAND_QUEUE_DESC qd;
    DXGI_SWAP_CHAIN_DESC1 scd;
    HRESULT hr;
    UINT i;

    hr = CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&factory);
    if (FAILED(hr))
	I_Error("CreateDXGIFactory2 failed (0x%08lx)", (unsigned long)hr);

    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
			   &IID_ID3D12Device, (void **)&g_dev);
    if (FAILED(hr))
	I_Error("D3D12CreateDevice failed (0x%08lx)", (unsigned long)hr);

    memset(&qd, 0, sizeof(qd));
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = ID3D12Device_CreateCommandQueue(g_dev, &qd, &IID_ID3D12CommandQueue,
					 (void **)&g_queue);
    if (FAILED(hr))
	I_Error("CreateCommandQueue failed");

    hr = ID3D12Device_CreateCommandAllocator(g_dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
					     &IID_ID3D12CommandAllocator,
					     (void **)&g_alloc);
    if (FAILED(hr))
	I_Error("CreateCommandAllocator failed");

    hr = ID3D12Device_CreateCommandList(g_dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
					g_alloc, NULL,
					&IID_ID3D12GraphicsCommandList,
					(void **)&g_cmd);
    if (FAILED(hr))
	I_Error("CreateCommandList failed");
    ID3D12GraphicsCommandList_Close(g_cmd);

    hr = ID3D12Device_CreateFence(g_dev, 0, D3D12_FENCE_FLAG_NONE,
				  &IID_ID3D12Fence, (void **)&g_fence);
    if (FAILED(hr))
	I_Error("CreateFence failed");
    g_fence_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_fence_ev)
	I_Error("CreateEvent failed");

    memset(&scd, 0, sizeof(scd));
    scd.Width = WIN_W;
    scd.Height = WIN_H;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = FRAME_COUNT;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = IDXGIFactory4_CreateSwapChainForHwnd(factory, (IUnknown *)g_queue,
					      hwnd, &scd, NULL, NULL, &swap1);
    if (FAILED(hr))
	I_Error("CreateSwapChainForHwnd failed (0x%08lx)", (unsigned long)hr);
    hr = IDXGISwapChain1_QueryInterface(swap1, &IID_IDXGISwapChain3,
					(void **)&g_swap);
    IDXGISwapChain1_Release(swap1);
    if (FAILED(hr))
	I_Error("IDXGISwapChain3 query failed");
    IDXGIFactory4_MakeWindowAssociation(factory, hwnd, DXGI_MWA_NO_ALT_ENTER);
    IDXGIFactory4_Release(factory);

    for (i = 0; i < FRAME_COUNT; i++)
    {
	hr = IDXGISwapChain3_GetBuffer(g_swap, i, &IID_ID3D12Resource,
				       (void **)&g_bb[i]);
	if (FAILED(hr))
	    I_Error("GetBuffer failed");
	g_bb_state[i] = D3D12_RESOURCE_STATE_PRESENT;
    }

    g_tex_color = make_tex(GB_WIDTH, GB_HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM,
			   L"GB_Color");
    g_tex_depth = make_tex(GB_WIDTH, GB_HEIGHT, DXGI_FORMAT_R32_FLOAT,
			   L"GB_Depth");
    g_tex_normal = make_tex(GB_WIDTH, GB_HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM,
			    L"GB_Normal");
    g_tex_velocity = make_tex(GB_WIDTH, GB_HEIGHT, DXGI_FORMAT_R32G32_FLOAT,
			      L"GB_Velocity");
    g_up_color = make_upload(upload_bytes(GB_WIDTH, GB_HEIGHT, 4));
    g_up_depth = make_upload(upload_bytes(GB_WIDTH, GB_HEIGHT, 4));
    g_up_normal = make_upload(upload_bytes(GB_WIDTH, GB_HEIGHT, 4));
    g_up_velocity = make_upload(upload_bytes(GB_WIDTH, GB_HEIGHT, 8));
    g_up_present = make_upload(upload_bytes(WIN_W, WIN_H, 4));
    if (!g_tex_color || !g_tex_depth || !g_tex_normal || !g_tex_velocity ||
	!g_up_color || !g_up_depth || !g_up_normal || !g_up_velocity ||
	!g_up_present)
	I_Error("G-buffer textures failed");
}

void I_StartFrame(void)
{
    GB_BeginFrame();
}

void I_StartTic(void)
{
    MSG msg;
    POINT pt;
    RECT rc;
    int cx, cy;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
	if (msg.message == WM_QUIT)
	    I_Quit();
	TranslateMessage(&msg);
	DispatchMessageA(&msg);
    }

    if (g_hwnd && g_have_focus && g_mouse_grab)
    {
	GetClientRect(g_hwnd, &rc);
	cx = rc.right / 2;
	cy = rc.bottom / 2;
	GetCursorPos(&pt);
	ScreenToClient(g_hwnd, &pt);
	if (pt.x != cx || pt.y != cy)
	{
	    post_mouse((pt.x - cx) << 2, (cy - pt.y) << 2);
	    pt.x = cx;
	    pt.y = cy;
	    ClientToScreen(g_hwnd, &pt);
	    SetCursorPos(pt.x, pt.y);
	}
    }
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    UINT idx;
    ID3D12CommandList *lists[1];

    GB_ConvertColor(screens[0]);
    GB_EndFrame();
    GB_ComposePresent(g_present, WIN_W, WIN_H);

    wait_gpu();
    idx = IDXGISwapChain3_GetCurrentBackBufferIndex(g_swap);
    ID3D12CommandAllocator_Reset(g_alloc);
    ID3D12GraphicsCommandList_Reset(g_cmd, g_alloc, NULL);

    upload_tex(g_tex_color, g_up_color, GB_ColorRGBA(),
	       GB_WIDTH, GB_HEIGHT, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    upload_tex(g_tex_depth, g_up_depth, GB_Depth(),
	       GB_WIDTH, GB_HEIGHT, 4, DXGI_FORMAT_R32_FLOAT);
    upload_tex(g_tex_normal, g_up_normal, GB_NormalRGBA(),
	       GB_WIDTH, GB_HEIGHT, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    upload_tex(g_tex_velocity, g_up_velocity, GB_VelocityRG(),
	       GB_WIDTH, GB_HEIGHT, 8, DXGI_FORMAT_R32G32_FLOAT);

    barrier(g_bb[idx], &g_bb_state[idx], D3D12_RESOURCE_STATE_COPY_DEST);
    upload_tex(g_bb[idx], g_up_present, g_present,
	       WIN_W, WIN_H, 4, DXGI_FORMAT_B8G8R8A8_UNORM);
    barrier(g_bb[idx], &g_bb_state[idx], D3D12_RESOURCE_STATE_PRESENT);

    ID3D12GraphicsCommandList_Close(g_cmd);
    lists[0] = (ID3D12CommandList *)g_cmd;
    ID3D12CommandQueue_ExecuteCommandLists(g_queue, 1, lists);
    IDXGISwapChain3_Present(g_swap, 0, 0);
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette)
{
    int i;
    int r, g, b;

    for (i = 0; i < 256; i++)
    {
	r = gammatable[usegamma][*palette++];
	g = gammatable[usegamma][*palette++];
	b = gammatable[usegamma][*palette++];
	g_palette[i * 3 + 0] = (unsigned char)r;
	g_palette[i * 3 + 1] = (unsigned char)g;
	g_palette[i * 3 + 2] = (unsigned char)b;
    }
    GB_SetPaletteRGB(g_palette);
}

void I_ShutdownGraphics(void)
{
    grab_mouse(0);
    wait_gpu();
    Ngx_Shutdown();
    if (g_up_present) ID3D12Resource_Release(g_up_present);
    if (g_up_velocity) ID3D12Resource_Release(g_up_velocity);
    if (g_up_normal) ID3D12Resource_Release(g_up_normal);
    if (g_up_depth) ID3D12Resource_Release(g_up_depth);
    if (g_up_color) ID3D12Resource_Release(g_up_color);
    if (g_tex_velocity) ID3D12Resource_Release(g_tex_velocity);
    if (g_tex_normal) ID3D12Resource_Release(g_tex_normal);
    if (g_tex_depth) ID3D12Resource_Release(g_tex_depth);
    if (g_tex_color) ID3D12Resource_Release(g_tex_color);
    if (g_bb[0]) ID3D12Resource_Release(g_bb[0]);
    if (g_bb[1]) ID3D12Resource_Release(g_bb[1]);
    if (g_swap) IDXGISwapChain3_Release(g_swap);
    if (g_cmd) ID3D12GraphicsCommandList_Release(g_cmd);
    if (g_alloc) ID3D12CommandAllocator_Release(g_alloc);
    if (g_queue) ID3D12CommandQueue_Release(g_queue);
    if (g_fence) ID3D12Fence_Release(g_fence);
    if (g_fence_ev) CloseHandle(g_fence_ev);
    if (g_dev) ID3D12Device_Release(g_dev);
    g_up_present = g_up_velocity = g_up_normal = g_up_depth = g_up_color = NULL;
    g_tex_velocity = g_tex_normal = g_tex_depth = g_tex_color = NULL;
    g_bb[0] = g_bb[1] = NULL;
    g_swap = NULL;
    g_cmd = NULL;
    g_alloc = NULL;
    g_queue = NULL;
    g_fence = NULL;
    g_fence_ev = NULL;
    g_dev = NULL;
    if (g_hwnd)
    {
	DestroyWindow(g_hwnd);
	g_hwnd = NULL;
    }
    GB_Shutdown();
}

void I_InitGraphics(void)
{
    WNDCLASSA wc;
    RECT rc;
    DWORD style;

    GB_Init();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "WinDoom";
    RegisterClassA(&wc);

    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    rc.left = 0;
    rc.top = 0;
    rc.right = WIN_W;
    rc.bottom = WIN_H;
    AdjustWindowRect(&rc, style, FALSE);

    g_hwnd = CreateWindowA("WinDoom", "Freedoom", style,
			   CW_USEDEFAULT, CW_USEDEFAULT,
			   rc.right - rc.left, rc.bottom - rc.top,
			   NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd)
	I_Error("CreateWindow failed");

    init_d3d(g_hwnd);
    if (Ngx_Wanted())
	Ngx_Init(g_dev, g_queue);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    grab_mouse(1);
    fprintf(stderr, "I_InitGraphics: D3D12CreateDevice %dx%d (internal %dx%d)\n",
	    WIN_W, WIN_H, SCREENWIDTH, SCREENHEIGHT);
}
