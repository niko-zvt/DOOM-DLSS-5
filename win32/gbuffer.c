/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "gbuffer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "r_local.h"
#include "tables.h"
#include "v_video.h"

#define GB_PIX   (GB_WIDTH * GB_HEIGHT)
#define GB_FAR_Z 8192.0f

static unsigned char gb_color[GB_PIX * 4];
static float         gb_depth[GB_PIX];
static unsigned char gb_normal[GB_PIX * 4];
static float         gb_velocity[GB_PIX * 2];
static float         gb_obj_du[GB_PIX];
static float         gb_obj_dv[GB_PIX];
static unsigned char gb_palette[256 * 3];

static float gb_col_z;
static float gb_col_nx, gb_col_ny, gb_col_nz;
static float gb_col_du, gb_col_dv;
static int   gb_col_x = -1;
static int   gb_debug_view = GB_VIEW_COLOR;
static int   gb_hud_visible = 1;
static int   gb_reset;

static int     gb_have_prev;
static fixed_t gb_prev_viewx;
static fixed_t gb_prev_viewy;
static fixed_t gb_prev_viewz;
static angle_t gb_prev_viewangle;

static void gb_clear_aux(void)
{
    int i;

    memset(gb_depth, 0, sizeof(gb_depth));
    memset(gb_normal, 0, sizeof(gb_normal));
    memset(gb_velocity, 0, sizeof(gb_velocity));
    memset(gb_obj_du, 0, sizeof(gb_obj_du));
    memset(gb_obj_dv, 0, sizeof(gb_obj_dv));
    for (i = 0; i < GB_PIX; i++)
	gb_normal[i * 4 + 2] = 128;
}

void GB_Init(void)
{
    memset(gb_color, 0, sizeof(gb_color));
    gb_clear_aux();
    gb_have_prev = 0;
    gb_reset = 1;
    gb_debug_view = GB_VIEW_COLOR;
    gb_hud_visible = 1;
    gb_col_du = 0.0f;
    gb_col_dv = 0.0f;
}

void GB_Shutdown(void)
{
}

void GB_BeginFrame(void)
{
    gb_clear_aux();
    gb_col_x = -1;
    gb_col_du = 0.0f;
    gb_col_dv = 0.0f;
}

void GB_SetColumn(int x, float z, float nx, float ny, float nz, int kind)
{
    (void)kind;
    gb_col_x = x;
    gb_col_z = z;
    gb_col_nx = nx;
    gb_col_ny = ny;
    gb_col_nz = nz;
    gb_col_du = 0.0f;
    gb_col_dv = 0.0f;
}

void GB_SetObjectMotion(float du, float dv)
{
    gb_col_du = du;
    gb_col_dv = dv;
}

void GB_RequestReset(void)
{
    gb_reset = 1;
    gb_have_prev = 0;
}

int GB_ConsumeReset(void)
{
    int r = gb_reset;
    gb_reset = 0;
    return r;
}

/* Screen pixel of view-space (vx, vy). Same address as screens[0]. */
static int gb_screen_x(int vx)
{
    if (vx >= 0 && columnofs[vx] >= 0 && columnofs[vx] < GB_WIDTH)
	return columnofs[vx];
    return vx + viewwindowx;
}

static int gb_screen_y(int vy)
{
    if (vy >= 0 && ylookup[vy] && screens[0])
	return (int)(ylookup[vy] - screens[0]) / SCREENWIDTH;
    return vy + viewwindowy;
}

static void gb_write_pixel(int x, int y, float z, float nx, float ny, float nz)
{
    int i;
    int r, g, b;

    if ((unsigned)x >= (unsigned)GB_WIDTH || (unsigned)y >= (unsigned)GB_HEIGHT)
	return;

    i = y * GB_WIDTH + x;
    gb_depth[i] = z;
    gb_obj_du[i] = gb_col_du;
    gb_obj_dv[i] = gb_col_dv;

    r = (int)((nx * 0.5f + 0.5f) * 255.0f + 0.5f);
    g = (int)((ny * 0.5f + 0.5f) * 255.0f + 0.5f);
    b = (int)((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    gb_normal[i * 4 + 0] = (unsigned char)r;
    gb_normal[i * 4 + 1] = (unsigned char)g;
    gb_normal[i * 4 + 2] = (unsigned char)b;
    gb_normal[i * 4 + 3] = 255;
}

void GB_WriteColumn(int x, int yl, int yh)
{
    int y;
    int sx;

    if (x != gb_col_x)
	return;
    if (yl < 0)
	yl = 0;
    if (viewheight > 0 && yh >= viewheight)
	yh = viewheight - 1;
    sx = gb_screen_x(x);
    for (y = yl; y <= yh; y++)
	gb_write_pixel(sx, gb_screen_y(y), gb_col_z,
		       gb_col_nx, gb_col_ny, gb_col_nz);
}

void GB_WriteSpan(int y, int x1, int x2, float z, float nx, float ny, float nz)
{
    int x;
    int sy;

    if (y < 0)
	return;
    if (viewheight > 0 && y >= viewheight)
	return;
    if (x1 < 0)
	x1 = 0;
    if (viewwidth > 0 && x2 >= viewwidth)
	x2 = viewwidth - 1;
    gb_col_du = 0.0f;
    gb_col_dv = 0.0f;
    sy = gb_screen_y(y);
    for (x = x1; x <= x2; x++)
	gb_write_pixel(gb_screen_x(x), sy, z, nx, ny, nz);
}

static float gb_bam_to_rad(angle_t a)
{
    return (float)((double)a * (6.283185307179586 / 4294967296.0));
}

static void gb_copy_pixel(int dx, int dy, int sx, int sy, int copy_color)
{
    int di;
    int si;

    if ((unsigned)dx >= (unsigned)GB_WIDTH ||
	(unsigned)dy >= (unsigned)GB_HEIGHT ||
	(unsigned)sx >= (unsigned)GB_WIDTH ||
	(unsigned)sy >= (unsigned)GB_HEIGHT)
	return;
    di = dy * GB_WIDTH + dx;
    si = sy * GB_WIDTH + sx;
    gb_depth[di] = gb_depth[si];
    gb_obj_du[di] = gb_obj_du[si];
    gb_obj_dv[di] = gb_obj_dv[si];
    /* Bezel / HUD are static 2D overlays: never inherit scene motion. */
    gb_velocity[di * 2 + 0] = 0.0f;
    gb_velocity[di * 2 + 1] = 0.0f;
    memcpy(gb_normal + di * 4, gb_normal + si * 4, 4);
    if (copy_color)
	memcpy(gb_color + di * 4, gb_color + si * 4, 4);
}

static void gb_view_rect(int *x0, int *y0, int *x1, int *y1)
{
    int vw = viewwidth;
    int vh = viewheight;

    if (vw < 1)
	vw = GB_WIDTH;
    if (vh < 1)
	vh = GB_HEIGHT;
    *x0 = gb_screen_x(0);
    *y0 = gb_screen_y(0);
    *x1 = *x0 + vw - 1;
    *y1 = *y0 + vh - 1;
    if (*x0 < 0)
	*x0 = 0;
    if (*y0 < 0)
	*y0 = 0;
    if (*x1 >= GB_WIDTH)
	*x1 = GB_WIDTH - 1;
    if (*y1 >= GB_HEIGHT)
	*y1 = GB_HEIGHT - 1;
}

static void gb_pad_view_edges(void)
{
    int x, y;
    int x0, y0, x1, y1;
    int ymax;
    int copy_color;

    gb_view_rect(&x0, &y0, &x1, &y1);
    copy_color = !gb_hud_visible;
    ymax = gb_hud_visible ? (SCREENHEIGHT - 32) : GB_HEIGHT;
    if (ymax > GB_HEIGHT)
	ymax = GB_HEIGHT;
    for (y = 0; y < ymax; y++)
    {
	for (x = 0; x < GB_WIDTH; x++)
	{
	    int cx = x;
	    int cy = y;

	    if (x >= x0 && x <= x1 && y >= y0 && y <= y1)
		continue;
	    if (cx < x0)
		cx = x0;
	    if (cx > x1)
		cx = x1;
	    if (cy < y0)
		cy = y0;
	    if (cy > y1)
		cy = y1;
	    gb_copy_pixel(x, y, cx, cy, copy_color);
	}
    }
}

void GB_EndFrame(void)
{
    int x, y;
    int x0, y0, x1, y1;
    float cur_x, cur_y, cur_z;
    float prev_x, prev_y, prev_z;
    float cur_c, cur_s, prev_c, prev_s;
    float proj;

    gb_view_rect(&x0, &y0, &x1, &y1);
    cur_x = (float)viewx / 65536.0f;
    cur_y = (float)viewy / 65536.0f;
    cur_z = (float)viewz / 65536.0f;
    cur_c = (float)cos(gb_bam_to_rad(viewangle));
    cur_s = (float)sin(gb_bam_to_rad(viewangle));
    proj = (float)projection / 65536.0f;
    if (proj < 1.0f)
	proj = (float)centerx;

    if (gb_have_prev)
    {
	prev_x = (float)gb_prev_viewx / 65536.0f;
	prev_y = (float)gb_prev_viewy / 65536.0f;
	prev_z = (float)gb_prev_viewz / 65536.0f;
	prev_c = (float)cos(gb_bam_to_rad(gb_prev_viewangle));
	prev_s = (float)sin(gb_bam_to_rad(gb_prev_viewangle));

	for (y = 0; y < GB_HEIGHT; y++)
	{
	    for (x = 0; x < GB_WIDTH; x++)
	    {
		int i = y * GB_WIDTH + x;
		int vx = x - x0;
		int vy = y - y0;
		float z = gb_depth[i];
		angle_t ray;
		float rc, rs;
		float wx, wy, wz;
		float relx, rely, relz;
		float vz, vxcam;
		float prev_sx, prev_sy;

		if (x < x0 || x > x1 || y < y0 || y > y1)
		{
		    gb_velocity[i * 2 + 0] = 0.0f;
		    gb_velocity[i * 2 + 1] = 0.0f;
		    continue;
		}
		if (z <= 0.0f || z >= GB_FAR_Z)
		{
		    gb_velocity[i * 2 + 0] = 0.0f;
		    gb_velocity[i * 2 + 1] = 0.0f;
		    continue;
		}
		if ((unsigned)vx >= (unsigned)SCREENWIDTH)
		{
		    gb_velocity[i * 2 + 0] = 0.0f;
		    gb_velocity[i * 2 + 1] = 0.0f;
		    continue;
		}

		/* gb_depth is the view-axis depth (projection / scale), so the
		   distance along the ray is z / cos(column angle). */
		ray = viewangle + xtoviewangle[vx];
		rc = (float)cos(gb_bam_to_rad(ray));
		rs = (float)sin(gb_bam_to_rad(ray));
		{
		    float ct = (float)cos(gb_bam_to_rad(xtoviewangle[vx]));
		    float d = (ct > 0.01f) ? z / ct : z;
		    wx = cur_x + rc * d;
		    wy = cur_y + rs * d;
		}
		wz = cur_z + ((float)(centery - vy) * z) / proj;

		relx = wx - prev_x;
		rely = wy - prev_y;
		relz = wz - prev_z;
		vz = relx * prev_c + rely * prev_s;
		vxcam = -relx * prev_s + rely * prev_c;
		if (vz < 1.0f)
		{
		    gb_velocity[i * 2 + 0] = 0.0f;
		    gb_velocity[i * 2 + 1] = 0.0f;
		    continue;
		}

		/* vxcam is positive to the LEFT (DOOM angles grow CCW,
		   xtoviewangle[0] is the left edge), so it moves screen x down. */
		prev_sx = (float)centerx - vxcam * (proj / vz);
		prev_sy = (float)centery - relz * (proj / vz);
		/* NGX / FSR2 convention: vector from the current pixel to where
		   it was in the previous frame (prev - cur), low-res pixels. */
		gb_velocity[i * 2 + 0] = prev_sx - (float)vx + gb_obj_du[i];
		gb_velocity[i * 2 + 1] = prev_sy - (float)vy + gb_obj_dv[i];
	    }
	}
    }

    (void)cur_c;
    (void)cur_s;
    gb_prev_viewx = viewx;
    gb_prev_viewy = viewy;
    gb_prev_viewz = viewz;
    gb_prev_viewangle = viewangle;
    gb_have_prev = 1;
    gb_pad_view_edges();
}

void GB_SetPaletteRGB(const unsigned char *rgb768)
{
    memcpy(gb_palette, rgb768, 256 * 3);
}

void GB_ConvertColor(const unsigned char *src8)
{
    int i;

    for (i = 0; i < GB_PIX; i++)
    {
	const unsigned char *p = gb_palette + src8[i] * 3;
	gb_color[i * 4 + 0] = p[0];
	gb_color[i * 4 + 1] = p[1];
	gb_color[i * 4 + 2] = p[2];
	gb_color[i * 4 + 3] = 255;
    }
}

void GB_SetDebugView(int view)
{
    if (view < GB_VIEW_COLOR)
	view = GB_VIEW_COLOR;
    if (view > GB_VIEW_VELOCITY)
	view = GB_VIEW_VELOCITY;
    gb_debug_view = view;
}

int GB_GetDebugView(void)
{
    return gb_debug_view;
}

void GB_ToggleHud(void)
{
    gb_hud_visible = !gb_hud_visible;
    GB_RequestReset();
}

int GB_HudVisible(void)
{
    return gb_hud_visible;
}

const unsigned char *GB_ColorRGBA(void)
{
    return gb_color;
}

const float *GB_Depth(void)
{
    return gb_depth;
}

const unsigned char *GB_NormalRGBA(void)
{
    return gb_normal;
}

const float *GB_VelocityRG(void)
{
    return gb_velocity;
}

static unsigned char gb_clamp_u8(int v)
{
    if (v < 0)
	return 0;
    if (v > 255)
	return 255;
    return (unsigned char)v;
}

void GB_ComposePresent(unsigned char *dst_bgra, int dst_w, int dst_h)
{
    int x, y;
    int sx, sy;
    int src;

    if (dst_w < 1 || dst_h < 1)
	return;

    for (y = 0; y < dst_h; y++)
    {
	sy = y * GB_HEIGHT / dst_h;
	for (x = 0; x < dst_w; x++)
	{
	    unsigned char *d;
	    unsigned char r = 0, g = 0, b = 0;

	    sx = x * GB_WIDTH / dst_w;
	    src = sy * GB_WIDTH + sx;
	    switch (gb_debug_view)
	    {
	      case GB_VIEW_DEPTH:
	      {
		  float z = gb_depth[src];
		  float t = (z <= 0.0f) ? 0.0f : (1.0f / (1.0f + z / 256.0f));
		  r = g = b = gb_clamp_u8((int)(t * 255.0f + 0.5f));
		  break;
	      }
	      case GB_VIEW_NORMAL:
		  r = gb_normal[src * 4 + 0];
		  g = gb_normal[src * 4 + 1];
		  b = gb_normal[src * 4 + 2];
		  break;
	      case GB_VIEW_VELOCITY:
		  r = gb_clamp_u8((int)(128.0f + gb_velocity[src * 2 + 0] * 8.0f));
		  g = gb_clamp_u8((int)(128.0f + gb_velocity[src * 2 + 1] * 8.0f));
		  b = 128;
		  break;
	      default:
		  r = gb_color[src * 4 + 0];
		  g = gb_color[src * 4 + 1];
		  b = gb_color[src * 4 + 2];
		  break;
	    }

	    d = dst_bgra + (y * dst_w + x) * 4;
	    d[0] = b;
	    d[1] = g;
	    d[2] = r;
            d[3] = 255;
	}
    }
}

int GB_HasScenePixels(void)
{
    int i;

    for (i = 0; i < GB_PIX; i++)
    {
	if (gb_depth[i] > 0.0f && gb_depth[i] < GB_FAR_Z)
	    return 1;
    }
    return 0;
}

void GB_OverlayHud(unsigned char *dst_bgra, int dst_w, int dst_h)
{
    int x, y;
    int sx, sy;
    int src;

    if (!dst_bgra || dst_w < 1 || dst_h < 1)
	return;

    for (y = 0; y < dst_h; y++)
    {
	sy = y * GB_HEIGHT / dst_h;
	for (x = 0; x < dst_w; x++)
	{
	    unsigned char *d;

	    sx = x * GB_WIDTH / dst_w;
	    src = sy * GB_WIDTH + sx;
	    if (gb_depth[src] != 0.0f)
		continue;
	    d = dst_bgra + (y * dst_w + x) * 4;
	    d[0] = gb_color[src * 4 + 2];
	    d[1] = gb_color[src * 4 + 1];
	    d[2] = gb_color[src * 4 + 0];
	    d[3] = 255;
	}
    }
}
