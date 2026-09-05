/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#include <stdlib.h>
#include <string.h>

#include "i_system.h"
#include "d_event.h"
#include "d_net.h"
#include "m_argv.h"
#include "doomstat.h"
#include "i_net.h"

void I_InitNetwork(void)
{
    int i;

    doomcom = malloc(sizeof(*doomcom));
    memset(doomcom, 0, sizeof(*doomcom));

    i = M_CheckParm("-dup");
    if (i && i < myargc - 1)
    {
	doomcom->ticdup = myargv[i + 1][0] - '0';
	if (doomcom->ticdup < 1)
	    doomcom->ticdup = 1;
	if (doomcom->ticdup > 9)
	    doomcom->ticdup = 9;
    }
    else
	doomcom->ticdup = 1;

    if (M_CheckParm("-extratic"))
	doomcom->extratics = 1;
    else
	doomcom->extratics = 0;

    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
}

void I_NetCmd(void)
{
    if (doomcom->command == CMD_SEND)
	return;
    if (doomcom->command == CMD_GET)
    {
	doomcom->remotenode = -1;
	return;
    }
    I_Error("Bad net cmd: %i\n", doomcom->command);
}
