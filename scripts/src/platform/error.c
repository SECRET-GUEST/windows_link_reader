/*
 *  Error reporting
 *
 * We try to show errors "nicely" (desktop notifications), but we ALWAYS keep
 * a reliable stderr fallback.
 *
 * Why stderr is always printed:
 *   - Notification tools may be missing.
 *   - Desktop services may be unavailable (no DBus, no DISPLAY, ...).
 *   - This tool is a CLI first; stderr is the most dependable channel.
 */

#include "open_lnk/error.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

static int is_executable_file(const char *path) {
    return (path && *path && access(path, X_OK) == 0);
}

/*
 * Small "spawn without a shell" helper.
 *
 * Return:
 *   1 -> fork succeeded (exec may still fail in the child)
 *   0 -> fork failed
 */
static int try_spawn(const char *path_or_name, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        /*
         * Notification tools can be very noisy when the desktop session is not
         * available (examples: DBus permission errors, "Failed to open display").
         *
         * We redirect stdout/stderr to /dev/null so the CLI output stays clean.
         * The real error message is still printed to stderr by showError().
         */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            (void)dup2(devnull, STDOUT_FILENO);
            (void)dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        if (strchr(path_or_name, '/')) execv(path_or_name, argv);
        else execvp(path_or_name, argv);
        _exit(127);
    }
    return 1;
}

void showError(const char *message) {
    if (!message) message = "Unknown error";

    /*
     * Best-effort notification:
     * we try to show the message in the desktop UI, but we don't block and we
     * don't treat failures as fatal.
     */
    struct utsname sysinfo;
    if (uname(&sysinfo) == 0) {
        if (strcmp(sysinfo.sysname, "Darwin") == 0) {
            char script[1024];
            snprintf(script, sizeof(script),
                     "display notification \"%s\" with title \"LNK Reader\"", message);
            char *argv[] = { "osascript", "-e", script, NULL };
            (void)try_spawn("osascript", argv);
        } else if (strcmp(sysinfo.sysname, "Linux") == 0) {
            char *argv1[] = { "notify-send", "LNK Reader", (char*)message, NULL };
            if (is_executable_file("/usr/bin/notify-send")) (void)try_spawn("/usr/bin/notify-send", argv1);
            else (void)try_spawn("notify-send", argv1);

            char *argv2[] = { "zenity", "--error", "--text", (char*)message, NULL };
            (void)try_spawn("zenity", argv2);

            char *argv3[] = { "kdialog", "--error", (char*)message, NULL };
            (void)try_spawn("kdialog", argv3);
        }
    }

    /* Always print to stderr so users see something even without a GUI session. */
    fprintf(stderr, "LNK Reader: %s\n", message);
}
