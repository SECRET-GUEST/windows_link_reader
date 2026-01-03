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
#include <sys/stat.h>
#include <unistd.h>

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
    RESOLVE_CACHE = 7
} ResolveKind;

static int g_debug = 0;
static int g_assist = 0; /* legacy flag, kept for now */

/* -------- debug -------- */

static void dbg(const char *stage, const char *win_raw, const char *linux_candidate) {
    if (!g_debug) return;
    fprintf(stderr, "[open_lnk] %s\n", stage ? stage : "debug");
    fprintf(stderr, "  windows: %s\n", win_raw ? win_raw : "(null)");
    fprintf(stderr, "  linux  : %s\n", linux_candidate ? linux_candidate : "(null)");
}

static void usage(void) {
    showError("Usage: open_lnk [--debug] [--assist] <file.lnk>");
}

/* -------- small helpers -------- */

static int looks_like_drive_path(const char *p) {
    return p && strlen(p) >= 3 &&
           isalpha((unsigned char)p[0]) &&
           p[1] == ':' &&
           p[2] == '/';
}

static int looks_like_unc_path(const char *p) {
    return p && strncmp(p, "//", 2) == 0;
}

static int is_tty_stdin(void) {
    return isatty(STDIN_FILENO);
}

static void rstrip_newline(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = 0;
}

static int mkdir_p(const char *dir) {
    if (!dir || !*dir) return -1;

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", dir);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            (void)mkdir(tmp, 0755);
            *p = '/';
        }
    }
    (void)mkdir(tmp, 0755);
    return 0;
}

static void ensure_parent_dir(const char *path) {
    if (!path) return;
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash) return;
    *slash = 0;
    (void)mkdir_p(tmp);
}

/* -------- cache: per .lnk absolute path --------
 * Format: <abs_lnk_path>=<prefix>
 * Example:
 * /home/me/Desktop/foo.lnk=/run/media/me/DISK_A
 *
 * latest-wins on read
 * rewrite on write (no duplicates)
 * safe write: tmp + rename
 */

static char* xdg_cache_links_path(void) {
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    if (!home || !*home) return NULL;

    char buf[PATH_MAX];
    if (xdg && *xdg) {
        snprintf(buf, sizeof(buf), "%s/windows-link-reader/links.conf", xdg);
    } else {
        snprintf(buf, sizeof(buf), "%s/.cache/windows-link-reader/links.conf", home);
    }
    return strdup(buf);
}

static char* cache_get_prefix_for_lnk(const char *lnk_abs_path) {
    if (!lnk_abs_path || !*lnk_abs_path) return NULL;

    char *cachePath = xdg_cache_links_path();
    if (!cachePath) return NULL;

    FILE *f = fopen(cachePath, "r");
    free(cachePath);
    if (!f) return NULL;

    char line[8192];
    char *found = NULL;

    while (fgets(line, sizeof(line), f)) {
        rstrip_newline(line);
        if (line[0] == '#' || line[0] == 0) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;

        const char *k = line;
        const char *v = eq + 1;

        if (strcmp(k, lnk_abs_path) == 0 && v && *v) {
            free(found);
            found = strdup(v); /* latest-wins */
        }
    }

    fclose(f);
    return found;
}

static void cache_set_prefix_for_lnk(const char *lnk_abs_path, const char *prefix) {
    if (!lnk_abs_path || !*lnk_abs_path || !prefix || !*prefix) return;

    char *cachePath = xdg_cache_links_path();
    if (!cachePath) return;

    ensure_parent_dir(cachePath);

    char tmpPath[PATH_MAX];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", cachePath);

    FILE *in = fopen(cachePath, "r");
    FILE *out = fopen(tmpPath, "w");
    if (!out) {
        if (in) fclose(in);
        free(cachePath);
        return;
    }

    int replaced = 0;
    char line[8192];

    if (in) {
        while (fgets(line, sizeof(line), in)) {
            char orig[8192];
            snprintf(orig, sizeof(orig), "%s", line);

            rstrip_newline(line);

            if (line[0] == '#' || line[0] == 0) {
                fputs(orig, out);
                continue;
            }

            char *eq = strchr(line, '=');
            if (!eq) {
                fputs(orig, out);
                continue;
            }

            *eq = 0;
            const char *k = line;

            if (strcmp(k, lnk_abs_path) == 0) {
                fprintf(out, "%s=%s\n", lnk_abs_path, prefix);
                replaced = 1;
            } else {
                fputs(orig, out);
                if (orig[strlen(orig)-1] != '\n') fputc('\n', out);
            }
        }
        fclose(in);
    }

    if (!replaced) {
        fprintf(out, "%s=%s\n", lnk_abs_path, prefix);
    }

    fclose(out);
    (void)rename(tmpPath, cachePath);
    free(cachePath);
}

/* -------- GUI assist (zenity) --------
 * Goal: if we can't resolve a drive path and there is no TTY,
 * show a mount list and let user pick a prefix.
 *
 * We keep it simple: list mount points from /proc/mounts (filtered),
 * user picks one, we use it as prefix.
 */

static int is_safe_mount(const char *mnt) {
    const char *skip[] = { "/proc", "/sys", "/dev", "/run", "/snap", "/var/lib/snapd", NULL };
    for (int i = 0; skip[i]; ++i) {
        size_t n = strlen(skip[i]);
        if (strncmp(mnt, skip[i], n) == 0) return 0;
    }
    return 1;
}

static void unescape_mount_field(char *s) {
    /* /proc/mounts escapes spaces as \040 etc. Handle the common ones. */
    if (!s) return;
    char *w = s;
    for (char *p = s; *p; ++p) {
        if (p[0] == '\\' && p[1] && p[2] && p[3]) {
            if (!strncmp(p, "\\040", 4)) { *w++ = ' '; p += 3; continue; }
            if (!strncmp(p, "\\011", 4)) { *w++ = '\t'; p += 3; continue; }
            if (!strncmp(p, "\\012", 4)) { *w++ = '\n'; p += 3; continue; }
            if (!strncmp(p, "\\134", 4)) { *w++ = '\\'; p += 3; continue; }
        }
        *w++ = *p;
    }
    *w = 0;
}

static char* zenity_list_mounts_pick(void) {
    /* Build a temp file with one mountpoint per line, then ask zenity to pick. */
    const char *tmp = "/tmp/open_lnk_mounts.txt";
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return NULL;

    FILE *o = fopen(tmp, "w");
    if (!o) { fclose(f); return NULL; }

    char dev[512], mnt[512], fst[128], rest[1024];
    while (fscanf(f, "%511s %511s %127s %1023[^\n]\n", dev, mnt, fst, rest) == 4) {
        unescape_mount_field(mnt);
        if (!is_safe_mount(mnt)) continue;
        fprintf(o, "%s\n", mnt);
    }

    fclose(o);
    fclose(f);

    /* zenity --list returns selected row on stdout */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "zenity --list --title='Open LNK' "
             "--text='Select the Linux mount prefix' "
             "--column='Mount point' --width=700 --height=500 "
             "--print-column=1 "
             "$(awk '{print \"\\\"\"$0\"\\\"\"}' %s)",
             tmp);

    FILE *p = popen(cmd, "r");
    if (!p) return NULL;

    char out[PATH_MAX];
    if (!fgets(out, sizeof(out), p)) { pclose(p); return NULL; }
    pclose(p);

    rstrip_newline(out);
    if (!out[0]) return NULL;
    return strdup(out);
}

static char* zenity_entry_prefix(const char *suggest) {
    char cmd[2048];
    const char *s = (suggest && *suggest) ? suggest : "/media";
    snprintf(cmd, sizeof(cmd),
             "zenity --entry --title='Open LNK' "
             "--text='Enter the Linux mount prefix (example: /run/media/<user>/DISK)' "
             "--entry-text='%s'",
             s);

    FILE *p = popen(cmd, "r");
    if (!p) return NULL;

    char out[PATH_MAX];
    if (!fgets(out, sizeof(out), p)) { pclose(p); return NULL; }
    pclose(p);

    rstrip_newline(out);
    if (!out[0]) return NULL;
    return strdup(out);
}

/* -------- main -------- */

int main(int argc, char *argv[]) {
    const char *envd = getenv("WINDOWS_LINK_READER_DEBUG");
    if (envd && *envd && strcmp(envd, "0") != 0) g_debug = 1;

    const char *enva = getenv("WINDOWS_LINK_READER_ASSIST");
    if (enva && *enva && strcmp(enva, "0") != 0) g_assist = 1;

    const char *lnk_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) { g_debug = 1; continue; }
        if (strcmp(argv[i], "--assist") == 0) { g_assist = 1; continue; }
        if (!lnk_path) lnk_path = argv[i];
    }
    if (!lnk_path) { usage(); return 1; }

    /* absolute path for cache key */
    char lnk_abs[PATH_MAX];
    const char *lnk_key = lnk_path;
    if (realpath(lnk_path, lnk_abs)) lnk_key = lnk_abs;

    FILE *f = fopen(lnk_path, "rb");
    if (!f) { showError("Cannot open .lnk file"); return 1; }

    LnkInfo info;
    if (!parse_lnk(f, &info)) { fclose(f); return 1; }
    fclose(f);

    char *target = build_best_target(&info);
    if (!target) { freeLnkInfo(&info); showError("No target path found"); return 1; }

    char *win_raw = strdup(target);
    dbg("parsed", win_raw, NULL);

    normalize_backslashes(target);
    dbg("normalized", win_raw, target);

    ResolveKind rk = RESOLVE_RAW;
    if (path_exists(target)) rk = RESOLVE_LOCAL;

    MapList maps = (MapList){0};
    char *mapPath = NULL;

    const char *envp = getenv("WINDOWS_LINK_READER_MAP");
    if (envp && *envp) mapPath = strdup(envp);
    else mapPath = default_map_path();

    if (mapPath) {
        if (g_assist) {
            FILE *mf = fopen(mapPath, "a");
            if (mf) fclose(mf);
        }
        load_map_file(mapPath, &maps);
    }

    char *resolved = NULL;
    char *uri_fallback = NULL;

    /* UNC flow */
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

    /* Drive flow */
    if (!path_exists(target) && looks_like_drive_path(target)) {
        /* 0) per-link cache first */
        char *cached = cache_get_prefix_for_lnk(lnk_key);
        if (cached) {
            char cand[PATH_MAX];
            snprintf(cand, sizeof(cand), "%s%s", cached, target + 2);
            dbg("drive:cache", win_raw, cand);
            if (path_exists(cand)) {
                free(target);
                target = strdup(cand);
                rk = RESOLVE_CACHE;
            }
            free(cached);
        }

        /* 1) mapping table */
        if (!path_exists(target)) {
            resolved = try_map_drive_with_table(target, &maps);
            dbg("drive:table", win_raw, resolved ? resolved : target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_TABLE; resolved = NULL; }
        }

        /* 2) mounts scoring */
        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_drive_to_mounts_scored(target);
            dbg("drive:mounts", win_raw, resolved ? resolved : target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_MOUNTS; resolved = NULL; }
        }

        /* 3) interactive fallback:
         *    - terminal: prompt_for_prefix_drive(drive) (existing function from mapping_file.c)
         *    - GUI: zenity list + optional entry
         */
        if (!path_exists(target) && rk == RESOLVE_RAW) {
            char drive = (char)toupper((unsigned char)target[0]);

            char *pfx = NULL;

            if (is_tty_stdin()) {
                /* mapping_file.c provides this */
                pfx = prompt_for_prefix_drive(drive);
            } else {
                /* GUI path: choose from mounts */
                pfx = zenity_list_mounts_pick();
                if (!pfx) {
                    /* fallback: entry dialog */
                    pfx = zenity_entry_prefix("/run/media");
                }
            }

            if (pfx) {
                char cand[PATH_MAX];
                snprintf(cand, sizeof(cand), "%s%s", pfx, target + 2);
                dbg("drive:prompt", win_raw, cand);

                if (path_exists(cand)) {
                    /* persist per-link cache (only for this .lnk) */
                    cache_set_prefix_for_lnk(lnk_key, pfx);

                    free(target);
                    target = strdup(cand);
                    rk = RESOLVE_CACHE;

                    /* optionally also add drive mapping (global) if you still want it:
                     * append_drive_map_file(mapPath, drive, pfx);
                     */
                }

                free(pfx);
            }
        }
    }

    int rc = 0;

    if (path_exists(target)) {
        dbg("open:path", win_raw, target);
        rc = open_with_desktop(target);
    } else {
        if (uri_fallback && *uri_fallback) {
            dbg("open:uri", win_raw, uri_fallback);
            rc = open_with_desktop(uri_fallback);
            if (rc == 0) rk = RESOLVE_URI;
        } else {
            rc = -1;
        }

        /* parent fallback if we had any resolution attempt */
        if (rc != 0 && rk != RESOLVE_RAW) {
            char *dup = strdup(target);
            if (dup) {
                char *slash = strrchr(dup, '/');
                if (slash) {
                    *slash = 0;
                    rc = path_exists(dup) ? open_with_desktop(dup) : -1;
                }
                free(dup);
            }
        }
    }

    if (rc != 0) {
        char buf[1024];
        if (uri_fallback) snprintf(buf, sizeof(buf), "Failed to open: %s (and %s)", target, uri_fallback);
        else snprintf(buf, sizeof(buf), "Failed to open: %s", target);
        showError(buf);

        if (g_debug || g_assist) {
            fprintf(stderr, "[open_lnk] final\n  windows: %s\n  linux  : %s\n",
                    win_raw ? win_raw : "(null)",
                    target ? target : "(null)");
            if (uri_fallback && *uri_fallback) fprintf(stderr, "  uri    : %s\n", uri_fallback);
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
