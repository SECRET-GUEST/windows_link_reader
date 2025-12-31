#ifndef OPEN_LNK_DESKTOP_H
#define OPEN_LNK_DESKTOP_H

/*
 * Open a filesystem path (file/folder) or a URI with the system default handler.
 *
 * This is the "double-click" equivalent:
 *   - Linux: uses `xdg-open`
 *   - macOS: uses `open`
 *
 * Important behavior notes:
 *   - This function is "fire-and-forget": it forks and execs but does NOT wait().
 *   - Returning 0 only means the child process was started (fork worked).
 *     It does NOT guarantee the target actually opened successfully.
 *
 * Return values:
 *   0   -> we successfully forked and attempted to exec the opener
 *  -1   -> fork() failed
 */
int open_with_desktop(const char *path_or_uri);

#endif /* OPEN_LNK_DESKTOP_H */
