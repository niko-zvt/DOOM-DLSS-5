/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "gbuffer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "r_local.h"
#include "tables.h"

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

    if (x != gb_col_x)
	return;
    if (yl < 0)
	yl = 0;
    if (yh >= GB_HEIGHT)
	yh = GB_HEIGHT - 1;
    for (y = yl; y <= yh; y++)
	gb_write_pixel(x, y, gb_col_z, gb_col_nx, gb_col_ny, gb_col_nz);
}

void GB_WriteSpan(int y, int x1, int x2, float z, float nx, float ny, float nz)
{
    int x;

    if (x1 < 0)
	x1 = 0;
    if (x2 >= GB_WIDTH)
	x2 = GB_WIDTH - 1;
    gb_col_du = 0.0f;
    gb_col_dv = 0.0f;
    for (x = x1; x <= x2; x++)
	gb_write_pixel(x, y, z, nx, ny, nz);
}

static float gb_bam_to_rad(angle_t a)
{
    return (float)((double)a * (6.283185307179586 / 4294967296.0));
}

void GB_EndFrame(void)
{
    int x, y;
    float cur_x, cur_y, cur_z;
    float prev_x, prev_y, prev_z;
    float cur_c, cur_s, prev_c, prev_s;
    float proj;

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
		float z = gb_depth[i];
		angle_t ray;
		float rc, rs;
		float wx, wy, wz;
		float relx, rely, relz;
		float vz, vx;
		float prev_sx, prev_sy;

		if (z <= 0.0f || z >= GB_FAR_Z)
		{
		    gb_velocity[i * 2 + 0] = 0.0f;
		    gb_velocity[i * 2 + 1] = 0.0f;
		    continue;
		}

		ray = viewangle + xtoviewangle[x];
		rc = (float)cos(gb_bam_to_rad(ray));
		rs = (float)sin(gb_bam_to_rad(ray));
		wx = cur_x + rc * z;
		wy = cur_y + rs * z;
		wz = cur_z + ((float)(centery - y) * z) / proj;

		relx = wx - prev_x;
		rely = wy - prev_y;
		relz = wz - prev_z;
		vz = relx * prev_c + rely * prev_s;
		vx = -relx * prev_s + rely * prev_c;
		if (vz < 1.0f)
		{
		    gb_velocity[i * 2 + 0] = 0.0f;
		    gb_velocity[i * 2 + 1] = 0.0f;
		    continue;
		}

		prev_sx = (float)centerx + vx * (proj / vz);
		prev_sy = (float)centery - relz * (proj / vz);
		/* Pixel delta at 320x200. +X right, +Y down (DOOM / NGX MVLowRes). */
		gb_velocity[i * 2 + 0] = (float)x - prev_sx + gb_obj_du[i];
		gb_velocity[i * 2 + 1] = (float)y - prev_sy + gb_obj_dv[i];
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
