// src/main.c

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


typedef enum {
    RESOLVE_RAW = 0,
    RESOLVE_LOCAL = 1,
    RESOLVE_TABLE = 2,
    RESOLVE_GVFS = 3,
    RESOLVE_CIFS = 4,
    RESOLVE_MOUNTS = 5,
    RESOLVE_URI = 6,
    RESOLVE_CACHE = 7,
    RESOLVE_ASSIST = 8
} ResolveKind;

static int g_debug = 0;

static void dbg(const char *stage, const char *win_raw, const char *linux_candidate) {
    if (!g_debug) return;
    fprintf(stderr, "[open_lnk] %s\n", stage ? stage : "debug");
    fprintf(stderr, "  windows: %s\n", win_raw ? win_raw : "(null)");
    fprintf(stderr, "  linux  : %s\n", linux_candidate ? linux_candidate : "(null)");
}

static void usage(void) {
    showError("Usage: open_lnk [--debug] <file.lnk>");
}

static int looks_like_drive_path(const char *p) {
    return p && strlen(p) >= 3 &&
           isalpha((unsigned char)p[0]) &&
           p[1] == ':' &&
           p[2] == '/';
}

static int looks_like_unc_path(const char *p) {
    return p && strncmp(p, "//", 2) == 0;
}

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *r = (char*)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static char *build_candidate_from_prefix(const char *prefix, const char *drive_path) {
    if (!prefix || !*prefix || !drive_path || strlen(drive_path) < 3) return NULL;

    char cand[PATH_MAX];
    snprintf(cand, sizeof(cand), "%s%s", prefix, drive_path + 2);
    return strdup_safe(cand);
}

static int has_zenity(void) {
    return (access("/usr/bin/zenity", X_OK) == 0) || (access("/bin/zenity", X_OK) == 0);
}

static char **list_mountpoints(size_t *out_n) {
    if (out_n) *out_n = 0;

    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return NULL;

    size_t cap = 64, n = 0;
    char **arr = (char**)calloc(cap, sizeof(char*));
    if (!arr) { fclose(f); return NULL; }

    char dev[256], mnt[PATH_MAX], fstype[64];
    while (fscanf(f, "%255s %4095s %63s %*s %*d %*d\n", dev, mnt, fstype) == 3) {
        if (strcmp(mnt, "/") == 0) continue;
        if (strncmp(mnt, "/proc", 5) == 0) continue;
        if (strncmp(mnt, "/sys", 4) == 0) continue;
        if (strncmp(mnt, "/dev", 4) == 0) continue;

        if (n + 1 >= cap) {
            cap *= 2;
            char **tmp = (char**)realloc(arr, cap * sizeof(char*));
            if (!tmp) break;
            arr = tmp;
        }
        arr[n++] = strdup_safe(mnt);
    }

    fclose(f);

    if (out_n) *out_n = n;
    return arr;
}

static void free_mountpoints(char **arr, size_t n) {
    if (!arr) return;
    for (size_t i = 0; i < n; i++) free(arr[i]);
    free(arr);
}

static char *run_zenity_capture(char *const argv[], int *out_exit) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    char *buf = NULL;
    size_t cap = 0, len = 0;
    char tmp[512];

    for (;;) {
        ssize_t r = read(pipefd[0], tmp, sizeof(tmp));
        if (r <= 0) break;
        if (len + (size_t)r + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 1024;
            while (ncap < len + (size_t)r + 1) ncap *= 2;
            char *nbuf = (char*)realloc(buf, ncap);
            if (!nbuf) { free(buf); buf = NULL; break; }
            buf = nbuf;
            cap = ncap;
        }
        memcpy(buf + len, tmp, (size_t)r);
        len += (size_t)r;
    }

    close(pipefd[0]);

    int st = 0;
    waitpid(pid, &st, 0);

    int code = -1;
    if (WIFEXITED(st)) code = WEXITSTATUS(st);
    if (out_exit) *out_exit = code;

    if (!buf) return NULL;
    buf[len] = 0;

    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
        buf[len-1] = 0;
        len--;
    }

    if (buf[0] == 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

/*
 * Assistant Zenity:
 * - Liste les mountpoints (bouton OK = choisir)
 * - Bouton "Enter path..." pour entrer manuellement (zenity --entry)
 * - Cancel = annule vraiment
 */
static char *assist_choose_prefix_zenity(const char *title, const char *lnk_abs_path, const char *drive_path) {
    (void)lnk_abs_path;
    (void)drive_path;

    if (!has_zenity()) return NULL;

    size_t n = 0;
    char **mounts = list_mountpoints(&n);
    if (!mounts || n == 0) {
        free_mountpoints(mounts, n);
        return NULL;
    }

    const char *t = title ? title : "Open LNK";
    const char *text = "Select the mount point prefix for this shortcut.\n\nTip: Use 'Enter path...' if the mount is not listed.";

    /* Build argv for zenity --list */
    size_t argv_cap = 16 + n + 1;
    char **argv = (char**)calloc(argv_cap, sizeof(char*));
    if (!argv) { free_mountpoints(mounts, n); return NULL; }

    size_t i = 0;
    argv[i++] = "zenity";
    argv[i++] = "--list";
    argv[i++] = "--print-column=1";
    argv[i++] = "--title";
    argv[i++] = (char*)t;
    argv[i++] = "--text";
    argv[i++] = (char*)text;
    argv[i++] = "--column";
    argv[i++] = "Mount point";
    argv[i++] = "--ok-label=Select";
    argv[i++] = "--extra-button=Enter path...";
    argv[i++] = "--cancel-label=Cancel";

    for (size_t k = 0; k < n; k++) argv[i++] = mounts[k];
    argv[i] = NULL;

    int exit_code = -1;
    char *picked = run_zenity_capture(argv, &exit_code);

    free(argv);

    if (exit_code == 5) {
        /* Extra button -> manual entry */
        char *entry_argv[] = {
            "zenity", "--entry",
            "--title", (char*)t,
            "--text", "Enter mount point prefix (example: /mnt/GAWAIN):",
            NULL
        };
        int ec2 = -1;
        char *manual = run_zenity_capture(entry_argv, &ec2);
        free_mountpoints(mounts, n);
        if (ec2 == 0) return manual;
        free(manual);
        return NULL;
    }

    free_mountpoints(mounts, n);
    if (exit_code == 0) return picked;

    free(picked);
    return NULL;
}

int main(int argc, char *argv[]) {
    const char *envd = getenv("WINDOWS_LINK_READER_DEBUG");
    if (envd && *envd && strcmp(envd, "0") != 0) g_debug = 1;

    const char *lnk_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) { g_debug = 1; continue; }
        if (!lnk_path) lnk_path = argv[i];
    }
    if (!lnk_path) { usage(); return 1; }

    char lnk_abs[PATH_MAX];
    lnk_abs[0] = 0;
    if (realpath(lnk_path, lnk_abs) == NULL) {
        snprintf(lnk_abs, sizeof(lnk_abs), "%s", lnk_path);
    }

    FILE *f = fopen(lnk_path, "rb");
    if (!f) { showError("Cannot open .lnk file"); return 1; }

    LnkInfo info;
    if (!parse_lnk(f, &info)) { fclose(f); return 1; }
    fclose(f);

    char *target = build_best_target(&info);
    if (!target) { freeLnkInfo(&info); showError("No target path found"); return 1; }

    char *win_raw = strdup_safe(target);
    dbg("parsed", win_raw, NULL);

    normalize_backslashes(target);
    dbg("normalized", win_raw, target);

    ResolveKind rk = RESOLVE_RAW;
    if (path_exists(target)) rk = RESOLVE_LOCAL;

    MapList maps = (MapList){0};
    char *mapPath = NULL;

    const char *envp = getenv("WINDOWS_LINK_READER_MAP");
    if (envp && *envp) mapPath = strdup_safe(envp);
    else mapPath = default_map_path();

    if (mapPath) {
        load_map_file(mapPath, &maps);
    }

    char *resolved = NULL;
    char *uri_fallback = NULL;

    /* UNC pipeline (comme avant) */
    if (!path_exists(target) && looks_like_unc_path(target)) {
        resolved = try_map_unc_with_table(target, &maps);
        dbg("unc:table", win_raw, resolved ? resolved : target);
        if (resolved) { free(target); target = resolved; rk = RESOLVE_TABLE; resolved = NULL; }

        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_unc_via_gvfs(target);
            dbg("unc:gvfs", win_raw, resolved ? resolved : target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_GVFS; resolved = NULL; }
        }

        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_unc_to_cifs_mounts(target);
            dbg("unc:cifs", win_raw, resolved ? resolved : target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_CIFS; resolved = NULL; }
        }

        if (!path_exists(target)) {
            uri_fallback = unc_to_smb_uri_encoded(target);
        }
    }

    /* DRIVE pipeline (cache -> mappings -> mounts -> assistant) */
    if (!path_exists(target) && looks_like_drive_path(target)) {
        /* 1) Per-link cache */
        char *cached_prefix = cache_get_prefix_for_lnk(lnk_abs);
        if (cached_prefix && *cached_prefix) {
            char *cand = build_candidate_from_prefix(cached_prefix, target);
            dbg("drive:cache", win_raw, cand ? cand : "(null)");
            if (cand && path_exists(cand)) {
                free(target);
                target = cand;
                rk = RESOLVE_CACHE;
            } else {
                free(cand);
            }
        }
        free(cached_prefix);

        /* 2) mappings.conf */
        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_drive_with_table(target, &maps);
            dbg("drive:table", win_raw, resolved ? resolved : target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_TABLE; resolved = NULL; }
        }

        /* 3) /proc/mounts probing (scored, permissif) */
        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_drive_to_mounts_scored(target);
            dbg("drive:mounts", win_raw, resolved ? resolved : target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_MOUNTS; resolved = NULL; }
        }

        /* 4) Assistant GUI (dernier recours) */
        if (!path_exists(target)) {
            char drive = (char)toupper((unsigned char)target[0]);
            char title[128];
            snprintf(title, sizeof(title), "Open LNK (Drive %c:)", drive);

            char *prefix = assist_choose_prefix_zenity(title, lnk_abs, target);
            if (prefix && *prefix) {
                char *cand = build_candidate_from_prefix(prefix, target);
                dbg("drive:assist", win_raw, cand ? cand : "(null)");
                if (cand && path_exists(cand)) {
                    cache_set_prefix_for_lnk(lnk_abs, prefix);
                    free(target);
                    target = cand;
                    rk = RESOLVE_ASSIST;
                } else {
                    free(cand);
                }
            }
            free(prefix);
        }
    }

    int rc = 0;

    if (path_exists(target)) {
        dbg("open:path", win_raw, target);
        rc = open_with_desktop(target);

        /* fallback: ouvrir le dossier parent si l’ouverture échoue */
        if (rc != 0) {
            char *dup = strdup_safe(target);
            if (dup) {
                char *slash = strrchr(dup, '/');
                if (slash) {
                    *slash = 0;
                    if (dup[0] != 0 && path_exists(dup)) rc = open_with_desktop(dup);
                }
                free(dup);
            }
        }
    } else {
        if (uri_fallback && *uri_fallback) {
            dbg("open:uri", win_raw, uri_fallback);
            rc = open_with_desktop(uri_fallback);
            if (rc == 0) rk = RESOLVE_URI;
        } else {
            rc = -1;
        }
    }

    if (rc != 0) {
        char buf[1024];
        if (uri_fallback) snprintf(buf, sizeof(buf), "Failed to open: %s (and %s)", target, uri_fallback);
        else snprintf(buf, sizeof(buf), "Failed to open: %s", target);
        showError(buf);

        if (g_debug) {
            fprintf(stderr, "[open_lnk] final\n  windows: %s\n  linux  : %s\n",
                    win_raw ? win_raw : "(null)",
                    target ? target : "(null)");
            if (uri_fallback && *uri_fallback) fprintf(stderr, "  uri    : %s\n", uri_fallback);
            fprintf(stderr, "  rk     : %d\n", (int)rk);
        }

        free(uri_fallback);
        free(target);
        free(win_raw);
        freeLnkInfo(&info);
        ml_free(&maps);
        free(mapPath);
        return 1;
    }

    free(uri_fallback);
    free(target);
    free(win_raw);
    freeLnkInfo(&info);
    ml_free(&maps);
    free(mapPath);
    return 0;
}
