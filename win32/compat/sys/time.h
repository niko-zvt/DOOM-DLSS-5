/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#ifndef WIN32_COMPAT_SYS_TIME_H
#define WIN32_COMPAT_SYS_TIME_H

#include <time.h>

struct timezone
{
    int tz_minuteswest;
    int tz_dsttime;
};

#endif
