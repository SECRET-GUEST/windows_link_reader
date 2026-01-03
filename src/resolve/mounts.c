#include "open_lnk/mounts.h"
#include "open_lnk/fs.h"
#include "open_lnk/unc.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


#ifdef __linux__
static int score_mountpoint_prefix(const char *mnt) {
    if (!mnt || !*mnt) return 0;

    int s = 0;

    if (strncmp(mnt, "/mnt/", 5) == 0) s += 25;
    else if (strncmp(mnt, "/media/", 7) == 0) s += 22;
    else if (strncmp(mnt, "/run/media/", 11) == 0) s += 18;
    else if (strncmp(mnt, "/run/user/", 10) == 0) s += 12;

    size_t len = strlen(mnt);
    if (len > 0) s += (int)(len / 8);
    if (s < 0) s = 0;
    return s;
}

static int is_probably_system_mount(const char *mnt) {
    if (!mnt || !*mnt) return 1;
    if (strcmp(mnt, "/") == 0) return 1;
    if (strncmp(mnt, "/proc", 5) == 0) return 1;
    if (strncmp(mnt, "/sys", 4) == 0) return 1;
    if (strncmp(mnt, "/dev", 4) == 0) return 1;
    if (strncmp(mnt, "/run", 4) == 0) return 0; /* /run/user can be legit */
    return 0;
}

char* try_map_drive_to_mounts_scored(const char *drive_path) {
    if (!drive_path || strlen(drive_path) < 3) return NULL;

    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return NULL;

    char bestPath[PATH_MAX];
    char secondPath[PATH_MAX];
    bestPath[0] = 0;
    secondPath[0] = 0;
    int bestScore = -1;
    int secondScore = -1;

    char dev[256], mnt[PATH_MAX], fstype[64];
    while (fscanf(f, "%255s %4095s %63s %*s %*d %*d\n", dev, mnt, fstype) == 3) {
        if (is_probably_system_mount(mnt)) continue;

        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s%s", mnt, drive_path + 2);
        if (!path_exists(candidate)) continue;

        int score = score_mountpoint_prefix(mnt);

        if (score > bestScore) {
            secondScore = bestScore;
            snprintf(secondPath, sizeof(secondPath), "%s", bestPath);

            bestScore = score;
            snprintf(bestPath, sizeof(bestPath), "%s", candidate);
        } else if (score > secondScore) {
            secondScore = score;
            snprintf(secondPath, sizeof(secondPath), "%s", candidate);
        }
    }

    fclose(f);

    if (bestScore < 0 || bestPath[0] == 0) return NULL;

    /* Ambiguïté: si 2 candidats “réels” ont un score quasi identique, on évite la fausse bonne réponse */
    if (secondScore >= 0 && (bestScore - secondScore) < 2) {
        return NULL;
    }

    return strdup(bestPath);
}
#else
char* try_map_drive_to_mounts_scored(const char *drive_path) {
    (void)drive_path;
    return NULL;
}
#endif


#ifdef __linux__

#include <strings.h> /* strcasecmp */

/*
 * Try to resolve a UNC path using CIFS mounts listed in /proc/mounts.
 *
 * Input:
 *   uncPath like "//server/share/optional/rest"
 *
 * Output:
 *   Newly allocated local path if found, otherwise NULL.
 */
char *try_map_unc_to_cifs_mounts(const char *uncPath) {
    if (!uncPath || !*uncPath) return NULL;

    char *canon = normalize_unc(uncPath);
    if (!canon) return NULL;

    char server[256], share[256];
    const char *rest = NULL;

    if (!parse_unc_share(canon, server, sizeof(server), share, sizeof(share), &rest)) {
        free(canon);
        return NULL;
    }

    FILE *f = fopen("/proc/mounts", "r");
    if (!f) { free(canon); return NULL; }

    char dev[256], mnt[PATH_MAX], fstype[64];

    while (fscanf(f, "%255s %4095s %63s %*s %*d %*d\n", dev, mnt, fstype) == 3) {
        /* We only care about CIFS/SMB mounts */
        if (strcmp(fstype, "cifs") != 0 && strcmp(fstype, "smbfs") != 0)
            continue;

        /*
         * dev is often like:
         *   "//server/share"
         * Normalize it and compare server/share.
         */
        char *devcanon = normalize_unc(dev);
        if (!devcanon) continue;

        char dserv[256], dshare[256];
        const char *drest = NULL;

        int ok = parse_unc_share(devcanon, dserv, sizeof(dserv), dshare, sizeof(dshare), &drest);
        free(devcanon);

        if (!ok) continue;

        if (strcasecmp(dserv, server) != 0) continue;
        if (strcasecmp(dshare, share) != 0) continue;

        /* Build local candidate: mountpoint + rest */
        char candidate[PATH_MAX];
        if (rest && *rest) snprintf(candidate, sizeof(candidate), "%s%s", mnt, rest);
        else snprintf(candidate, sizeof(candidate), "%s", mnt);

        if (path_exists(candidate)) {
            fclose(f);
            free(canon);
            return strdup(candidate);
        }

        /*
         * Sometimes the exact file doesn't exist yet (or is a dir),
         * but the mount itself is valid.
         */
        if (path_exists(mnt)) {
            fclose(f);
            free(canon);
            return strdup(mnt);
        }
    }

    fclose(f);
    free(canon);
    return NULL;
}

#endif /* __linux__ */
