#ifndef TIKI_WIN32_UNISTD_H
#define TIKI_WIN32_UNISTD_H

/**
 * The POSIX file and process functions tiki (mostly its tests) uses, for Windows builds
 * (src/posix_win32.c), Linux uses the system <unistd.h>. close, read, write, unlink, access and
 * getcwd are the CRT's own (declared by <io.h> and <direct.h>, _CRT_DECLARE_NONSTDC_NAMES is set
 * by the build).
 */

#include <io.h>
#include <direct.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef F_OK
#define F_OK 0
#define W_OK 2
#define R_OK 4
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#ifndef PATH_MAX
#define PATH_MAX 260
#endif

int usleep(unsigned int microseconds);

/* A binary mode pipe. */
int pipe(int fds[2]);

/* The file is opened read/write in binary mode. */
int mkstemp(char* path_template);

int truncate(const char* path, long long length);

#ifdef __cplusplus
}
#endif

#endif
