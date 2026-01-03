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
#define OPEN_LNK_VERSION "0.0.7"
#endif

typedef enum {
    RESOLVE_RAW = 0,
    RESOLVE_LOCAL = 1,
    RESOLVE_CACHE = 2,
    RESOLVE_TABLE = 3,
    RESOLVE_GVFS = 4,
    RESOLVE_CIFS = 5,
    RESOLVE_MOUNTS = 6,
    RESOLVE_URI = 7
} ResolveKind;

static int g_debug = 0;

static void dbg(const char *stage, const char *win_raw, const char *linux_candidate) {
    if (!g_debug) return;
    fprintf(stderr, "[open_lnk] %s\n", stage ? stage : "debug");
    fprintf(stderr, "  windows: %s\n", win_raw ? win_raw : "(null)");
    fprintf(stderr, "  linux  : %s\n", linux_candidate ? linux_candidate : "(null)");
}

static void usage(void) {
    showError("Usage: open-lnk [--debug] [--version] <file.lnk>");
}

static int looks_like_drive_path(const char *p) {
    return p && strlen(p) >= 3 &&
           isalpha((unsigned char)p[0]) &&
           p[1] == ':' &&
           (p[2] == '/' || p[2] == '\\');
}

static int looks_like_unc_path(const char *p) {
    if (!p) return 0;
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
    char buf[PATH_MAX];

    if (*suffix == '/') snprintf(buf, sizeof(buf), "%s%s", prefix, suffix);
    else snprintf(buf, sizeof(buf), "%s/%s", prefix, suffix);

    return strdup(buf);
}

// -------- capture stdout + exit code --------
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
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    ssize_t n = read(pipefd[0], out, (ssize_t)(outsz - 1));
    if (n < 0) n = 0;
    out[n] = 0;
    close(pipefd[0]);

    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
}

static int has_prog_in_path(const char *name) {
    if (!name || !*name) return 0;
    const char *path = getenv("PATH");
    if (!path) return 0;

    char *dup = strdup(path);
    if (!dup) return 0;

    int ok = 0;
    char *save = NULL;
    for (char *tok = strtok_r(dup, ":", &save); tok; tok = strtok_r(NULL, ":", &save)) {
        char full[4096];
        int r = snprintf(full, sizeof(full), "%s/%s", tok, name);
        if (r > 0 && r < (int)sizeof(full) && access(full, X_OK) == 0) {
            ok = 1;
            break;
        }
    }

    free(dup);
    return ok;
}

// -------- read mountpoints (Linux) --------
static char **read_mountpoints(int *count) {
    if (count) *count = 0;

    FILE *f = fopen("/proc/self/mounts", "r");
    if (!f) return NULL;

    char **items = NULL;
    int n = 0, cap = 0;

    char dev[512], mnt[1024], fstype[128], opts[1024];
    int a = 0, b = 0;

    while (fscanf(f, "%511s %1023s %127s %1023s %d %d\n", dev, mnt, fstype, opts, &a, &b) == 6) {
        if (strncmp(mnt, "/proc", 5) == 0) continue;
        if (strncmp(mnt, "/sys", 4) == 0) continue;
        if (strncmp(mnt, "/dev", 4) == 0) continue;
        if (strncmp(mnt, "/run", 4) == 0) continue;

        int exists = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(items[i], mnt) == 0) { exists = 1; break; }
        }
        if (exists) continue;

        if (n == cap) {
            int ncap = (cap == 0) ? 16 : cap * 2;
            char **tmp = (char **)realloc(items, (size_t)ncap * sizeof(char *));
            if (!tmp) break;
            items = tmp;
            cap = ncap;
        }
        items[n++] = strdup(mnt);
    }

    fclose(f);
    if (count) *count = n;
    return items;
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
    char *argv[4096];
    int k = 0;
    argv[k++] = "zenity";
    argv[k++] = "--list";
    argv[k++] = "--title=Open LNK";
    argv[k++] = "--text=Select a mount point for this shortcut.";
    argv[k++] = "--column=Mount point";
    argv[k++] = "--ok-label=Use";
    argv[k++] = "--extra-button=Manual path";
    argv[k++] = "--cancel-label=Cancel";

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
            // manual entry
            char *argv2[] = {
                "zenity", "--entry",
                "--title=Open LNK",
                "--text=Enter the mount prefix (example: /mnt/GAWAIN)",
                "--entry-text=",
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
    const char *envd = getenv("WINDOWS_LINK_READER_DEBUG");
    if (envd && *envd && strcmp(envd, "0") != 0) g_debug = 1;

    const char *lnk_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) { g_debug = 1; continue; }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("open_lnk %s\n", OPEN_LNK_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        }
        if (!lnk_path) lnk_path = argv[i];
    }

    if (!lnk_path) { usage(); return 1; }

    char *lnk_abs = abs_path_or_dup(lnk_path);

    FILE *f = fopen(lnk_path, "rb");
    if (!f) { showError("Cannot open .lnk file"); free(lnk_abs); return 1; }

    LnkInfo info;
    if (!parse_lnk(f, &info)) { fclose(f); free(lnk_abs); return 1; }
    fclose(f);

    char *target = build_best_target(&info);
    if (!target) { freeLnkInfo(&info); showError("No target path found"); free(lnk_abs); return 1; }

    char *win_raw = strdup(target);
    normalize_backslashes(target);
    dbg("normalized", win_raw, target);

    // Load mappings
    MapList maps = {0};
    char *mapPath = NULL;

    const char *envp = getenv("WINDOWS_LINK_READER_MAP");
    if (envp && *envp) mapPath = strdup(envp);
    else mapPath = default_map_path();

    if (mapPath) load_map_file(mapPath, &maps);

    // 1) Raw local path
    if (path_exists(target)) {
        dbg("open:raw", win_raw, target);
        if (open_with_desktop(target) == 0) {
            free(target); free(win_raw);
            freeLnkInfo(&info);
            ml_free(&maps);
            free(mapPath);
            free(lnk_abs);
            return 0;
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

    char *uri_fallback = NULL;

    // 3) UNC strategies (no assistant)
    if (looks_like_unc_path(target)) {
        char *r = try_map_unc_with_table(target, &maps);
        if (r) { free(target); target = r; }
        if (try_open_candidate(win_raw, target, "unc:table") == 0) goto ok;

        r = try_map_unc_via_gvfs(target);
        if (r) { free(target); target = r; }
        if (try_open_candidate(win_raw, target, "unc:gvfs") == 0) goto ok;

        r = try_map_unc_to_cifs_mounts(target);
        if (r) { free(target); target = r; }
        if (try_open_candidate(win_raw, target, "unc:cifs") == 0) goto ok;

        uri_fallback = unc_to_smb_uri_encoded(target);
        if (uri_fallback && *uri_fallback) {
            dbg("unc:uri", win_raw, uri_fallback);
            if (open_with_desktop(uri_fallback) == 0) goto ok;
        }
    }

    // 4) Drive strategies (no assistant)
    if (looks_like_drive_path(target)) {
        char *r = try_map_drive_with_table(target, &maps);
        if (r) { free(target); target = r; }
        if (try_open_candidate(win_raw, target, "drive:table") == 0) goto ok;

        r = try_map_drive_to_mounts_scored(target);
        if (r) { free(target); target = r; }
        if (try_open_candidate(win_raw, target, "drive:mounts") == 0) goto ok;
    }

    // 5) LAST RESORT assistant (only now)
    if (looks_like_drive_path(target) && lnk_abs) {
        char *prefix = assist_choose_prefix_zenity();
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

    // FAIL
    {
        char buf[1024];
        if (uri_fallback && *uri_fallback) snprintf(buf, sizeof(buf), "Failed to open: %s (and %s)", target, uri_fallback);
        else snprintf(buf, sizeof(buf), "Failed to open: %s", target);
        showError(buf);

        if (g_debug) {
            fprintf(stderr, "[open_lnk] final\n  windows: %s\n  linux  : %s\n",
                    win_raw ? win_raw : "(null)",
                    target ? target : "(null)");
            if (uri_fallback && *uri_fallback) fprintf(stderr, "  uri    : %s\n", uri_fallback);
        }
    }

    free(uri_fallback);
    free(target);
    free(win_raw);
    freeLnkInfo(&info);
    ml_free(&maps);
    free(mapPath);
    free(lnk_abs);
    return 1;

ok:
    free(uri_fallback);
    free(target);
    free(win_raw);
    freeLnkInfo(&info);
    ml_free(&maps);
    free(mapPath);
    free(lnk_abs);
    return 0;
}
