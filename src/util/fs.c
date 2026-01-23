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

int path_is_dir(const char *path) {
    struct stat st;
    if (!path || !*path) return 0;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

void normalize_backslashes(char *s) {
    /* Simple in-place replace: '\' -> '/' */
    if (!s) return;
    for (char *p = s; *p; ++p) if (*p == '\\') *p = '/';
}
