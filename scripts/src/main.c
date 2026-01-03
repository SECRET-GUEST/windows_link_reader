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

typedef enum {
    RESOLVE_RAW = 0,
    RESOLVE_LOCAL = 1,
    RESOLVE_TABLE = 2,
    RESOLVE_GVFS = 3,
    RESOLVE_CIFS = 4,
    RESOLVE_MOUNTS = 5,
    RESOLVE_URI = 6
} ResolveKind;

static int g_debug = 0;
static int g_assist = 0;

static void dbg(const char *stage, const char *win_raw, const char *linux_candidate) {
    if (!g_debug) return;
    fprintf(stderr, "[open_lnk] %s\n", stage ? stage : "debug");
    fprintf(stderr, "  windows: %s\n", win_raw ? win_raw : "(null)");
    fprintf(stderr, "  linux  : %s\n", linux_candidate ? linux_candidate : "(null)");
}

static void usage(void) {
    showError("Usage: open_lnk [--debug] [--assist] <file.lnk>");
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

    MapList maps = {0};
    char *mapPath = NULL;

    const char *envp = getenv("WINDOWS_LINK_READER_MAP");
    if (envp && *envp) mapPath = strdup(envp);
    else mapPath = default_map_path();

    if (mapPath) {
        if (g_assist) {
            /* Ensure the mapping file exists so users can find/edit it. */
            FILE *mf = fopen(mapPath, "a");
            if (mf) fclose(mf);
        }
        load_map_file(mapPath, &maps);
    }

    char *resolved = NULL;
    char *uri_fallback = NULL;

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

    if (!path_exists(target) && looks_like_drive_path(target)) {
        resolved = try_map_drive_with_table(target, &maps);
        dbg("drive:table", win_raw, resolved ? resolved : target);
        if (resolved) { free(target); target = resolved; rk = RESOLVE_TABLE; resolved = NULL; }

        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_drive_to_mounts_scored(target);
            dbg("drive:mounts", win_raw, resolved ? resolved : target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_MOUNTS; resolved = NULL; }
        }

        if (!path_exists(target) && rk == RESOLVE_RAW) {
            char drive = (char)toupper((unsigned char)target[0]);
            char *pfx = prompt_for_prefix_drive_any(drive);
            if (pfx) {
                char cand[PATH_MAX];
                snprintf(cand, sizeof(cand), "%s%s", pfx, target + 2);
                dbg("drive:prompt", win_raw, cand);
                if (path_exists(cand)) {
                    free(target);
                    target = strdup(cand);
                    rk = RESOLVE_TABLE;
                    if (mapPath) append_drive_map_file(mapPath, drive, pfx);
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

        if (g_assist && mapPath) {
            (void)open_with_desktop(mapPath);
        }
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
