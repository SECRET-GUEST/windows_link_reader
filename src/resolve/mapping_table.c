/*
 * Resolve through the mapping table (MapList).
 *
 * These functions take a normalized drive/UNC path and try to translate it
 * using rules loaded from mappings.conf.
 *
 * Important:
 *   Local filesystem mappings are returned only if the candidate exists.
 *   SMB URI mappings are returned without stat(), because smb:// is not a
 *   local filesystem path.
 */

#include "open_lnk/mapping.h"

#include "open_lnk/fs.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int is_smb_uri_prefix(const char *prefix) {
    if (!prefix) return 0;
    return strncasecmp(prefix, "smb://", 6) == 0;
}

static char *join_prefix_and_rest(const char *prefix, const char *rest) {
    if (!prefix || !*prefix) return NULL;
    if (!rest) rest = "";

    size_t prefix_len = strlen(prefix);
    size_t rest_len = strlen(rest);

    int prefix_has_slash = prefix_len > 0 && prefix[prefix_len - 1] == '/';
    int rest_has_slash = rest_len > 0 && rest[0] == '/';

    char candidate[PATH_MAX];

    if (rest_len == 0) {
        snprintf(candidate, sizeof(candidate), "%s", prefix);
    } else if (prefix_has_slash && rest_has_slash) {
        snprintf(candidate, sizeof(candidate), "%.*s%s", (int)(prefix_len - 1), prefix, rest);
    } else if (!prefix_has_slash && !rest_has_slash) {
        snprintf(candidate, sizeof(candidate), "%s/%s", prefix, rest);
    } else {
        snprintf(candidate, sizeof(candidate), "%s%s", prefix, rest);
    }

    return strdup(candidate);
}

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

        char *candidate = join_prefix_and_rest(e->prefix, core);
        if (!candidate) continue;

        if (path_exists(candidate)) {
            return candidate;
        }

        free(candidate);
    }

    return NULL;
}

char *try_map_unc_with_table(const char *uncPath, const MapList *maps) {
    if (!uncPath || !maps) return NULL;
    if (strncmp(uncPath, "//", 2) != 0) return NULL;

    /*
     * Choose the most specific rule.
     *
     * Example:
     *   //server/share -> /mnt/share
     *   //server       -> /mnt/server
     *
     * Input:
     *   //server/share/path/file.txt
     *
     * Expected result:
     *   /mnt/share/path/file.txt
     */
    const MapEntry *best = NULL;
    size_t bestLen = 0;

    for (size_t i = 0; i < maps->len; i++) {
        const MapEntry *e = &maps->items[i];

        if (e->type != MAP_UNC) continue;
        if (!e->unc || !*e->unc) continue;

        size_t n = strlen(e->unc);
        if (n <= bestLen) continue;

        if (strncasecmp(uncPath, e->unc, n) == 0) {
            /*
             * Boundary check:
             *   Match:
             *     //server/share
             *     //server/share/...
             *
             *   Do not match:
             *     //server/shareXYZ
             */
            if (uncPath[n] == 0 || uncPath[n] == '/') {
                best = e;
                bestLen = n;
            }
        }
    }

    if (!best || !best->prefix || !*best->prefix) return NULL;

    const char *rest = uncPath + bestLen;
    char *candidate = join_prefix_and_rest(best->prefix, rest);

    if (!candidate) return NULL;

    if (is_smb_uri_prefix(best->prefix)) {
        return candidate;
    }

    if (path_exists(candidate)) {
        return candidate;
    }

    free(candidate);
    return NULL;
}