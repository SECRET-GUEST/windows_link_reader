#include "open_lnk/cache_links.h"
#include "open_lnk/desktop.h"
#include "open_lnk/error.h"
#include "open_lnk/fs.h"
#include "open_lnk/gvfs.h"
#include "open_lnk/lnk.h"
#include "open_lnk/mapping.h"
#include "open_lnk/mounts.h"
#include "open_lnk/unc.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef OPEN_LNK_VERSION
#define OPEN_LNK_VERSION "0.0.8"
#endif

typedef enum {
    RESOLVE_RAW = 0,
    RESOLVE_LOCAL = 1,
    RESOLVE_CACHE = 2,
    RESOLVE_TABLE = 3
} ResolveSource;

static int g_debug = 0;
static int g_assist = 0;

static void dbg(const char *stage, const char *win, const char *lin) {
    if (!g_debug && !g_assist) return;
    fprintf(stderr, "[%s] win='%s'\n", stage, win ? win : "(null)");
    fprintf(stderr, "[%s] lin='%s'\n", stage, lin ? lin : "(null)");
}

static int has_prog_in_path(const char *prog) {
    if (!prog || !*prog) return 0;
    const char *path = getenv("PATH");
    if (!path) return 0;

    char tmp[PATH_MAX];
    const char *p = path;

    while (*p) {
        const char *sep = strchr(p, ':');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);

        if (len > 0 && len < sizeof(tmp) - 2) {
            memcpy(tmp, p, len);
            tmp[len] = 0;

            size_t need = len + 1 + strlen(prog) + 1;
            if (need < sizeof(tmp)) {
                tmp[len] = '/';
                strcpy(tmp + len + 1, prog);
                if (access(tmp, X_OK) == 0) return 1;
            }
        }

        if (!sep) break;
        p = sep + 1;
    }

    return 0;
}

static int run_capture(char *const argv[], char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    out[0] = 0;

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    ssize_t r = read(pipefd[0], out, outsz - 1);
    close(pipefd[0]);
    if (r < 0) r = 0;
    out[(size_t)r] = 0;

    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
}

static int looks_like_drive_path(const char *p) {
    if (!p || strlen(p) < 3) return 0;
    if (!isalpha((unsigned char)p[0])) return 0;
    if (p[1] != ':') return 0;
    if (p[2] != '/' && p[2] != '\\') return 0;
    return 1;
}

static int looks_like_unc_path(const char *p) {
    if (!p || strlen(p) < 5) return 0;
    return (strncmp(p, "//", 2) == 0) || (strncmp(p, "\\\\", 2) == 0);
}

static char *abs_path_or_dup(const char *p) {
    if (!p) return NULL;
    char tmp[PATH_MAX];
    if (realpath(p, tmp)) return strdup(tmp);
    return strdup(p);
}

static char *build_candidate_from_prefix(const char *prefix, const char *driveTarget /* X:/... */) {
    if (!prefix || !*prefix || !driveTarget || strlen(driveTarget) < 3) return NULL;

    const char *suffix = driveTarget + 2; // points at "/..."
    if (*suffix == '\\') suffix++;

    char joined[PATH_MAX];
    snprintf(joined, sizeof(joined), "%s%s", prefix, suffix);

    for (char *c = joined; *c; c++) {
        if (*c == '\\') *c = '/';
    }

    return strdup(joined);
}

static void free_mountpoints(char **items, int n) {
    if (!items) return;
    for (int i = 0; i < n; i++) free(items[i]);
    free(items);
}

// -------- assistant: choose prefix via zenity (LAST resort) --------
static char *assist_choose_prefix_zenity(void) {
    if (!has_prog_in_path("zenity")) return NULL;

    int n = 0;
    char **mnts = read_mountpoints(&n);
    if (!mnts || n <= 0) {
        free_mountpoints(mnts, n);
        return NULL;
    }

    // zenity --list ... + rows ; plus an extra button for manual entry
    // We output only the selected mount path.
    char *argv[4096];
    int k = 0;

    argv[k++] = "zenity";
    argv[k++] = "--list";
    argv[k++] = "--title=Open LNK";
    argv[k++] = "--text=Select a mount point for this shortcut:";
    argv[k++] = "--column=Mount";
    argv[k++] = "--hide-header";
    argv[k++] = "--height=420";
    argv[k++] = "--width=800";
    argv[k++] = "--extra-button=Manual path";

    for (int i = 0; i < n && k < (int)(sizeof(argv)/sizeof(argv[0])) - 2; i++) {
        argv[k++] = mnts[i];
    }
    argv[k] = NULL;

    char out[4096];
    int ec = run_capture(argv, out, sizeof(out));

    while (*out && (out[strlen(out) - 1] == '\n' || out[strlen(out) - 1] == '\r')) {
        out[strlen(out) - 1] = 0;
    }

    char *picked = NULL;

    // If user clicked OK, zenity returns 0 and prints selected row.
    // If user clicked extra button, zenity may still return 0 but output is empty (depends on versions).
    if (ec == 0) {
        if (out[0] == 0) {
            // Manual input
            char *argv2[] = {
                "zenity",
                "--entry",
                "--title=Open LNK",
                "--text=Enter the mount prefix (example: /mnt/GAWAIN)",
                NULL
            };
            char out2[4096];
            int ec2 = run_capture(argv2, out2, sizeof(out2));
            while (*out2 && (out2[strlen(out2) - 1] == '\n' || out2[strlen(out2) - 1] == '\r')) {
                out2[strlen(out2) - 1] = 0;
            }
            if (ec2 == 0 && out2[0]) picked = strdup(out2);
        } else {
            picked = strdup(out);
        }
    }

    free_mountpoints(mnts, n);
    return picked;
}

// -------- assistant: choose prefix via kdialog --------
static char *assist_choose_prefix_kdialog(void) {
    if (!has_prog_in_path("kdialog")) return NULL;

    int n = 0;
    char **mnts = read_mountpoints(&n);
    if (!mnts || n <= 0) {
        free_mountpoints(mnts, n);
        return NULL;
    }

    // kdialog --menu expects pairs: <tag> <item>
    // We'll use the mount path for both tag and item.
    // Add a manual entry option as first item.
    char *argv[4096];
    int k = 0;
    argv[k++] = "kdialog";
    argv[k++] = "--title";
    argv[k++] = "Open LNK";
    argv[k++] = "--menu";
    argv[k++] = "Select a mount point for this shortcut:";

    argv[k++] = "__MANUAL__";
    argv[k++] = "Manual path";

    for (int i = 0; i < n && k < (int)(sizeof(argv) / sizeof(argv[0])) - 3; i++) {
        argv[k++] = mnts[i];
        argv[k++] = mnts[i];
    }
    argv[k] = NULL;

    char out[4096];
    int ec = run_capture(argv, out, sizeof(out));

    while (*out && (out[strlen(out) - 1] == '\n' || out[strlen(out) - 1] == '\r')) {
        out[strlen(out) - 1] = 0;
    }

    char *picked = NULL;
    if (ec == 0 && out[0]) {
        if (strcmp(out, "__MANUAL__") == 0) {
            char *argv2[] = {
                "kdialog",
                "--title",
                "Open LNK",
                "--inputbox",
                "Enter the mount prefix (example: /mnt/GAWAIN)",
                "",
                NULL
            };
            char out2[4096];
            int ec2 = run_capture(argv2, out2, sizeof(out2));
            while (*out2 && (out2[strlen(out2) - 1] == '\n' || out2[strlen(out2) - 1] == '\r')) {
                out2[strlen(out2) - 1] = 0;
            }
            if (ec2 == 0 && out2[0]) picked = strdup(out2);
        } else {
            picked = strdup(out);
        }
    }

    free_mountpoints(mnts, n);
    return picked;
}

static char *assist_choose_prefix_tty(void) {
    if (!isatty(STDIN_FILENO)) return NULL;
    fprintf(stderr, "Enter mount prefix (example: /mnt/GAWAIN), or empty to cancel:\n> ");
    fflush(stderr);

    char buf[PATH_MAX];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;

    while (*buf && (buf[strlen(buf) - 1] == '\n' || buf[strlen(buf) - 1] == '\r')) {
        buf[strlen(buf) - 1] = 0;
    }
    if (!buf[0]) return NULL;
    return strdup(buf);
}

static int try_open_candidate(const char *win_raw, const char *cand, const char *stage) {
    if (!cand || !*cand) return -1;
    dbg(stage, win_raw, cand);
    if (!path_exists(cand)) return -1;
    return open_with_desktop(cand);
}

int main(int argc, char *argv[]) {
    const char *lnkPath = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--debug")) g_debug = 1;
        else if (!strcmp(argv[i], "--assist")) g_assist = 1;
        else if (!strcmp(argv[i], "--version")) {
            printf("%s\n", OPEN_LNK_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Usage: open_lnk [--debug] [--assist] <file.lnk>\n");
            return 0;
        } else {
            lnkPath = argv[i];
        }
    }

    if (!lnkPath) {
        fprintf(stderr, "No .lnk provided.\n");
        return 1;
    }

    // load & parse lnk
    LnkInfo info = {0};
    if (parseLnkFile(lnkPath, &info) != 0) {
        showError("Open LNK", "Failed to parse .lnk file.");
        return 1;
    }

    char *win_raw = lnk_get_best_target_raw(&info);
    if (!win_raw || !*win_raw) {
        free(win_raw);
        freeLnkInfo(&info);
        showError("Open LNK", "No target path found in .lnk file.");
        return 1;
    }

    // normalize target slashes to forward (internal)
    char *target = strdup(win_raw);
    for (char *c = target; *c; c++) if (*c == '\\') *c = '/';

    // compute absolute path of lnk for per-link cache
    char *lnk_abs = abs_path_or_dup(lnkPath);

    // mapping file (drive/UNC tables)
    char *mapPath = ml_get_default_path();
    MappingList maps = {0};
    (void)ml_load(&maps, mapPath);

    // 0) RAW local path already on Linux
    if (is_abs_posix_path(target)) {
        if (try_open_candidate(win_raw, target, "raw:posix") == 0) goto ok;
    }

    // 1) UNC shares
    if (looks_like_unc_path(target)) {
        // first check explicit mapping rules
        char *unc_mapped = ml_map_unc(&maps, target);
        if (unc_mapped) {
            if (try_open_candidate(win_raw, unc_mapped, "unc:table") == 0) {
                free(unc_mapped);
                goto ok;
            }
            free(unc_mapped);
        }

        // then try GVFS style (smb://)
        char *smb = unc_to_smb_uri(target);
        if (smb) {
            dbg("unc:smb", win_raw, smb);
            // if gvfs available try to open URI
            if (gvfs_open_uri(smb) == 0) {
                free(smb);
                goto ok;
            }
            free(smb);
        }
    }

    // 2) Drive: per-link cache FIRST (silent)
    if (looks_like_drive_path(target) && lnk_abs) {
        char *pfx = cache_get_prefix_for_lnk(lnk_abs);
        if (pfx && *pfx) {
            char *cand = build_candidate_from_prefix(pfx, target);
            if (cand) {
                if (try_open_candidate(win_raw, cand, "drive:cache") == 0) {
                    free(cand); free(pfx);
                    free(target); free(win_raw);
                    freeLnkInfo(&info);
                    ml_free(&maps);
                    free(mapPath);
                    free(lnk_abs);
                    return 0;
                }
                free(cand);
            }
        }
        free(pfx);
    }

    // 3) Drive: mapping table
    if (looks_like_drive_path(target)) {
        char *mapped = ml_map_drive(&maps, target);
        if (mapped) {
            if (try_open_candidate(win_raw, mapped, "drive:table") == 0) {
                free(mapped);
                goto ok;
            }
            free(mapped);
        }
    }

    // 4) Drive: try guessing mounts from /proc/mounts
    if (looks_like_drive_path(target)) {
        char *guess = guess_drive_mount(target);
        if (guess) {
            if (try_open_candidate(win_raw, guess, "drive:mounts") == 0) {
                free(guess);
                goto ok;
            }
            free(guess);
        }
    }

    // 5) LAST RESORT assistant (only now)
    if (looks_like_drive_path(target) && lnk_abs) {
        char *prefix = assist_choose_prefix_zenity();
        if (!prefix) prefix = assist_choose_prefix_kdialog();
        if (!prefix) prefix = assist_choose_prefix_tty();

        if (prefix && *prefix) {
            char *cand = build_candidate_from_prefix(prefix, target);
            if (cand) {
                dbg("drive:assist", win_raw, cand);
                if (path_exists(cand) && open_with_desktop(cand) == 0) {
                    cache_set_prefix_for_lnk(lnk_abs, prefix);
                    free(cand);
                    free(prefix);
                    goto ok;
                }
                free(cand);
            }
        }
        free(prefix);
    }

    // not resolved
    dbg("fail", win_raw, "(no resolution)");
    showError("Open LNK", "Could not resolve this shortcut target.\nTry --debug or --assist from terminal.");

    free(target);
    free(win_raw);
    freeLnkInfo(&info);
    ml_free(&maps);
    free(mapPath);
    free(lnk_abs);
    return 2;

ok:
    free(target);
    free(win_raw);
    freeLnkInfo(&info);
    ml_free(&maps);
    free(mapPath);
    free(lnk_abs);
    return 0;
}
