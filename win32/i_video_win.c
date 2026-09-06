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
#include "m_argv.h"
#include "ngx_dlss.h"
#include "anime4k.h"
#include "fsr2.h"
#include "png_export.h"

extern int viewheight;
extern boolean singletics;

#define WIN_SCALE 4
#define WIN_W (SCREENWIDTH * WIN_SCALE)
#define WIN_H (SCREENHEIGHT * WIN_SCALE)
#define FRAME_COUNT 2

static HWND g_hwnd;
static int g_mouse_grab;
static int g_mouse_buttons;
static int g_have_focus = 1;
static int g_insert_down;
static int g_insert_from_wnd;

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
static ID3D12Resource *g_tex_out;
static ID3D12Resource *g_tex_color_hi;
static ID3D12Resource *g_tex_depth_hi;
static ID3D12Resource *g_tex_velocity_hi;
static D3D12_RESOURCE_STATES g_st_color;
static D3D12_RESOURCE_STATES g_st_depth;
static D3D12_RESOURCE_STATES g_st_normal;
static D3D12_RESOURCE_STATES g_st_velocity;
static D3D12_RESOURCE_STATES g_st_out;
static D3D12_RESOURCE_STATES g_st_color_hi;
static D3D12_RESOURCE_STATES g_st_depth_hi;
static D3D12_RESOURCE_STATES g_st_velocity_hi;
static ID3D12Resource *g_up_color;
static ID3D12Resource *g_up_depth;
static ID3D12Resource *g_up_normal;
static ID3D12Resource *g_up_velocity;
static ID3D12Resource *g_up_color_hi;
static ID3D12Resource *g_up_depth_hi;
static ID3D12Resource *g_up_velocity_hi;
static ID3D12Resource *g_up_present;
static ID3D12Resource *g_readback;
static D3D12_RESOURCE_STATES g_bb_state[FRAME_COUNT];

static unsigned char g_present[WIN_W * WIN_H * 4];
static unsigned char g_palette[768];
static unsigned char g_color_hi[WIN_W * WIN_H * 4];
static float g_depth_hi[WIN_W * WIN_H];
static float g_vel_hi[WIN_W * WIN_H * 2];

/* -export <dir>: every presented frame -> <dir>\fNNNNNN.png */
static const char *g_export_dir;
static int g_export_frame;
static unsigned char g_export_bgr[WIN_W * WIN_H * 3];

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

static int is_insert_key(WPARAM vk, LPARAM lparam)
{
    UINT sc = (UINT)((lparam >> 16) & 0xFF);

    if (vk == VK_INSERT)
	return 1;
    /* Dedicated Insert is E0 52. Numpad 0 is 52 without the extended bit. */
    if (sc == 0x52 && (lparam & (1 << 24)))
	return 1;
    return 0;
}

int I_StatusBarVisible(void)
{
    return GB_HudVisible();
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
	if (is_insert_key(wparam, lparam))
	{
	    if (!(lparam & (1 << 30)))
	    {
		GB_ToggleHud();
		g_insert_from_wnd = 1;
	    }
	    return 0;
	}
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
	if (is_insert_key(wparam, lparam))
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

static void check_device(const char *where)
{
    HRESULT hr;

    if (!g_dev)
	return;
    hr = ID3D12Device_GetDeviceRemovedReason(g_dev);
    if (FAILED(hr))
	I_Error("D3D12 device removed at %s (0x%08lx)",
		where, (unsigned long)hr);
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
    check_device("wait_gpu");
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

static void uav_barrier(ID3D12Resource *res)
{
    D3D12_RESOURCE_BARRIER b;

    if (!res)
	return;
    memset(&b, 0, sizeof(b));
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    ID3D12GraphicsCommandList_ResourceBarrier(g_cmd, 1, &b);
}

static void copy_tex_to_tex(ID3D12Resource *src, ID3D12Resource *dst)
{
    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    D3D12_TEXTURE_COPY_LOCATION src_loc;

    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource = dst;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource = src;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(g_cmd, &dst_loc, 0, 0, 0,
						&src_loc, NULL);
}

static void copy_tex_rect(ID3D12Resource *src, ID3D12Resource *dst,
			  UINT src_x, UINT src_y, UINT dst_x, UINT dst_y,
			  UINT w, UINT h)
{
    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    D3D12_TEXTURE_COPY_LOCATION src_loc;
    D3D12_BOX box;

    if (!src || !dst || w < 1 || h < 1)
	return;
    if (src_x + w > (UINT)WIN_W || src_y + h > (UINT)WIN_H)
	return;
    if (dst_x + w > (UINT)WIN_W || dst_y + h > (UINT)WIN_H)
	return;
    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource = dst;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource = src;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    box.left = src_x;
    box.top = src_y;
    box.front = 0;
    box.right = src_x + w;
    box.bottom = src_y + h;
    box.back = 1;
    ID3D12GraphicsCommandList_CopyTextureRegion(g_cmd, &dst_loc, dst_x, dst_y,
						0, &src_loc, &box);
}

static void copy_tex_rows(ID3D12Resource *src, ID3D12Resource *dst,
			  UINT y, UINT h)
{
    copy_tex_rect(src, dst, 0, y, 0, y, (UINT)WIN_W, h);
}

static ID3D12Resource *make_tex(UINT w, UINT h, DXGI_FORMAT fmt,
				D3D12_RESOURCE_FLAGS flags,
				D3D12_RESOURCE_STATES state,
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
    desc.Flags = flags;
    if (FAILED(ID3D12Device_CreateCommittedResource(
	    g_dev, &heap, D3D12_HEAP_FLAG_NONE, &desc,
	    state, NULL,
	    &IID_ID3D12Resource, (void **)&res)))
	return NULL;
    if (name)
	ID3D12Object_SetName((ID3D12Object *)res, name);
    return res;
}

static ID3D12Resource *make_readback(UINT64 size)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *res = NULL;

    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_HEAP_TYPE_READBACK;
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
	    D3D12_RESOURCE_STATE_COPY_DEST, NULL,
	    &IID_ID3D12Resource, (void **)&res)))
	return NULL;
    return res;
}

static void copy_tex_to_buffer(ID3D12Resource *src, ID3D12Resource *dst,
			       UINT w, UINT h, DXGI_FORMAT fmt)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    D3D12_TEXTURE_COPY_LOCATION src_loc;
    D3D12_RESOURCE_DESC desc;
    UINT num_rows;
    UINT64 row_size;
    UINT64 total;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = fmt;
    desc.SampleDesc.Count = 1;
    ID3D12Device_GetCopyableFootprints(g_dev, &desc, 0, 1, 0,
				       &fp, &num_rows, &row_size, &total);
    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource = dst;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_loc.PlacedFootprint = fp;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource = src;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(g_cmd, &dst_loc, 0, 0, 0,
						&src_loc, NULL);
}

static int readback_to_present(UINT w, UINT h, DXGI_FORMAT fmt)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_RESOURCE_DESC desc;
    UINT num_rows;
    UINT64 row_size;
    UINT64 total;
    unsigned char *mapped = NULL;
    UINT y;
    UINT dst_pitch = w * 4;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = fmt;
    desc.SampleDesc.Count = 1;
    ID3D12Device_GetCopyableFootprints(g_dev, &desc, 0, 1, 0,
				       &fp, &num_rows, &row_size, &total);
    if (FAILED(ID3D12Resource_Map(g_readback, 0, NULL, (void **)&mapped)))
	return 0;
    for (y = 0; y < h; y++)
	memcpy(g_present + y * dst_pitch,
	       mapped + fp.Offset + y * fp.Footprint.RowPitch, dst_pitch);
    ID3D12Resource_Unmap(g_readback, 0, NULL);
    return 1;
}

/* Reads the B8G8R8A8 back buffer copy out of g_readback (GPU already
   idle), drops alpha and writes <dir>\fNNNNNN.png. Disables export on
   the first failure so a bad path does not spam every frame. */
static void export_frame_png(void)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_RESOURCE_DESC desc;
    UINT num_rows;
    UINT64 row_size;
    UINT64 total;
    unsigned char *mapped = NULL;
    char path[MAX_PATH];
    UINT x, y;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = WIN_W;
    desc.Height = WIN_H;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ID3D12Device_GetCopyableFootprints(g_dev, &desc, 0, 1, 0,
				       &fp, &num_rows, &row_size, &total);
    if (FAILED(ID3D12Resource_Map(g_readback, 0, NULL, (void **)&mapped)))
    {
	fprintf(stderr, "export: readback map failed, export disabled\n");
	g_export_dir = NULL;
	return;
    }
    for (y = 0; y < (UINT)WIN_H; y++)
    {
	const unsigned char *src = mapped + fp.Offset + y * fp.Footprint.RowPitch;
	unsigned char *dst = g_export_bgr + y * WIN_W * 3;

	for (x = 0; x < (UINT)WIN_W; x++)
	{
	    dst[x * 3 + 0] = src[x * 4 + 0];
	    dst[x * 3 + 1] = src[x * 4 + 1];
	    dst[x * 3 + 2] = src[x * 4 + 2];
	}
    }
    ID3D12Resource_Unmap(g_readback, 0, NULL);

    snprintf(path, sizeof(path), "%s\\f%06d.png", g_export_dir,
	     g_export_frame + 1);
    if (!Png_Write(path, WIN_W, WIN_H, g_export_bgr))
    {
	fprintf(stderr, "export: cannot write %s, export disabled\n", path);
	g_export_dir = NULL;
	return;
    }
    g_export_frame++;
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

static void overlay_hud_on_bb(ID3D12Resource *dst)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    D3D12_TEXTURE_COPY_LOCATION src_loc;
    D3D12_RESOURCE_DESC desc;
    D3D12_BOX box;
    UINT num_rows;
    UINT64 row_size;
    UINT64 total;
    unsigned char *mapped = NULL;
    const unsigned char *color;
    const float *depth;
    int sx, sy, x0, x1, dx, dy, x, y;

    color = GB_ColorRGBA();
    depth = GB_Depth();
    if (!dst || !color || !depth)
	return;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = WIN_W;
    desc.Height = WIN_H;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ID3D12Device_GetCopyableFootprints(g_dev, &desc, 0, 1, 0,
				       &fp, &num_rows, &row_size, &total);
    if (FAILED(ID3D12Resource_Map(g_up_present, 0, NULL, (void **)&mapped)))
	return;

    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource = dst;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource = g_up_present;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = fp;

    for (sy = 0; sy < GB_HEIGHT; sy++)
    {
	sx = 0;
	while (sx < GB_WIDTH)
	{
	    while (sx < GB_WIDTH &&
		   depth[sy * GB_WIDTH + sx] != 0.0f)
		sx++;
	    x0 = sx;
	    while (sx < GB_WIDTH &&
		   depth[sy * GB_WIDTH + sx] == 0.0f)
		sx++;
	    x1 = sx;
	    if (x1 <= x0)
		break;
	    dy = sy * WIN_SCALE;
	    for (y = 0; y < WIN_SCALE; y++)
	    {
		for (x = x0; x < x1; x++)
		{
		    const unsigned char *s = color + (sy * GB_WIDTH + x) * 4;
		    int i;

		    dx = x * WIN_SCALE;
		    for (i = 0; i < WIN_SCALE; i++)
		    {
			unsigned char *d = mapped + fp.Offset +
			    (UINT)(dy + y) * fp.Footprint.RowPitch +
			    (UINT)(dx + i) * 4;

			d[0] = s[2];
			d[1] = s[1];
			d[2] = s[0];
			d[3] = 255;
		    }
		}
	    }
	    box.left = (UINT)(x0 * WIN_SCALE);
	    box.top = (UINT)dy;
	    box.front = 0;
	    box.right = (UINT)(x1 * WIN_SCALE);
	    box.bottom = (UINT)(dy + WIN_SCALE);
	    box.back = 1;
	    ID3D12GraphicsCommandList_CopyTextureRegion(
		g_cmd, &dst_loc, box.left, box.top, 0, &src_loc, &box);
	}
    }
    ID3D12Resource_Unmap(g_up_present, 0, NULL);
}

static void blit_statusbar(ID3D12Resource *dst)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    D3D12_TEXTURE_COPY_LOCATION src_loc;
    D3D12_RESOURCE_DESC desc;
    D3D12_BOX box;
    UINT num_rows;
    UINT64 row_size;
    UINT64 total;
    unsigned char *mapped = NULL;
    const unsigned char *color;
    int x, y, sx, sy;
    int st_y = GB_HEIGHT - 32;
    int dst_y;

    if (!dst || viewheight <= 0 || viewheight >= GB_HEIGHT)
	return;
    /* Status bar is ST_Y=168, not viewheight (144). 3D sits at
     * viewwindowy..viewwindowy+viewheight-1 (12..155 at screenblocks 9). */
    st_y = SCREENHEIGHT - 32;
    dst_y = st_y * WIN_SCALE;
    if (dst_y >= WIN_H)
	return;
    color = GB_ColorRGBA();
    if (!color)
	return;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = WIN_W;
    desc.Height = WIN_H;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ID3D12Device_GetCopyableFootprints(g_dev, &desc, 0, 1, 0,
				       &fp, &num_rows, &row_size, &total);
    if (FAILED(ID3D12Resource_Map(g_up_present, 0, NULL, (void **)&mapped)))
	return;

    for (y = dst_y; y < WIN_H; y++)
    {
	sy = y / WIN_SCALE;
	for (x = 0; x < WIN_W; x++)
	{
	    const unsigned char *s;
	    unsigned char *d;

	    sx = x / WIN_SCALE;
	    s = color + (sy * GB_WIDTH + sx) * 4;
	    d = mapped + fp.Offset + (UINT)y * fp.Footprint.RowPitch +
		(UINT)x * 4;
	    d[0] = s[2];
	    d[1] = s[1];
	    d[2] = s[0];
	    d[3] = 255;
	}
    }
    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource = dst;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource = g_up_present;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = fp;
    box.left = 0;
    box.top = (UINT)dst_y;
    box.front = 0;
    box.right = WIN_W;
    box.bottom = WIN_H;
    box.back = 1;
    ID3D12GraphicsCommandList_CopyTextureRegion(
	g_cmd, &dst_loc, 0, (UINT)dst_y, 0, &src_loc, &box);
    ID3D12Resource_Unmap(g_up_present, 0, NULL);
}

static void nearest_upscale_ngx(void)
{
    const unsigned char *color = GB_ColorRGBA();
    const float *depth = GB_Depth();
    const float *vel = GB_VelocityRG();
    int x, y;

    if (!color || !depth || !vel)
	return;
    for (y = 0; y < WIN_H; y++)
    {
	int sy = y / WIN_SCALE;

	for (x = 0; x < WIN_W; x++)
	{
	    int sx = x / WIN_SCALE;
	    int si = sy * GB_WIDTH + sx;
	    int di = y * WIN_W + x;

	    g_color_hi[di * 4 + 0] = color[si * 4 + 0];
	    g_color_hi[di * 4 + 1] = color[si * 4 + 1];
	    g_color_hi[di * 4 + 2] = color[si * 4 + 2];
	    g_color_hi[di * 4 + 3] = 255;
	    if (depth[si] <= 0.0f)
	    {
		g_depth_hi[di] = 0.0f;
		g_vel_hi[di * 2 + 0] = 0.0f;
		g_vel_hi[di * 2 + 1] = 0.0f;
		continue;
	    }
	    g_depth_hi[di] = depth[si];
	    g_vel_hi[di * 2 + 0] = vel[si * 2 + 0];
	    g_vel_hi[di * 2 + 1] = vel[si * 2 + 1];
	}
    }
}

static int init_hi_res(void)
{
    g_st_color_hi = D3D12_RESOURCE_STATE_COPY_DEST;
    g_st_depth_hi = D3D12_RESOURCE_STATE_COPY_DEST;
    g_st_velocity_hi = D3D12_RESOURCE_STATE_COPY_DEST;
    g_tex_color_hi = make_tex(WIN_W, WIN_H, DXGI_FORMAT_R8G8B8A8_UNORM,
			      D3D12_RESOURCE_FLAG_NONE, g_st_color_hi,
			      L"GB_ColorHi");
    g_tex_depth_hi = make_tex(WIN_W, WIN_H, DXGI_FORMAT_R32_FLOAT,
			      D3D12_RESOURCE_FLAG_NONE, g_st_depth_hi,
			      L"GB_DepthHi");
    g_tex_velocity_hi = make_tex(WIN_W, WIN_H, DXGI_FORMAT_R32G32_FLOAT,
				 D3D12_RESOURCE_FLAG_NONE, g_st_velocity_hi,
				 L"GB_VelocityHi");
    g_up_color_hi = make_upload(upload_bytes(WIN_W, WIN_H, 4));
    g_up_depth_hi = make_upload(upload_bytes(WIN_W, WIN_H, 4));
    g_up_velocity_hi = make_upload(upload_bytes(WIN_W, WIN_H, 8));
    return g_tex_color_hi && g_tex_depth_hi && g_tex_velocity_hi &&
	g_up_color_hi && g_up_depth_hi && g_up_velocity_hi;
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

    g_st_color = g_st_depth = g_st_normal = g_st_velocity =
	D3D12_RESOURCE_STATE_COPY_DEST;
    g_st_out = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    g_tex_color = make_tex(GB_WIDTH, GB_HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM,
			   D3D12_RESOURCE_FLAG_NONE, g_st_color, L"GB_Color");
    g_tex_depth = make_tex(GB_WIDTH, GB_HEIGHT, DXGI_FORMAT_R32_FLOAT,
			   D3D12_RESOURCE_FLAG_NONE, g_st_depth, L"GB_Depth");
    g_tex_normal = make_tex(GB_WIDTH, GB_HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM,
			    D3D12_RESOURCE_FLAG_NONE, g_st_normal, L"GB_Normal");
    g_tex_velocity = make_tex(GB_WIDTH, GB_HEIGHT, DXGI_FORMAT_R32G32_FLOAT,
			      D3D12_RESOURCE_FLAG_NONE, g_st_velocity,
			      L"GB_Velocity");
    g_tex_out = make_tex(WIN_W, WIN_H, DXGI_FORMAT_B8G8R8A8_UNORM,
			 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			 g_st_out, L"GB_Output");
    g_up_color = make_upload(upload_bytes(GB_WIDTH, GB_HEIGHT, 4));
    g_up_depth = make_upload(upload_bytes(GB_WIDTH, GB_HEIGHT, 4));
    g_up_normal = make_upload(upload_bytes(GB_WIDTH, GB_HEIGHT, 4));
    g_up_velocity = make_upload(upload_bytes(GB_WIDTH, GB_HEIGHT, 8));
    g_up_present = make_upload(upload_bytes(WIN_W, WIN_H, 4));
    g_readback = make_readback(upload_bytes(WIN_W, WIN_H, 4));
    if (!g_tex_color || !g_tex_depth || !g_tex_normal || !g_tex_velocity ||
	!g_tex_out ||
	!g_up_color || !g_up_depth || !g_up_normal || !g_up_velocity ||
	!g_up_present || !g_readback)
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

    {
	int down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;

	if (g_have_focus && down && !g_insert_down && !g_insert_from_wnd)
	    GB_ToggleHud();
	if (!down)
	    g_insert_from_wnd = 0;
	g_insert_down = down;
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
    int used_ngx = 0;
    int used_a4k = 0;
    int used_fsr2 = 0;
    int reset;

    GB_ConvertColor(screens[0]);
    GB_EndFrame();
    reset = GB_ConsumeReset();

    wait_gpu();
    idx = IDXGISwapChain3_GetCurrentBackBufferIndex(g_swap);
    ID3D12CommandAllocator_Reset(g_alloc);
    ID3D12GraphicsCommandList_Reset(g_cmd, g_alloc, NULL);

    barrier(g_tex_color, &g_st_color, D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(g_tex_depth, &g_st_depth, D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(g_tex_normal, &g_st_normal, D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(g_tex_velocity, &g_st_velocity, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_tex(g_tex_color, g_up_color, GB_ColorRGBA(),
	       GB_WIDTH, GB_HEIGHT, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    upload_tex(g_tex_depth, g_up_depth, GB_Depth(),
	       GB_WIDTH, GB_HEIGHT, 4, DXGI_FORMAT_R32_FLOAT);
    upload_tex(g_tex_normal, g_up_normal, GB_NormalRGBA(),
	       GB_WIDTH, GB_HEIGHT, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    upload_tex(g_tex_velocity, g_up_velocity, GB_VelocityRG(),
	       GB_WIDTH, GB_HEIGHT, 8, DXGI_FORMAT_R32G32_FLOAT);

    if (Ngx_Ready() && Ngx_ShowEvalOutput() &&
	GB_GetDebugView() == GB_VIEW_COLOR &&
	GB_HasScenePixels())
    {
	if (Ngx_WantsHiRes() && g_tex_color_hi)
	{
	    nearest_upscale_ngx();
	    barrier(g_tex_color_hi, &g_st_color_hi,
		    D3D12_RESOURCE_STATE_COPY_DEST);
	    barrier(g_tex_depth_hi, &g_st_depth_hi,
		    D3D12_RESOURCE_STATE_COPY_DEST);
	    barrier(g_tex_velocity_hi, &g_st_velocity_hi,
		    D3D12_RESOURCE_STATE_COPY_DEST);
	    upload_tex(g_tex_color_hi, g_up_color_hi, g_color_hi,
		       WIN_W, WIN_H, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
	    upload_tex(g_tex_depth_hi, g_up_depth_hi, g_depth_hi,
		       WIN_W, WIN_H, 4, DXGI_FORMAT_R32_FLOAT);
	    upload_tex(g_tex_velocity_hi, g_up_velocity_hi, g_vel_hi,
		       WIN_W, WIN_H, 8, DXGI_FORMAT_R32G32_FLOAT);
	    barrier(g_tex_color_hi, &g_st_color_hi,
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	    barrier(g_tex_depth_hi, &g_st_depth_hi,
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	    barrier(g_tex_velocity_hi, &g_st_velocity_hi,
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	    barrier(g_tex_normal, &g_st_normal,
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	    barrier(g_tex_out, &g_st_out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	    used_ngx = Ngx_Evaluate(g_cmd, g_tex_color_hi, g_tex_depth_hi,
				    g_tex_velocity_hi, g_tex_normal, g_tex_out,
				    reset);
	}
	else
	{
	    barrier(g_tex_color, &g_st_color,
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	    barrier(g_tex_depth, &g_st_depth,
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	    barrier(g_tex_velocity, &g_st_velocity,
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	    barrier(g_tex_normal, &g_st_normal,
		    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	    barrier(g_tex_out, &g_st_out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	    used_ngx = Ngx_Evaluate(g_cmd, g_tex_color, g_tex_depth,
				    g_tex_velocity, g_tex_normal, g_tex_out,
				    reset);
	}
    }

    if (used_ngx && !Ngx_ShowEvalOutput())
	used_ngx = 0;

    if (!used_ngx && Fsr2_Ready() &&
	GB_GetDebugView() == GB_VIEW_COLOR &&
	GB_HasScenePixels())
    {
	barrier(g_tex_color, &g_st_color,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	barrier(g_tex_depth, &g_st_depth,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	barrier(g_tex_velocity, &g_st_velocity,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	barrier(g_tex_out, &g_st_out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	used_fsr2 = Fsr2_Evaluate(g_cmd, g_tex_color, g_tex_depth,
				  g_tex_velocity, g_tex_out, reset);
    }

    if (!used_ngx && !used_fsr2 && Anime4K_Ready() &&
	GB_GetDebugView() == GB_VIEW_COLOR)
    {
	barrier(g_tex_color, &g_st_color,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	barrier(g_tex_out, &g_st_out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	used_a4k = Anime4K_Evaluate(g_cmd, g_tex_color, g_tex_out);
    }

    if (used_ngx || used_a4k || used_fsr2)
    {
	int hud_y = (SCREENHEIGHT - 32) * WIN_SCALE;

	uav_barrier(g_tex_out);
	barrier(g_tex_out, &g_st_out, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barrier(g_bb[idx], &g_bb_state[idx], D3D12_RESOURCE_STATE_COPY_DEST);
	if (GB_HudVisible() && hud_y > 0 && hud_y < WIN_H)
	    copy_tex_rows(g_tex_out, g_bb[idx], 0, (UINT)hud_y);
	else
	    copy_tex_to_tex(g_tex_out, g_bb[idx]);
	if (GB_HudVisible())
	    blit_statusbar(g_bb[idx]);
    }
    else
    {
	GB_ComposePresent(g_present, WIN_W, WIN_H);
	barrier(g_bb[idx], &g_bb_state[idx], D3D12_RESOURCE_STATE_COPY_DEST);
	upload_tex(g_bb[idx], g_up_present, g_present,
		   WIN_W, WIN_H, 4, DXGI_FORMAT_B8G8R8A8_UNORM);
	if (GB_HudVisible())
	    blit_statusbar(g_bb[idx]);
    }

    if (g_export_dir)
    {
	barrier(g_bb[idx], &g_bb_state[idx], D3D12_RESOURCE_STATE_COPY_SOURCE);
	copy_tex_to_buffer(g_bb[idx], g_readback, WIN_W, WIN_H,
			   DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    barrier(g_bb[idx], &g_bb_state[idx], D3D12_RESOURCE_STATE_PRESENT);
    ID3D12GraphicsCommandList_Close(g_cmd);
    lists[0] = (ID3D12CommandList *)g_cmd;
    ID3D12CommandQueue_ExecuteCommandLists(g_queue, 1, lists);
    check_device("ExecuteCommandLists");
    if (g_export_dir)
    {
	wait_gpu();
	export_frame_png();
    }
    IDXGISwapChain3_Present(g_swap, 0, 0);
    check_device("Present");
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
    if (g_export_frame > 0)
	fprintf(stderr, "export: %d frames written\n", g_export_frame);
    Png_Shutdown();
    Anime4K_Shutdown();
    Fsr2_Shutdown();
    Ngx_Shutdown();
    if (g_readback) ID3D12Resource_Release(g_readback);
    if (g_up_present) ID3D12Resource_Release(g_up_present);
    if (g_up_velocity_hi) ID3D12Resource_Release(g_up_velocity_hi);
    if (g_up_depth_hi) ID3D12Resource_Release(g_up_depth_hi);
    if (g_up_color_hi) ID3D12Resource_Release(g_up_color_hi);
    if (g_up_velocity) ID3D12Resource_Release(g_up_velocity);
    if (g_up_normal) ID3D12Resource_Release(g_up_normal);
    if (g_up_depth) ID3D12Resource_Release(g_up_depth);
    if (g_up_color) ID3D12Resource_Release(g_up_color);
    if (g_tex_velocity_hi) ID3D12Resource_Release(g_tex_velocity_hi);
    if (g_tex_depth_hi) ID3D12Resource_Release(g_tex_depth_hi);
    if (g_tex_color_hi) ID3D12Resource_Release(g_tex_color_hi);
    if (g_tex_out) ID3D12Resource_Release(g_tex_out);
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
    g_readback = g_up_present = g_up_velocity = g_up_normal = g_up_depth = g_up_color = NULL;
    g_up_velocity_hi = g_up_depth_hi = g_up_color_hi = NULL;
    g_tex_out = g_tex_velocity = g_tex_normal = g_tex_depth = g_tex_color = NULL;
    g_tex_velocity_hi = g_tex_depth_hi = g_tex_color_hi = NULL;
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
    if (M_CheckParm("-depth"))
	GB_SetDebugView(GB_VIEW_DEPTH);
    else if (M_CheckParm("-normal"))
	GB_SetDebugView(GB_VIEW_NORMAL);
    else if (M_CheckParm("-velocity"))
	GB_SetDebugView(GB_VIEW_VELOCITY);
    else if (M_CheckParm("-color"))
	GB_SetDebugView(GB_VIEW_COLOR);

    {
	int p = M_CheckParm("-export");

	if (p && p < myargc - 1)
	{
	    g_export_dir = myargv[p + 1];
	    g_export_frame = 0;
	    if (!CreateDirectoryA(g_export_dir, NULL) &&
		GetLastError() != ERROR_ALREADY_EXISTS)
		I_Error("-export: cannot create %s", g_export_dir);
	    /* One tic per presented frame: the PNG sequence is a strict
	       35 fps timeline no matter how long encoding takes. */
	    singletics = true;
	    fprintf(stderr, "export: PNG frames -> %s\n", g_export_dir);
	}
    }

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
    if (Ngx_WantsHiRes() && !init_hi_res())
	I_Error("NGX hi-res G-buffers failed");
    if (!Ngx_Ready() && Fsr2_Wanted())
	Fsr2_Init(g_dev);
    if (!Ngx_Ready() && !Fsr2_Ready())
	Anime4K_Init(g_dev);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    grab_mouse(1);
    fprintf(stderr, "I_InitGraphics: D3D12CreateDevice %dx%d (internal %dx%d)\n",
	    WIN_W, WIN_H, SCREENWIDTH, SCREENHEIGHT);
    if (Anime4K_Ready())
	fprintf(stderr, "present mode: anime4k-fast\n");
    else if (Fsr2_Ready())
	fprintf(stderr, "present mode: fsr2\n");
    else if (Ngx_WantsHiRes())
	fprintf(stderr, "present mode: dlss5-dlaa\n");
    else if (Ngx_Ready())
	fprintf(stderr, "present mode: dlss-upscale\n");
    else
	fprintf(stderr, "present mode: nearest\n");
}
