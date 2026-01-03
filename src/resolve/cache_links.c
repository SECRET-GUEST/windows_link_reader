#include "open_lnk/cache_links.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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
