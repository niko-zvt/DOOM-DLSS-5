// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id: linux.c,v 1.3 1997/01/26 07:45:01 b1 Exp $
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	UNIX soundserver output. PulseAudio on modern Linux / WSL.
//
//-----------------------------------------------------------------------------

static const char rcsid[] = "$Id: linux.c,v 1.3 1997/01/26 07:45:01 b1 Exp $";


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#ifdef HAVE_LINUX_SOUNDCARD
#include <linux/soundcard.h>
#endif

#include "soundsrv.h"

static pa_simple	*pulse = NULL;
static int		audio_fd = -1;

void I_InitMusic(void)
{
}

void
I_InitSound
( int	samplerate,
  int	samplesize )
{
    int			error;
    pa_sample_spec	ss;

    (void)samplesize;

    ss.format = PA_SAMPLE_S16LE;
    ss.rate = samplerate;
    ss.channels = 2;

    pulse = pa_simple_new(NULL, "linuxdoom-sndserver", PA_STREAM_PLAYBACK,
			  NULL, "game", &ss, NULL, NULL, &error);
    if (pulse)
    {
	fprintf(stderr, "sndserver: PulseAudio %d Hz stereo 16-bit\n",
		samplerate);
	return;
    }

    fprintf(stderr, "sndserver: PulseAudio failed (%s), trying /dev/dsp\n",
	    pa_strerror(error));

    audio_fd = open("/dev/dsp", O_WRONLY);
    if (audio_fd < 0)
    {
	fprintf(stderr, "sndserver: no audio device, running silent\n");
	return;
    }

#ifdef HAVE_LINUX_SOUNDCARD
    {
	int i = 11 | (2<<16);
	ioctl(audio_fd, SNDCTL_DSP_SETFRAGMENT, &i);
	ioctl(audio_fd, SNDCTL_DSP_RESET, 0);
	i = samplerate;
	ioctl(audio_fd, SNDCTL_DSP_SPEED, &i);
	i = 1;
	ioctl(audio_fd, SNDCTL_DSP_STEREO, &i);
	i = AFMT_S16_LE;
	ioctl(audio_fd, SNDCTL_DSP_SETFMT, &i);
    }
#endif
}

void
I_SubmitOutputBuffer
( void*	samples,
  int	samplecount )
{
    int	error;

    if (pulse)
    {
	if (pa_simple_write(pulse, samples, (size_t)samplecount * 4, &error) < 0)
	    fprintf(stderr, "sndserver: PulseAudio write: %s\n",
		    pa_strerror(error));
	return;
    }

    if (audio_fd >= 0)
	write(audio_fd, samples, samplecount * 4);
}

void I_ShutdownSound(void)
{
    int	error;

    if (pulse)
    {
	pa_simple_drain(pulse, &error);
	pa_simple_free(pulse);
	pulse = NULL;
    }

    if (audio_fd >= 0)
    {
	close(audio_fd);
	audio_fd = -1;
    }
}

void I_ShutdownMusic(void)
{
}
