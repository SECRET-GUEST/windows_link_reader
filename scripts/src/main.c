/*
 * open_lnk - Windows .lnk reader / opener
 *
 * High-level flow (what this program does):
 *  1) Parse the .lnk file and extract the fields we care about (LnkInfo).
 *  2) Build the most useful "target" string we can (still in Windows syntax).
 *  3) Convert backslashes to slashes so Unix APIs can reason about the path.
 *     (This makes `stat()` and our heuristics work on Linux/macOS.)
 *  4) Resolve Windows-style locations to a real local path or a URI:
 *       - If the target already exists locally -> open it immediately.
 *       - UNC paths ("//server/share/..."):
 *           mapping table -> GVFS -> CIFS mounts -> encoded smb:// fallback
 *       - Drive paths ("X:/..."):
 *           mapping table -> /proc/mounts scoring -> interactive prompt
 *  5) Open the final path/URI with the system default handler (xdg-open/open).
 *
 * Memory/ownership:
 *   Most helper functions return heap-allocated strings. This file is careful
 *   to free everything before exiting, no matter which branch we take.
 */

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
#include <stdlib.h>
#include <string.h>

typedef enum {
    RESOLVE_RAW = 0,
    RESOLVE_LOCAL = 1,
    RESOLVE_TABLE = 2,
    RESOLVE_GVFS = 3,
    RESOLVE_CIFS = 4,
    RESOLVE_MOUNTS = 5,
    RESOLVE_URI = 6
} ResolveKind;

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
    if (argc != 2) { showError("Usage: open_lnk <file.lnk>"); return 1; }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { showError("Cannot open .lnk file"); return 1; }

    LnkInfo info;
    if (!parse_lnk(f, &info)) { fclose(f); return 1; }
    fclose(f);

    char *target = build_best_target(&info);
    if (!target) { freeLnkInfo(&info); showError("No target path found"); return 1; }

    /*
     * The .lnk format uses Windows paths, but we are running on Unix.
     * We convert '\' -> '/' so that:
     *   - `path_exists()` can test local files correctly
     *   - our "looks_like_drive_path/unc" checks see "X:/..." and "//server/..."
     */
    normalize_backslashes(target);

    ResolveKind rk = RESOLVE_RAW;
    if (path_exists(target)) rk = RESOLVE_LOCAL;

    /*
     * Load mapping rules (optional).
     * - If the file does not exist, that's fine; we just keep an empty MapList.
     * - You can override the mapping path with $WINDOWS_LINK_READER_MAP.
     */
    MapList maps = {0};
    char *mapPath = NULL;
    const char *envp = getenv("WINDOWS_LINK_READER_MAP");
    if (envp && *envp) mapPath = strdup(envp);
    else mapPath = default_map_path();
    if (mapPath) load_map_file(mapPath, &maps);

    char *resolved = NULL;
    char *uri_fallback = NULL;

    /*
     * UNC resolution layers (in order):
     *   1) mappings.conf (manual mappings)
     *   2) GVFS (common in GNOME desktops)
     *   3) /proc/mounts CIFS mount (system-level mount)
     *   4) smb:// URI fallback (so the desktop can still open a share)
     */
    if (!path_exists(target) && looks_like_unc_path(target)) {
        resolved = try_map_unc_with_table(target, &maps);
        if (resolved) { free(target); target = resolved; rk = RESOLVE_TABLE; resolved = NULL; }

        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_unc_via_gvfs(target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_GVFS; resolved = NULL; }
        }

        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_unc_to_cifs_mounts(target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_CIFS; resolved = NULL; }
        }

        if (!path_exists(target)) {
            uri_fallback = unc_to_smb_uri_encoded(target);
        }
    }

    /*
     * Drive-letter resolution layers (in order):
     *   1) mappings.conf (manual mappings)
     *   2) /proc/mounts scoring (automatic best guess)
     *   3) interactive prompt (ask the user to type the mount prefix)
     */
    if (!path_exists(target) && looks_like_drive_path(target)) {
        resolved = try_map_drive_with_table(target, &maps);
        if (resolved) { free(target); target = resolved; rk = RESOLVE_TABLE; resolved = NULL; }

        if (!path_exists(target) && rk == RESOLVE_RAW) {
            resolved = try_map_drive_to_mounts_scored(target);
            if (resolved) { free(target); target = resolved; rk = RESOLVE_MOUNTS; resolved = NULL; }
        }

        if (!path_exists(target) && rk == RESOLVE_RAW) {
            char drive = (char)toupper((unsigned char)target[0]);
            char *pfx = prompt_for_prefix_drive(drive);
            if (pfx) {
                char cand[PATH_MAX];
                /*
                 * `target` looks like "X:/something".
                 * - `target + 2` points to the substring starting at "/something"
                 * - the user provided a Linux prefix like "/media/me/X_Drive"
                 * Result is a Linux candidate like:
                 *   "/media/me/X_Drive" + "/something"
                 */
                snprintf(cand, sizeof(cand), "%s%s", pfx, target + 2);
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
        /* The path exists locally: ask the desktop to open it. */
        rc = open_with_desktop(target);
    } else {
        if (uri_fallback && *uri_fallback) {
            /* Local path not found, but we do have a smb:// fallback. */
            rc = open_with_desktop(uri_fallback);
            if (rc == 0) rk = RESOLVE_URI;
        } else {
            rc = -1;
        }

        /*
         * Optional fallback: open the parent folder.
         *
         * We only do this when `rk != RESOLVE_RAW` because:
         *   - RESOLVE_RAW means `target` is still a Windows-ish path like "X:/..."
         *     which is not a meaningful Linux directory to open.
         */
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

        free(uri_fallback);
        free(target);
        freeLnkInfo(&info);
        ml_free(&maps);
        free(mapPath);
        return 1;
    }

    free(uri_fallback);
    free(target);
    freeLnkInfo(&info);
    ml_free(&maps);
    free(mapPath);
    return 0;
}
