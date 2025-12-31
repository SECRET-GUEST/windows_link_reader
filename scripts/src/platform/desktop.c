/*
 *  Desktop opening
 *
 * Goal: reproduce the "open with the default application" behavior.
 * - macOS: `open`
 * - Linux: `xdg-open`
 *
 * Note:
 *   We start the opener process and do not wait() for it (simple CLI usage).
 */

#include "open_lnk/desktop.h"

#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

int open_with_desktop(const char *path_or_uri) {
    if (!path_or_uri || !*path_or_uri) return -1;

    /*
     * We use uname() to detect macOS at runtime, so the same binary can be
     * built in different environments without relying on preprocessor macros.
     */
    struct utsname u;
    if (uname(&u) == 0 && strcmp(u.sysname, "Darwin") == 0) {
        pid_t pid = fork();
        if (pid < 0) return -1;
        if (pid == 0) { execlp("open", "open", path_or_uri, (char*)NULL); _exit(127); }
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { execlp("xdg-open", "xdg-open", path_or_uri, (char*)NULL); _exit(127); }
    return 0;
}
