/*
 *  UNC resolution via GVFS (GNOME)
 *
 * When an SMB share is mounted through a GNOME file manager, it can appear as:
 *   /run/user/<uid>/gvfs/smb-share:server=SERVER,share=SHARE[,...]
 *
 * We scan that directory to find the matching server/share and then build a
 * local filesystem path for the requested UNC.
 */

#include "open_lnk/gvfs.h"

#include "open_lnk/fs.h"
#include "open_lnk/unc.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

static int gvfs_extract_kv(const char *name, const char *key, char *out, size_t outsz) {
    /*
     * The GVFS folder name contains comma-separated key/value pairs, e.g.:
     *   "smb-share:server=NAS,share=Public"
     *
     * This helper extracts the raw value part (up to the next comma).
     */
    const char *p = strstr(name, key);
    if (!p) return 0;
    p += strlen(key);
    const char *end = strchr(p, ',');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    if (n == 0 || n >= outsz) return 0;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

char *try_map_unc_via_gvfs(const char *uncPath) {
    if (!uncPath || strncmp(uncPath, "//", 2) != 0) return NULL;

    /* Extract "//server/share" and keep `rest` (optional "/sub/path"). */
    char server[256], share[256];
    const char *rest = NULL;
    if (!parse_unc_share(uncPath, server, sizeof(server), share, sizeof(share), &rest)) return NULL;

    /* The GVFS mount root is per-user (uid). */
    uid_t uid = getuid();
    char gvfs_base[PATH_MAX];
    snprintf(gvfs_base, sizeof(gvfs_base), "/run/user/%u/gvfs", (unsigned)uid);

    DIR *d = opendir(gvfs_base);
    if (!d) return NULL;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (strncmp(de->d_name, "smb-share:", 10) != 0) continue;

        char s_server[256], s_share[256];
        if (!gvfs_extract_kv(de->d_name, "server=", s_server, sizeof(s_server))) continue;
        if (!gvfs_extract_kv(de->d_name, "share=", s_share, sizeof(s_share))) continue;

        if (strcasecmp(s_server, server) != 0) continue;
        if (strcasecmp(s_share, share) != 0) continue;

        char candidate[PATH_MAX];
        const char *rest_s = rest ? rest : "";
        size_t gb = strlen(gvfs_base);
        size_t dn = strlen(de->d_name);
        size_t rn = strlen(rest_s);
        if (gb + 1 + dn + rn + 1 > sizeof(candidate)) continue;

        /*
         * Build:
         *   <gvfs_base> "/" <dir_name> <rest>
         *
         * We avoid snprintf here to keep full control over bounds and to avoid
         * compiler warnings about potential truncation.
         */
        memcpy(candidate, gvfs_base, gb);
        candidate[gb] = '/';
        memcpy(candidate + gb + 1, de->d_name, dn);
        memcpy(candidate + gb + 1 + dn, rest_s, rn);
        candidate[gb + 1 + dn + rn] = 0;

        if (path_exists(candidate)) { closedir(d); return strdup(candidate); }
}

    closedir(d);
    return NULL;
}
