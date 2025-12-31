/*
 *  Resolve through the mapping table (MapList)
 *
 * These functions take a normalized Windows/UNC path and try to translate it
 * using rules loaded from mappings.conf.
 *
 * Important:
 *   We only return a candidate if it actually exists on disk (stat succeeds).
 */

#include "open_lnk/mapping.h"

#include "open_lnk/fs.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

char *try_map_drive_with_table(const char *winPath, const MapList *maps) {
    if (!winPath || !maps) return NULL;
    if (strlen(winPath) < 3) return NULL;
    if (!isalpha((unsigned char)winPath[0]) || winPath[1] != ':' || winPath[2] != '/') return NULL;

    char drive = (char)toupper((unsigned char)winPath[0]);
    const char *core = winPath + 2; /* substring starting at "/..." */

    for (size_t i = 0; i < maps->len; i++) {
        const MapEntry *e = &maps->items[i];
        if (e->type != MAP_DRIVE) continue;
        if (e->drive != drive) continue;
        if (!e->prefix || !*e->prefix) continue;

        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s%s", e->prefix, core);
        if (path_exists(candidate)) return strdup(candidate);
    }
    return NULL;
}

char *try_map_unc_with_table(const char *uncPath, const MapList *maps) {
    if (!uncPath || !maps) return NULL;
    if (strncmp(uncPath, "//", 2) != 0) return NULL;

    /*
     * Choose the most specific rule (longest UNC prefix).
     *
     * Example:
     *   rules:
     *     //server/share -> /mnt/share
     *     //server       -> /mnt/server
     *   input:
     *     //server/share/path/file.txt
     *   We want the longest match so we map to /mnt/share/path/file.txt.
     */
    const MapEntry *best = NULL;
    size_t bestLen = 0;

    for (size_t i = 0; i < maps->len; i++) {
        const MapEntry *e = &maps->items[i];
        if (e->type != MAP_UNC) continue;
        if (!e->unc || !*e->unc) continue;

        size_t n = strlen(e->unc);
        if (n <= bestLen) continue;

        if (strncmp(uncPath, e->unc, n) == 0) {
            /*
             * Boundary check:
             * - If the rule is "//server/share", we want to match:
             *     "//server/share" or "//server/share/..."
             * - But we do NOT want to match:
             *     "//server/shareXYZ"
             */
            if (uncPath[n] == 0 || uncPath[n] == '/') {
                best = e;
                bestLen = n;
            }
        }
    }

    if (!best || !best->prefix) return NULL;

    /* Append the remaining part ("/...") after the matched UNC prefix. */
    const char *rest = uncPath + bestLen;
    char candidate[PATH_MAX];
    snprintf(candidate, sizeof(candidate), "%s%s", best->prefix, rest);
    if (path_exists(candidate)) return strdup(candidate);

    return NULL;
}
