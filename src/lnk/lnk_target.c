/*
 *  Build the best "target" path (Windows semantics)
 *
 * A .lnk file can store the target in multiple ways, for example:
 *  - LocalBasePath (+ an optional Unicode variant)
 *  - CommonPathSuffix (+ an optional Unicode variant)
 *  - RelativePath + WorkingDir, etc.
 *
 * Here we try to build a single consistent target string.
 *
 * Important choice:
 *   We keep Windows separators ('\\') in this module because the .lnk data is
 *   Windows-native. The caller (main) later converts '\\' -> '/' when it needs
 *   to interact with Unix filesystems.
 */

#include "open_lnk/lnk.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int looks_like_drive_path(const char *p) {
    if (!p || strlen(p) < 3) return 0;
    if (!isalpha((unsigned char)p[0])) return 0;
    if (p[1] != ':') return 0;
    if (p[2] != '\\' && p[2] != '/') return 0;
    return 1;
}

static int looks_like_drive_root(const char *p) {
    if (!p || strlen(p) != 2) return 0;
    return (isalpha((unsigned char)p[0]) && p[1] == ':');
}

static int looks_like_unc_path(const char *p) {
    if (!p || strlen(p) < 5) return 0;
    return ((p[0] == '\\' && p[1] == '\\') || (p[0] == '/' && p[1] == '/'));
}

/*
 * Normalize a UNC root string to start with "\\\\" and use backslashes.
 *
 * Examples:
 *   "\\\\server\\share"  -> unchanged
 *   "\\server\\share"    -> "\\\\server\\share"
 *   "//server/share"     -> "\\\\server\\share"
 *   "server\\share"      -> "\\\\server\\share"
 */
static char *normalize_unc_root(const char *s) {
    if (!s || !*s) return NULL;

    /* First normalize all '/' to '\\' so we keep Windows semantics here. */
    char *tmp = strdup(s);
    if (!tmp) return NULL;
    for (char *p = tmp; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    if (tmp[0] == '\\' && tmp[1] == '\\') {
        return tmp;
    }

    size_t n = strlen(tmp);
    if (tmp[0] == '\\') {
        /* Single leading backslash -> add one more. */
        char *out = (char*)malloc(n + 2);
        if (!out) { free(tmp); return NULL; }
        out[0] = '\\';
        memcpy(out + 1, tmp, n + 1);
        free(tmp);
        return out;
    }

    /* No leading backslash -> add two. */
    char *out = (char*)malloc(n + 3);
    if (!out) { free(tmp); return NULL; }
    out[0] = '\\';
    out[1] = '\\';
    memcpy(out + 2, tmp, n + 1);
    free(tmp);
    return out;
}

/*
 * Case-insensitive "prefix match".
 *
 * The name is historical: we use it to compare strings in a case-insensitive
 * way (Windows paths are often case-insensitive).
 */
static int starts_with_ci(const char *s, const char *pfx) {
    if (!s || !pfx) return 0;
    while (*pfx) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*pfx)) return 0;
        s++; pfx++;
    }
    return 1;
}

/*
 * Join a base path and a suffix into a single Windows-style path.
 *
 * Why this exists:
 *   Some .lnk files store:
 *     - a "base" path (ex: "C:\\Users\\me")
 *     - and a "suffix" (ex: "Documents\\file.txt")
 *   We want to output: "C:\\Users\\me\\Documents\\file.txt"
 *
 * Return:
 *   - newly allocated string (caller must free())
 *   - NULL on allocation failure or invalid inputs
 */
static char *join_win_paths(const char *base, const char *suffix) {
    if (!base || !*base) return NULL;
    if (!suffix || !*suffix) return strdup(base);

    size_t nb = strlen(base);
    size_t ns = strlen(suffix);

    /*
     * Common situation: `base` already ends with `suffix` (duplicate data).
     *
     * The pointer expression `base + (nb - ns)` means:
     *   "start comparing at the last `ns` characters of `base`".
     * So we can test if:
     *   base ends_with suffix   (case-insensitive)
     * and if yes, we return `base` as-is to avoid double-appending.
     */
    if (nb >= ns && starts_with_ci(base + (nb - ns), suffix)) {
        return strdup(base);
    }

    /*
     * Decide whether we need to insert a path separator between base and suffix.
     * We insert ONE backslash if:
     *   - base does NOT already end with '/' or '\\'
     *   - suffix does NOT already start with '/' or '\\'
     */
    int need_sep = 1;
    if (nb > 0 && (base[nb - 1] == '\\' || base[nb - 1] == '/')) need_sep = 0;
    if (ns > 0 && (suffix[0] == '\\' || suffix[0] == '/')) need_sep = 0;

    /* Total size = base + optional separator + suffix + final NUL byte. */
    size_t n = nb + (need_sep ? 1 : 0) + ns + 1;
    char *out = (char*)malloc(n);
    if (!out) return NULL;

    /*
     * In a C string literal, "\\\\" becomes a single backslash character.
     * So "%s\\%s" prints: base + '\' + suffix.
     */
    if (need_sep) snprintf(out, n, "%s\\%s", base, suffix);
    else snprintf(out, n, "%s%s", base, suffix);

    return out;
}

/*
 * Build the best target string from the parsed fields.
 *
 * Strategy (from most reliable to least reliable):
 *   1) If we have (base + suffix), join them.
 *   2) If we only have base, use base.
 *   3) If we have WorkingDir + RelativePath, join them.
 *   4) If we only have RelativePath, use it.
 *   5) If we only have suffix, use it.
 */
char *build_best_target(LnkInfo *li) {
    if (!li) return NULL;

    const char *base_local = li->localBasePathU ? li->localBasePathU : li->localBasePath;
    const char *base_net_raw = li->netNameU ? li->netNameU : li->netName;
    const char *base_dev = li->deviceNameU ? li->deviceNameU : li->deviceName;
    const char *suf  = li->commonPathSuffixU ? li->commonPathSuffixU : li->commonPathSuffix;

    char *base_net_norm = NULL;
    const char *base_net = NULL;
    if (base_net_raw && *base_net_raw) {
        base_net_norm = normalize_unc_root(base_net_raw);
        if (base_net_norm && *base_net_norm) base_net = base_net_norm;
    }

    const char *base = base_local;

    /*
     * Prefer UNC base when available:
     * - Network shortcuts often also store a drive letter (M:).
     * - The UNC ("\\\\server\\\\share") is more portable for Linux resolution.
     */
    if (base_net && *base_net && looks_like_unc_path(base_net)) {
        if (!base || !*base || looks_like_drive_path(base) || looks_like_drive_root(base)) {
            base = base_net;
        }
    }

    /* If LocalBasePath is empty, fall back to DeviceName (ex: "M:"). */
    if ((!base || !*base) && base_dev && *base_dev) {
        base = base_dev;
    }

    char *candidate = NULL;
    if (base && *base && suf && *suf) candidate = join_win_paths(base, suf);
    if (!candidate && base && *base) candidate = strdup(base);

    /* Fallback: WorkingDir + RelativePath, only if both are present and non-empty. */
    if (!candidate && li->workingDir && li->relativePath && *li->workingDir && *li->relativePath) {
        size_t n = strlen(li->workingDir) + 1 + strlen(li->relativePath) + 1;
        char *s = (char*)malloc(n);
        if (!s) { free(base_net_norm); return NULL; }
        snprintf(s, n, "%s\\%s", li->workingDir, li->relativePath);
        candidate = s;
    }

    if (!candidate && li->relativePath && *li->relativePath) candidate = strdup(li->relativePath);
    if (!candidate && suf && *suf) candidate = strdup(suf);

    /*
     * If our best candidate is NOT a drive/UNC path (common for network shortcuts
     * when LinkInfo is incomplete), fall back to the best-effort IDList extraction.
     */
    if ((!candidate || (!looks_like_drive_path(candidate) && !looks_like_unc_path(candidate))) &&
        li->idListPath && *li->idListPath &&
        (looks_like_drive_path(li->idListPath) || looks_like_unc_path(li->idListPath))) {
        free(candidate);
        free(base_net_norm);
        return strdup(li->idListPath);
    }

    free(base_net_norm);
    return candidate;
}
