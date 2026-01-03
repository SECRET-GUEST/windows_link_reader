/* (fichier complet inchangé sauf la partie try_map_drive_to_mounts_scored)
 * Je te colle le fichier entier pour être safe.
 */

#include "open_lnk/mounts.h"

#include "open_lnk/fs.h"
#include "open_lnk/unc.h"
#include "open_lnk/compat.h"
#include <ctype.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static void unescape_fstab_field(char *s) {
    if (!s) return;
    char *r = s, *w = s;
    while (*r) {
        if (*r == '\\' &&
            isdigit((unsigned char)r[1]) &&
            isdigit((unsigned char)r[2]) &&
            isdigit((unsigned char)r[3])) {
            int v = (r[1]-'0')*64 + (r[2]-'0')*8 + (r[3]-'0');
            *w++ = (char)v;
            r += 4;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

static int mounts_line_tokens(const char *line,
                              char *dev, size_t devsz,
                              char *mnt, size_t mntsz,
                              char *fst, size_t fstsz) {
    if (!line || !dev || !mnt || !fst) return 0;

    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;

    const char *t1 = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t n1 = (size_t)(p - t1);
    while (*p && isspace((unsigned char)*p)) p++;

    const char *t2 = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t n2 = (size_t)(p - t2);
    while (*p && isspace((unsigned char)*p)) p++;

    const char *t3 = p;
    while (*p && !isspace((unsigned char)*p) && *p != '\n') p++;
    size_t n3 = (size_t)(p - t3);

    if (n1 == 0 || n2 == 0 || n3 == 0) return 0;
    if (n1 >= devsz || n2 >= mntsz || n3 >= fstsz) return 0;

    memcpy(dev, t1, n1); dev[n1] = 0;
    memcpy(mnt, t2, n2); mnt[n2] = 0;
    memcpy(fst, t3, n3); fst[n3] = 0;

    unescape_fstab_field(dev);
    unescape_fstab_field(mnt);
    unescape_fstab_field(fst);
    return 1;
}

static int score_mountpoint_prefix(const char *mnt, uid_t uid) {
    if (!mnt) return 0;

    const char *home = getenv("HOME");
    if (!home || !*home) {
        struct passwd *pw = getpwuid(uid);
        if (pw && pw->pw_dir) home = pw->pw_dir;
    }

    char user[128] = {0};
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) snprintf(user, sizeof(user), "%s", pw->pw_name);

    int score = 0;

    if (strncmp(mnt, "/run/media/", 11) == 0) score += 50;
    if (strncmp(mnt, "/media/", 7) == 0) score += 40;
    if (strncmp(mnt, "/mnt/", 5) == 0) score += 25;

    if (user[0]) {
        char p1[PATH_MAX];
        snprintf(p1, sizeof(p1), "/run/media/%s/", user);
        if (strncmp(mnt, p1, strlen(p1)) == 0) score += 25;

        char p2[PATH_MAX];
        snprintf(p2, sizeof(p2), "/media/%s/", user);
        if (strncmp(mnt, p2, strlen(p2)) == 0) score += 20;
    }

    if (home && *home) {
        size_t hn = strlen(home);
        if (strncmp(mnt, home, hn) == 0 && (mnt[hn] == 0 || mnt[hn] == '/')) score += 10;
    }

    return score;
}

static int score_fstype(const char *fst) {
    if (!fst) return 0;
    if (strcmp(fst, "cifs") == 0 || strcmp(fst, "smb3") == 0) return 35;
    if (strcmp(fst, "ntfs") == 0 || strcmp(fst, "ntfs3") == 0) return 30;
    if (strcmp(fst, "exfat") == 0) return 28;
    if (strcmp(fst, "vfat") == 0 || strcmp(fst, "msdos") == 0) return 22;
    return 0;
}

char *try_map_drive_to_mounts_scored(const char *winPath) {
    if (!winPath || strlen(winPath) < 3) return NULL;
    if (winPath[1] != ':' || winPath[2] != '/') return NULL;

    FILE *m = fopen("/proc/mounts", "r");
    if (!m) return NULL;

    /* DO NOT skip /run entirely: /run/media/... is a very common desktop mountpoint. */
    const char *skip[] = { "/proc", "/sys", "/dev", "/run/user", "/snap", "/var/lib/snapd", NULL };
    const char *core = winPath + 2;

    char bestPath[PATH_MAX];
    bestPath[0] = 0;
    int bestScore = -1;
    int secondBestScore = -1;

    uid_t uid = getuid();
    char line[4096];

    while (fgets(line, sizeof(line), m)) {
        char dev[1024], mnt[1024], fst[128];
        if (!mounts_line_tokens(line, dev, sizeof(dev), mnt, sizeof(mnt), fst, sizeof(fst))) continue;

        int bad = 0;
        for (int i = 0; skip[i]; ++i) {
            size_t n = strlen(skip[i]);
            if (strncmp(mnt, skip[i], n) == 0) { bad = 1; break; }
        }
        if (bad) continue;

        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s%s", mnt, core);
        if (!path_exists(candidate)) continue;

        int score = 0;
        score += score_fstype(fst);
        score += score_mountpoint_prefix(mnt, uid);
        score += (int)(strlen(mnt) / 8);

        if (score > bestScore) {
            secondBestScore = bestScore;
            bestScore = score;
            snprintf(bestPath, sizeof(bestPath), "%s", candidate);
        } else if (score > secondBestScore) {
            secondBestScore = score;
        }
    }

    fclose(m);

    /* Confidence gate: avoid “matched by chance”. */
    if (bestScore < 30) return NULL;
    if (secondBestScore >= bestScore - 3) return NULL;

    if (bestScore >= 0 && bestPath[0]) return strdup(bestPath);
    return NULL;
}

/* UNC part unchanged */
char *try_map_unc_to_cifs_mounts(const char *uncPath) {
    if (!uncPath || strncmp(uncPath, "//", 2) != 0) return NULL;

    char server[256], share[256];
    const char *rest = NULL;
    if (!parse_unc_share(uncPath, server, sizeof(server), share, sizeof(share), &rest)) return NULL;

    FILE *m = fopen("/proc/mounts", "r");
    if (!m) return NULL;

    char want[PATH_MAX];
    snprintf(want, sizeof(want), "//%s/%s", server, share);

    char line[4096];
    while (fgets(line, sizeof(line), m)) {
        char dev[1024], mnt[1024], fst[128];
        if (!mounts_line_tokens(line, dev, sizeof(dev), mnt, sizeof(mnt), fst, sizeof(fst))) continue;

        if (strcmp(fst, "cifs") != 0 && strcmp(fst, "smb3") != 0) continue;

        char *dev_norm = normalize_unc(dev);
        if (!dev_norm) continue;

        int match = (strcmp(dev_norm, want) == 0);
        free(dev_norm);
        if (!match) continue;

        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s%s", mnt, rest ? rest : "");
        if (path_exists(candidate)) { fclose(m); return strdup(candidate); }
    }

    fclose(m);
    return NULL;
}
