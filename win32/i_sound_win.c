/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include "doomdef.h"
#include "doomtype.h"

#define boolean BOOLEAN_WIN32_AVOID
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>

static const CLSID CLSID_MMDeviceEnumerator_ =
    {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const IID IID_IMMDeviceEnumerator_ =
    {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const IID IID_IAudioClient_ =
    {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
static const IID IID_IAudioRenderClient_ =
    {0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}};
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef boolean

#include "z_zone.h"
#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "d_main.h"

#define SAMPLECOUNT		512
#define NUM_CHANNELS		8
#define BUFMUL			4
#define MIXBUFFERSIZE		(SAMPLECOUNT * BUFMUL)
#define SAMPLERATE		11025
#define SAMPLESIZE		2

int lengths[NUMSFX];
signed short mixbuffer[MIXBUFFERSIZE];

static unsigned int channelstep[NUM_CHANNELS];
static unsigned int channelstepremainder[NUM_CHANNELS];
static unsigned char *channels[NUM_CHANNELS];
static unsigned char *channelsend[NUM_CHANNELS];
static int channelstart[NUM_CHANNELS];
static int channelhandles[NUM_CHANNELS];
static int channelids[NUM_CHANNELS];
static int steptable[256];
static int vol_lookup[128 * 256];
static int *channelleftvol_lookup[NUM_CHANNELS];
static int *channelrightvol_lookup[NUM_CHANNELS];

static IMMDeviceEnumerator *gb_enum;
static IMMDevice *gb_dev;
static IAudioClient *gb_client;
static IAudioRenderClient *gb_render;
static UINT32 gb_buffer_frames;
static WAVEFORMATEX *gb_mixfmt;
static int gb_audio_ok;

static void *
getsfx(char *sfxname, int *len)
{
    unsigned char *sfx;
    unsigned char *paddedsfx;
    int i;
    int size;
    int paddedsize;
    char name[20];
    int sfxlump;

    sprintf(name, "ds%s", sfxname);
    if (W_CheckNumForName(name) == -1)
	sfxlump = W_GetNumForName("dspistol");
    else
	sfxlump = W_GetNumForName(name);

    size = W_LumpLength(sfxlump);
    sfx = (unsigned char *)W_CacheLumpNum(sfxlump, PU_STATIC);
    paddedsize = ((size - 8 + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;
    paddedsfx = (unsigned char *)Z_Malloc(paddedsize + 8, PU_STATIC, 0);
    memcpy(paddedsfx, sfx, size);
    for (i = size; i < paddedsize + 8; i++)
	paddedsfx[i] = 128;
    Z_Free(sfx);
    *len = paddedsize;
    return (void *)(paddedsfx + 8);
}

static int
addsfx(int sfxid, int volume, int step, int seperation)
{
    static unsigned short handlenums = 0;
    int i;
    int rc = -1;
    int oldest = gametic;
    int oldestnum = 0;
    int slot;
    int rightvol;
    int leftvol;

    if (sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful
	|| sfxid == sfx_sawhit || sfxid == sfx_stnmov || sfxid == sfx_pistol)
    {
	for (i = 0; i < NUM_CHANNELS; i++)
	{
	    if (channels[i] && channelids[i] == sfxid)
	    {
		channels[i] = 0;
		break;
	    }
	}
    }

    for (i = 0; (i < NUM_CHANNELS) && channels[i]; i++)
    {
	if (channelstart[i] < oldest)
	{
	    oldestnum = i;
	    oldest = channelstart[i];
	}
    }

    slot = (i == NUM_CHANNELS) ? oldestnum : i;
    channels[slot] = (unsigned char *)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];
    if (!handlenums)
	handlenums = 100;
    channelhandles[slot] = rc = handlenums++;
    channelstep[slot] = step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    seperation += 1;
    leftvol = volume - ((volume * seperation * seperation) >> 16);
    seperation = seperation - 257;
    rightvol = volume - ((volume * seperation * seperation) >> 16);

    if (rightvol < 0)
	rightvol = 0;
    if (rightvol > 127)
	rightvol = 127;
    if (leftvol < 0)
	leftvol = 0;
    if (leftvol > 127)
	leftvol = 127;

    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];
    channelids[slot] = sfxid;
    return rc;
}

void I_SetChannels(void)
{
    int i;
    int j;
    int *steptablemid = steptable + 128;

    for (i = -128; i < 128; i++)
	steptablemid[i] = (int)(pow(2.0, (i / 64.0)) * 65536.0);

    for (i = 0; i < 128; i++)
	for (j = 0; j < 256; j++)
	    vol_lookup[i * 256 + j] = (i * (j - 128) * 256) / 127;
}

void I_SetSfxVolume(int volume)
{
    snd_SfxVolume = volume;
}

void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    priority = 0;
    id = addsfx(id, vol, steptable[pitch], sep);
    return id;
}

void I_StopSound(int handle)
{
    handle = 0;
}

int I_SoundIsPlaying(int handle)
{
    return gametic < handle;
}

void I_UpdateSound(void)
{
    unsigned int sample;
    int dl;
    int dr;
    signed short *leftout;
    signed short *rightout;
    signed short *leftend;
    int step;
    int chan;

    leftout = mixbuffer;
    rightout = mixbuffer + 1;
    step = 2;
    leftend = mixbuffer + SAMPLECOUNT * step;

    while (leftout != leftend)
    {
	dl = 0;
	dr = 0;
	for (chan = 0; chan < NUM_CHANNELS; chan++)
	{
	    if (channels[chan])
	    {
		sample = *channels[chan];
		dl += channelleftvol_lookup[chan][sample];
		dr += channelrightvol_lookup[chan][sample];
		channelstepremainder[chan] += channelstep[chan];
		channels[chan] += channelstepremainder[chan] >> 16;
		channelstepremainder[chan] &= 65536 - 1;
		if (channels[chan] >= channelsend[chan])
		    channels[chan] = 0;
	    }
	}

	if (dl > 0x7fff)
	    *leftout = 0x7fff;
	else if (dl < -0x8000)
	    *leftout = -0x8000;
	else
	    *leftout = (signed short)dl;

	if (dr > 0x7fff)
	    *rightout = 0x7fff;
	else if (dr < -0x8000)
	    *rightout = -0x8000;
	else
	    *rightout = (signed short)dr;

	leftout += step;
	rightout += step;
    }
}

static void gb_write_wasapi(void)
{
    HRESULT hr;
    UINT32 padding;
    UINT32 avail;
    BYTE *data;
    UINT32 frames;
    UINT32 i;
    UINT32 src_rate = SAMPLERATE;
    UINT32 dst_rate;
    UINT32 src_ch;
    UINT32 dst_ch;
    UINT32 dst_bits;
    double pos;
    double step;

    if (!gb_audio_ok)
	return;

    hr = IAudioClient_GetCurrentPadding(gb_client, &padding);
    if (FAILED(hr))
	return;

    avail = gb_buffer_frames - padding;
    dst_rate = gb_mixfmt->nSamplesPerSec;
    dst_ch = gb_mixfmt->nChannels;
    dst_bits = gb_mixfmt->wBitsPerSample;
    frames = (UINT32)((SAMPLECOUNT * (unsigned long long)dst_rate) / src_rate);
    if (frames > avail)
	frames = avail;
    if (frames < 1)
	return;

    hr = IAudioRenderClient_GetBuffer(gb_render, frames, &data);
    if (FAILED(hr))
	return;

    src_ch = 2;
    step = (double)src_rate / (double)dst_rate;
    pos = 0.0;

    for (i = 0; i < frames; i++)
    {
	int si = (int)pos;
	int sl, sr;
	UINT32 c;

	if (si >= SAMPLECOUNT)
	    si = SAMPLECOUNT - 1;
	sl = mixbuffer[si * src_ch + 0];
	sr = mixbuffer[si * src_ch + 1];
	pos += step;

	if (dst_bits == 16)
	{
	    short *out = (short *)data;
	    out[i * dst_ch + 0] = (short)sl;
	    if (dst_ch > 1)
		out[i * dst_ch + 1] = (short)sr;
	    for (c = 2; c < dst_ch; c++)
		out[i * dst_ch + c] = 0;
	}
	else if (dst_bits == 32)
	{
	    float *out = (float *)data;
	    out[i * dst_ch + 0] = (float)sl / 32768.0f;
	    if (dst_ch > 1)
		out[i * dst_ch + 1] = (float)sr / 32768.0f;
	    for (c = 2; c < dst_ch; c++)
		out[i * dst_ch + c] = 0.0f;
	}
	else
	{
	    memset(data, 0, frames * gb_mixfmt->nBlockAlign);
	    break;
	}
    }

    IAudioRenderClient_ReleaseBuffer(gb_render, frames, 0);
}

void I_SubmitSound(void)
{
    gb_write_wasapi();
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    handle = vol = sep = pitch = 0;
}

void I_ShutdownSound(void)
{
    if (gb_client)
	IAudioClient_Stop(gb_client);
    if (gb_render)
	IAudioRenderClient_Release(gb_render);
    if (gb_client)
	IAudioClient_Release(gb_client);
    if (gb_mixfmt)
	CoTaskMemFree(gb_mixfmt);
    if (gb_dev)
	IMMDevice_Release(gb_dev);
    if (gb_enum)
	IMMDeviceEnumerator_Release(gb_enum);
    gb_render = NULL;
    gb_client = NULL;
    gb_mixfmt = NULL;
    gb_dev = NULL;
    gb_enum = NULL;
    gb_audio_ok = 0;
}

static int gb_init_wasapi(void)
{
    HRESULT hr;
    REFERENCE_TIME dur = 10000000; /* 1s */

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
	return 0;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_, NULL, CLSCTX_ALL,
			  &IID_IMMDeviceEnumerator_, (void **)&gb_enum);
    if (FAILED(hr))
	return 0;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(gb_enum, eRender, eConsole, &gb_dev);
    if (FAILED(hr))
	return 0;

    hr = IMMDevice_Activate(gb_dev, &IID_IAudioClient_, CLSCTX_ALL, NULL, (void **)&gb_client);
    if (FAILED(hr))
	return 0;

    hr = IAudioClient_GetMixFormat(gb_client, &gb_mixfmt);
    if (FAILED(hr))
	return 0;

    hr = IAudioClient_Initialize(gb_client, AUDCLNT_SHAREMODE_SHARED, 0,
				 dur, 0, gb_mixfmt, NULL);
    if (FAILED(hr))
	return 0;

    hr = IAudioClient_GetBufferSize(gb_client, &gb_buffer_frames);
    if (FAILED(hr))
	return 0;

    hr = IAudioClient_GetService(gb_client, &IID_IAudioRenderClient_, (void **)&gb_render);
    if (FAILED(hr))
	return 0;

    hr = IAudioClient_Start(gb_client);
    if (FAILED(hr))
	return 0;

    return 1;
}

void I_InitSound(void)
{
    int i;

    fprintf(stderr, "I_InitSound: ");
    gb_audio_ok = gb_init_wasapi();
    if (gb_audio_ok)
	fprintf(stderr, "WASAPI ready\n");
    else
	fprintf(stderr, "WASAPI failed, continuing silent\n");

    fprintf(stderr, "I_InitSound: ");
    for (i = 1; i < NUMSFX; i++)
    {
	if (!S_sfx[i].link)
	    S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
	else
	{
	    S_sfx[i].data = S_sfx[i].link->data;
	    lengths[i] = lengths[(int)(S_sfx[i].link - S_sfx)];
	}
    }
    fprintf(stderr, "pre-cached all sound data\n");

    memset(mixbuffer, 0, sizeof(mixbuffer));
    fprintf(stderr, "I_InitSound: sound module ready\n");
}

void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}

static int looping = 0;
static int musicdies = -1;

void I_PlaySong(int handle, int looping_)
{
    handle = looping_ = 0;
    looping = 0;
    musicdies = gametic + TICRATE * 30;
}

void I_PauseSong(int handle) { handle = 0; }
void I_ResumeSong(int handle) { handle = 0; }

void I_StopSong(int handle)
{
    handle = 0;
    looping = 0;
    musicdies = 0;
}

void I_UnRegisterSong(int handle) { handle = 0; }

int I_RegisterSong(void *data)
{
    data = NULL;
    return 1;
}

int I_QrySongPlaying(int handle)
{
    handle = 0;
    return looping || (musicdies > gametic);
}
