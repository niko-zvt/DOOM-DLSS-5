/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "doomdef.h"
#include "doomtype.h"

#define boolean BOOLEAN_WIN32_AVOID
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
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

#define WIN_SCALE 4
#define WIN_W (SCREENWIDTH * WIN_SCALE)
#define WIN_H (SCREENHEIGHT * WIN_SCALE)

static HWND g_hwnd;
static int g_mouse_grab;
static int g_mouse_buttons;
static int g_have_focus = 1;

static ID3D11Device *g_dev;
static ID3D11DeviceContext *g_ctx;
static IDXGISwapChain *g_swap;
static ID3D11Texture2D *g_bb;
static ID3D11Texture2D *g_staging;
static ID3D11Texture2D *g_tex_color;
static ID3D11Texture2D *g_tex_depth;
static ID3D11Texture2D *g_tex_normal;
static ID3D11Texture2D *g_tex_velocity;

static unsigned char g_present[WIN_W * WIN_H * 4];
static unsigned char g_palette[768];

static const GUID kD3DDebugName =
    {0x429b8c22, 0x9188, 0x4b0c, {0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00}};

static void d3d_name(ID3D11DeviceChild *obj, const char *name)
{
    if (obj && name)
	ID3D11DeviceChild_SetPrivateData(obj, &kD3DDebugName,
					 (UINT)strlen(name), name);
}

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

static ID3D11Texture2D *make_tex(DXGI_FORMAT fmt, UINT bind, const char *name)
{
    D3D11_TEXTURE2D_DESC d;
    ID3D11Texture2D *tex = NULL;

    memset(&d, 0, sizeof(d));
    d.Width = GB_WIDTH;
    d.Height = GB_HEIGHT;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = bind;
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &d, NULL, &tex)))
	return NULL;
    d3d_name((ID3D11DeviceChild *)tex, name);
    return tex;
}

static void init_d3d(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    D3D11_TEXTURE2D_DESC td;
    HRESULT hr;

    memset(&sd, 0, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = WIN_W;
    sd.BufferDesc.Height = WIN_H;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = D3D11CreateDeviceAndSwapChain(
	NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
	NULL, 0, D3D11_SDK_VERSION, &sd,
	&g_swap, &g_dev, NULL, &g_ctx);
    if (FAILED(hr))
	I_Error("D3D11CreateDeviceAndSwapChain failed (0x%08lx)", (unsigned long)hr);

    hr = IDXGISwapChain_GetBuffer(g_swap, 0, &IID_ID3D11Texture2D, (void **)&g_bb);
    if (FAILED(hr))
	I_Error("GetBuffer failed");

    memset(&td, 0, sizeof(td));
    td.Width = WIN_W;
    td.Height = WIN_H;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &g_staging);
    if (FAILED(hr))
	I_Error("staging texture failed");

    g_tex_color = make_tex(DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, "GB_Color");
    g_tex_depth = make_tex(DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, "GB_Depth");
    g_tex_normal = make_tex(DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, "GB_Normal");
    g_tex_velocity = make_tex(DXGI_FORMAT_R32G32_FLOAT, D3D11_BIND_SHADER_RESOURCE, "GB_Velocity");
    if (!g_tex_color || !g_tex_depth || !g_tex_normal || !g_tex_velocity)
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

static void upload_gbuffers(void)
{
    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_tex_color,
					  0, NULL, GB_ColorRGBA(), GB_WIDTH * 4, 0);
    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_tex_depth,
					  0, NULL, GB_Depth(), GB_WIDTH * 4, 0);
    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_tex_normal,
					  0, NULL, GB_NormalRGBA(), GB_WIDTH * 4, 0);
    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_tex_velocity,
					  0, NULL, GB_VelocityRG(), GB_WIDTH * 8, 0);
}

void I_FinishUpdate(void)
{
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr;
    int y;

    GB_ConvertColor(screens[0]);
    GB_EndFrame();
    GB_ComposePresent(g_present, WIN_W, WIN_H);
    upload_gbuffers();

    hr = ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_staging, 0,
				 D3D11_MAP_WRITE, 0, &map);
    if (SUCCEEDED(hr))
    {
	for (y = 0; y < WIN_H; y++)
	    memcpy((unsigned char *)map.pData + y * map.RowPitch,
		   g_present + y * WIN_W * 4, WIN_W * 4);
	ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_staging, 0);
	ID3D11DeviceContext_CopyResource(g_ctx, (ID3D11Resource *)g_bb,
					 (ID3D11Resource *)g_staging);
    }
    IDXGISwapChain_Present(g_swap, 0, 0);
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
    if (g_tex_velocity) ID3D11Texture2D_Release(g_tex_velocity);
    if (g_tex_normal) ID3D11Texture2D_Release(g_tex_normal);
    if (g_tex_depth) ID3D11Texture2D_Release(g_tex_depth);
    if (g_tex_color) ID3D11Texture2D_Release(g_tex_color);
    if (g_staging) ID3D11Texture2D_Release(g_staging);
    if (g_bb) ID3D11Texture2D_Release(g_bb);
    if (g_swap) IDXGISwapChain_Release(g_swap);
    if (g_ctx) ID3D11DeviceContext_Release(g_ctx);
    if (g_dev) ID3D11Device_Release(g_dev);
    g_tex_velocity = g_tex_normal = g_tex_depth = g_tex_color = NULL;
    g_staging = g_bb = NULL;
    g_swap = NULL;
    g_ctx = NULL;
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
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    grab_mouse(1);
    fprintf(stderr, "I_InitGraphics: D3D11 %dx%d (internal %dx%d)\n",
	    WIN_W, WIN_H, SCREENWIDTH, SCREENHEIGHT);
}
