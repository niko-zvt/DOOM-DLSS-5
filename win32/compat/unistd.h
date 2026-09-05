/* Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT. */
#ifndef WIN32_COMPAT_UNISTD_H
#define WIN32_COMPAT_UNISTD_H

#include <io.h>
#include <direct.h>
#include <process.h>
#include <fcntl.h>

#ifndef R_OK
#define R_OK 4
#define W_OK 2
#define X_OK 0
#define F_OK 0
#endif

#ifndef access
#define access _access
#endif

#ifndef close
#define close _close
#endif

#ifndef read
#define read _read
#endif

#ifndef write
#define write _write
#endif

#ifndef lseek
#define lseek _lseek
#endif

#ifndef open
#define open _open
#endif

#ifndef unlink
#define unlink _unlink
#endif

#ifndef getcwd
#define getcwd _getcwd
#endif

#ifndef chdir
#define chdir _chdir
#endif

#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif

#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif

#endif
