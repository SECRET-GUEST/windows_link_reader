#include "open_lnk/cache_links.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    if (home && *home) return home;

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir && *pw->pw_dir) return pw->pw_dir;
    return NULL;
}

static char* xdg_cache_links_path(void) {
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = get_home_dir();
    if ((!xdg || !*xdg) && (!home || !*home)) return NULL;

    char buf[PATH_MAX];
    if (xdg && *xdg) {
        snprintf(buf, sizeof(buf), "%s/windows-link-reader/links.conf", xdg);
    } else {
        snprintf(buf, sizeof(buf), "%s/.cache/windows-link-reader/links.conf", home);
    }
    return strdup(buf);
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

static void rstrip_newline(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = 0;
}

static int rewrite_cache_file(const char *cachePath, const char *lnk_abs_path, const char *prefix, int keep_entry) {
    char tmpPath[PATH_MAX];
    FILE *in = NULL;
    FILE *out = NULL;
    int found = 0;

    if (!cachePath || !*cachePath || !lnk_abs_path || !*lnk_abs_path) return 0;
    if (keep_entry && (!prefix || !*prefix)) return 0;

    if (!keep_entry) {
        in = fopen(cachePath, "r");
        if (!in) return 1;
    } else {
        ensure_parent_dir(cachePath);
        in = fopen(cachePath, "r");
    }

    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", cachePath);
    out = fopen(tmpPath, "w");
    if (!out) {
        if (in) fclose(in);
        return 0;
    }

    if (in) {
        char line[8192];

        while (fgets(line, sizeof(line), in)) {
            char orig[8192];
            size_t orig_len = strlen(line);

            memcpy(orig, line, orig_len + 1);
            rstrip_newline(line);
            if (line[0] == '#' || line[0] == 0) {
                fputs(orig, out);
                continue;
            }

            char *eq = strchr(line, '=');
            if (!eq) {
                fputs(orig, out);
                if (orig_len == 0 || orig[orig_len - 1] != '\n') fputc('\n', out);
                continue;
            }

            *eq = 0;
            if (strcmp(line, lnk_abs_path) == 0) {
                found = 1;
                if (keep_entry) fprintf(out, "%s=%s\n", lnk_abs_path, prefix);
                continue;
            }

            fputs(orig, out);
            if (orig_len == 0 || orig[orig_len - 1] != '\n') fputc('\n', out);
        }
        fclose(in);
    }

    if (keep_entry && !found) fprintf(out, "%s=%s\n", lnk_abs_path, prefix);

    if (fclose(out) != 0) {
        (void)unlink(tmpPath);
        return 0;
    }
    if (rename(tmpPath, cachePath) != 0) {
        (void)unlink(tmpPath);
        return 0;
    }
    return 1;
}

/* latest-wins */
char* cache_get_prefix_for_lnk(const char *lnk_abs_path) {
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
            found = strdup(v);
        }
    }

    fclose(f);
    return found;
}

/* rewrite (no duplicates) via tmp + rename */
void cache_set_prefix_for_lnk(const char *lnk_abs_path, const char *prefix) {
    if (!lnk_abs_path || !*lnk_abs_path || !prefix || !*prefix) return;

    char *cachePath = xdg_cache_links_path();
    if (!cachePath) return;
    (void)rewrite_cache_file(cachePath, lnk_abs_path, prefix, 1);
    free(cachePath);
}

void cache_delete_prefix_for_lnk(const char *lnk_abs_path) {
    if (!lnk_abs_path || !*lnk_abs_path) return;

    char *cachePath = xdg_cache_links_path();
    if (!cachePath) return;
    (void)rewrite_cache_file(cachePath, lnk_abs_path, NULL, 0);
    free(cachePath);
}

int cache_clear_all(void) {
    char *cachePath = xdg_cache_links_path();
    if (!cachePath) return 0;

    if (unlink(cachePath) != 0 && errno != ENOENT) {
        free(cachePath);
        return 0;
    }

    free(cachePath);
    return 1;
}
