// src/platform/desktop.c

#include "open_lnk/desktop.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

int open_with_desktop(const char *path_or_uri) {
    if (!path_or_uri || !*path_or_uri) return -1;

    struct utsname u;
    int is_macos = (uname(&u) == 0 && strcmp(u.sysname, "Darwin") == 0);

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        if (is_macos) {
            execlp("open", "open", path_or_uri, (char*)NULL);
        } else {
            execlp("xdg-open", "xdg-open", path_or_uri, (char*)NULL);
        }
        _exit(127);
    }

    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;

    if (WIFEXITED(st)) {
        int code = WEXITSTATUS(st);
        return (code == 0) ? 0 : -1;
    }

    return -1;
}
