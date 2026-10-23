#ifndef TIKI_WIN32_STRINGS_H
#define TIKI_WIN32_STRINGS_H

/* strcasecmp and strncasecmp for Windows builds, Linux uses the system <strings.h>. */

#include <string.h>

static inline int strcasecmp(const char* a, const char* b) {
    return _stricmp(a, b);
}

static inline int strncasecmp(const char* a, const char* b, size_t n) {
    return _strnicmp(a, b, n);
}

#endif
