/*
 *  Filesystem helpers
 *
 * Small filesystem utilities reused across the project.
 */

#include "open_lnk/fs.h"

#include <sys/stat.h>

int path_exists(const char *path) {
    struct stat st;
    return (path && *path && stat(path, &st) == 0);
}

void normalize_backslashes(char *s) {
    /* Simple in-place replace: '\' -> '/' */
    if (!s) return;
    for (char *p = s; *p; ++p) if (*p == '\\') *p = '/';
}
