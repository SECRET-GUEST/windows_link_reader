#include "open_lnk/cache_links.h"
#include "open_lnk/compat.h"
#include "open_lnk/desktop.h"
#include "open_lnk/error.h"
#include "open_lnk/fs.h"
#include "open_lnk/gvfs.h"
#include "open_lnk/lnk.h"
#include "open_lnk/mapping.h"
#include "open_lnk/mounts.h"
#include "open_lnk/unc.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#ifndef OPEN_LNK_VERSION
#define OPEN_LNK_VERSION "0.0.12"
#endif

static int g_debug = 0;
static int g_assist = 0;

static char *join_prefix_and_rest(const char *prefix, const char *rest);

static void dbg(const char *stage, const char *win, const char *lin) {
    if (!g_debug && !g_assist) return;
    fprintf(stderr, "[%s] win='%s'\n", stage, win ? win : "(null)");
    fprintf(stderr, "[%s] lin='%s'\n", stage, lin ? lin : "(null)");
}

static int has_prog_in_path(const char *prog) {
    if (!prog || !*prog) return 0;
    const char *path = getenv("PATH");
    if (!path) return 0;

    char tmp[PATH_MAX];
    const char *p = path;

    while (*p) {
        const char *sep = strchr(p, ':');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);

        if (len > 0 && len < sizeof(tmp) - 2) {
            memcpy(tmp, p, len);
            tmp[len] = 0;

            size_t need = len + 1 + strlen(prog) + 1;
            if (need < sizeof(tmp)) {
                tmp[len] = '/';
                strcpy(tmp + len + 1, prog);
                if (access(tmp, X_OK) == 0) return 1;
            }
        }

        if (!sep) break;
        p = sep + 1;
    }

    return 0;
}

static int run_capture(char *const argv[], char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    out[0] = 0;

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        (void)dup2(pipefd[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            (void)dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    size_t used = 0;
    while (used + 1 < outsz) {
        ssize_t r = read(pipefd[0], out + used, outsz - used - 1);
        if (r <= 0) break;
        used += (size_t)r;
    }
    close(pipefd[0]);
    out[used] = 0;

    int st = 0;
    (void)waitpid(pid, &st, 0);
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
}

static void rstrip_newlines(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static int is_prefix_dangerous(const char *pfx) {
    if (!pfx || !*pfx) return 1;
    if (strcmp(pfx, "/") == 0) return 1;

    const char *bad[] = { "/proc", "/sys", "/dev", NULL };
    for (int i = 0; bad[i]; i++) {
        size_t n = strlen(bad[i]);
        if (strncmp(pfx, bad[i], n) == 0 && (pfx[n] == 0 || pfx[n] == '/')) return 1;
    }
    return 0;
}

static int score_mountpoint_prefix(const char *mnt) {
    if (!mnt || !*mnt) return 0;
    int s = 0;

    if (strncmp(mnt, "/mnt/", 5) == 0) s += 25;
    else if (strncmp(mnt, "/media/", 7) == 0) s += 22;
    else if (strncmp(mnt, "/run/media/", 11) == 0) s += 20;
    else if (strncmp(mnt, "/run/user/", 10) == 0) s += 12;

    /* Slight preference for shorter paths (usually higher-level mount roots). */
    size_t len = strlen(mnt);
    if (len > 0) s += (int)(64 / (len < 64 ? len : 64));

    return s;
}

static int cmp_mountpoints(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    int as = score_mountpoint_prefix(sa);
    int bs = score_mountpoint_prefix(sb);
    if (as != bs) return (bs - as); /* desc */
    return strcmp(sa, sb);
}

static int is_probably_system_mount(const char *mnt) {
    if (!mnt || !*mnt) return 1;
    if (strcmp(mnt, "/") == 0) return 1;
    if (strncmp(mnt, "/proc", 5) == 0) return 1;
    if (strncmp(mnt, "/sys", 4) == 0) return 1;
    if (strncmp(mnt, "/dev", 4) == 0) return 1;
    if (strncmp(mnt, "/snap", 5) == 0) return 1;
    if (strncmp(mnt, "/var/lib/snapd", 13) == 0) return 1;
    return 0;
}

static void free_str_list(char **items, int n) {
    if (!items) return;
    for (int i = 0; i < n; i++) free(items[i]);
    free(items);
}

static int push_unique(char ***items, int *len, int *cap, const char *s) {
    if (!items || !len || !cap || !s || !*s) return 0;

    for (int i = 0; i < *len; i++) {
        if (strcmp((*items)[i], s) == 0) return 1;
    }

    if (*len >= *cap) {
        int nc = (*cap == 0) ? 16 : (*cap * 2);
        char **ni = (char **)realloc(*items, (size_t)nc * sizeof(char *));
        if (!ni) return 0;
        *items = ni;
        *cap = nc;
    }

    (*items)[*len] = strdup(s);
    if (!(*items)[*len]) return 0;
    (*len)++;
    return 1;
}

static char **collect_mountpoints(int *n_out) {
    if (n_out) *n_out = 0;

    char **out = NULL;
    int len = 0;
    int cap = 0;

#ifdef __linux__
    FILE *f = fopen("/proc/mounts", "r");
    if (f) {
        char dev[256], mnt[PATH_MAX], fstype[64];
        while (fscanf(f, "%255s %4095s %63s %*s %*d %*d\n", dev, mnt, fstype) == 3) {
            if (is_probably_system_mount(mnt)) continue;
            if (mnt[0] != '/') continue;
            (void)push_unique(&out, &len, &cap, mnt);
        }
        fclose(f);
    }

    /* Add GVFS entries as mount "prefixes" (useful for smb-share:...). */
    uid_t uid = getuid();
    char gvfs_base[PATH_MAX];
    snprintf(gvfs_base, sizeof(gvfs_base), "/run/user/%u/gvfs", (unsigned)uid);
    if (access(gvfs_base, R_OK | X_OK) == 0) {
        DIR *d = opendir(gvfs_base);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char full[PATH_MAX];
                if (snprintf(full, sizeof(full), "%s/%s", gvfs_base, de->d_name) >= (int)sizeof(full)) continue;
                if (access(full, R_OK | X_OK) != 0) continue;
                (void)push_unique(&out, &len, &cap, full);
            }
            closedir(d);
        }
    }
#endif

    if (len > 1) qsort(out, (size_t)len, sizeof(char *), cmp_mountpoints);
    if (n_out) *n_out = len;
    return out;
}

static char **filter_mountpoints_for_rest(char **mnts, int n, const char *rest, int *out_n) {
    if (out_n) *out_n = 0;
    if (!mnts || n <= 0 || !rest) return NULL;

    char **out = NULL;
    int len = 0;
    int cap = 0;

    for (int i = 0; i < n; i++) {
        if (!mnts[i]) continue;
        char *cand = join_prefix_and_rest(mnts[i], rest);
        if (cand && path_exists(cand)) (void)push_unique(&out, &len, &cap, mnts[i]);
        free(cand);
    }

    if (len > 1) qsort(out, (size_t)len, sizeof(char *), cmp_mountpoints);
    if (out_n) *out_n = len;
    return out;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static char *percent_decode(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n) {
            int a = hexval(s[i + 1]);
            int b = hexval(s[i + 2]);
            if (a >= 0 && b >= 0) {
                out[j++] = (char)((a << 4) | b);
                i += 2;
                continue;
            }
        }
        out[j++] = s[i];
    }
    out[j] = 0;
    return out;
}

static char *file_uri_to_path(const char *uri) {
    if (!uri) return NULL;
    const char *p = uri;
    if (strncmp(p, "file://", 7) != 0) return strdup(uri);
    p += 7;

    if (strncmp(p, "localhost/", 10) == 0) p += 9; /* keep leading slash */
    if (p[0] != '/') return strdup(uri);

    return percent_decode(p);
}

static char *abs_path_or_dup(const char *p) {
    if (!p) return NULL;
    char tmp[PATH_MAX];
    if (realpath(p, tmp)) return strdup(tmp);
    return strdup(p);
}

static int looks_like_drive_path(const char *p) {
    if (!p || strlen(p) < 3) return 0;
    if (!isalpha((unsigned char)p[0])) return 0;
    if (p[1] != ':') return 0;
    if (p[2] != '/') return 0;
    return 1;
}

static int looks_like_unc_path(const char *p) {
    if (!p) return 0;
    return (strncmp(p, "//", 2) == 0);
}

static char *join_prefix_and_rest(const char *prefix, const char *rest) {
    if (!prefix || !*prefix) return NULL;
    if (!rest) rest = "";

    size_t pfx_len = strlen(prefix);
    size_t rest_len = strlen(rest);

    int pfx_slash = (pfx_len > 0 && prefix[pfx_len - 1] == '/');
    int rest_slash = (rest_len > 0 && rest[0] == '/');

    size_t extra = 0;
    if (rest_len == 0) extra = 0;
    else if (pfx_slash && rest_slash) extra = (size_t)-1; /* remove one */
    else if (!pfx_slash && !rest_slash) extra = 1;        /* add one */

    size_t n = pfx_len + rest_len + 1;
    if (extra == (size_t)-1) n -= 1;
    else n += extra;

    char *out = (char *)malloc(n);
    if (!out) return NULL;

    if (rest_len == 0) {
        snprintf(out, n, "%s", prefix);
        return out;
    }

    if (pfx_slash && rest_slash) snprintf(out, n, "%.*s%s", (int)(pfx_len - 1), prefix, rest);
    else if (!pfx_slash && !rest_slash) snprintf(out, n, "%s/%s", prefix, rest);
    else snprintf(out, n, "%s%s", prefix, rest);

    return out;
}

static char *prompt_manual_prefix_tty(const char *prompt) {
    if (!isatty(STDIN_FILENO)) return NULL;
    fprintf(stderr, "%s\n> ", prompt ? prompt : "Enter mount prefix:");
    fflush(stderr);

    char buf[PATH_MAX];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;
    rstrip_newlines(buf);
    if (!buf[0]) return NULL;
    if (buf[0] != '/') return NULL;
    if (is_prefix_dangerous(buf)) return NULL;
    return strdup(buf);
}

static char *prompt_manual_prefix_zenity(const char *title, const char *text) {
    if (!has_prog_in_path("zenity")) return NULL;
    char *argv[] = { "zenity", "--entry", "--title", (char *)title, "--text", (char *)text, NULL };
    char out[4096];
    int ec = run_capture(argv, out, sizeof(out));
    rstrip_newlines(out);
    if (ec != 0 || !out[0]) return NULL;
    if (out[0] != '/') return NULL;
    if (is_prefix_dangerous(out)) return NULL;
    return strdup(out);
}

static char *prompt_manual_prefix_kdialog(const char *title, const char *text) {
    if (!has_prog_in_path("kdialog")) return NULL;
    char *argv[] = { "kdialog", "--title", (char *)title, "--inputbox", (char *)text, "", NULL };
    char out[4096];
    int ec = run_capture(argv, out, sizeof(out));
    rstrip_newlines(out);
    if (ec != 0 || !out[0]) return NULL;
    if (out[0] != '/') return NULL;
    if (is_prefix_dangerous(out)) return NULL;
    return strdup(out);
}

static char *choose_mount_prefix_zenity(const char *title, const char *text, char **items, int n_items) {
    if (!has_prog_in_path("zenity")) return NULL;
    if (!items || n_items <= 0) return NULL;

    char *argv[4096];
    int k = 0;

    argv[k++] = "zenity";
    argv[k++] = "--list";
    argv[k++] = "--title";
    argv[k++] = (char *)title;
    argv[k++] = "--text";
    argv[k++] = (char *)text;
    argv[k++] = "--column=Mount";
    argv[k++] = "--hide-header";
    argv[k++] = "--height=420";
    argv[k++] = "--width=800";
    argv[k++] = "--extra-button=Manual path";

    for (int i = 0; i < n_items && k < (int)(sizeof(argv) / sizeof(argv[0])) - 2; i++) {
        argv[k++] = items[i];
    }
    argv[k] = NULL;

    char out[4096];
    int ec = run_capture(argv, out, sizeof(out));
    rstrip_newlines(out);
    if (ec != 0) return NULL;

    if (out[0] == 0 || strcmp(out, "Manual path") == 0) {
        return prompt_manual_prefix_zenity(title, "Enter mount prefix (example: /mnt/DRIVE)\n(leave empty to cancel)");
    }

    if (out[0] != '/' || is_prefix_dangerous(out)) return NULL;
    return strdup(out);
}

static char *choose_mount_prefix_kdialog(const char *title, const char *text, char **items, int n_items) {
    if (!has_prog_in_path("kdialog")) return NULL;
    if (!items || n_items <= 0) return NULL;

    char *argv[4096];
    int k = 0;

    argv[k++] = "kdialog";
    argv[k++] = "--title";
    argv[k++] = (char *)title;
    argv[k++] = "--menu";
    argv[k++] = (char *)text;

    argv[k++] = "__MANUAL__";
    argv[k++] = "Manual path";

    for (int i = 0; i < n_items && k < (int)(sizeof(argv) / sizeof(argv[0])) - 3; i++) {
        argv[k++] = items[i];
        argv[k++] = items[i];
    }
    argv[k] = NULL;

    char out[4096];
    int ec = run_capture(argv, out, sizeof(out));
    rstrip_newlines(out);
    if (ec != 0 || !out[0]) return NULL;

    if (strcmp(out, "__MANUAL__") == 0) {
        return prompt_manual_prefix_kdialog(title, "Enter mount prefix (example: /mnt/DRIVE)\n(leave empty to cancel)");
    }

    if (out[0] != '/' || is_prefix_dangerous(out)) return NULL;
    return strdup(out);
}

static char *choose_mount_prefix_tty(const char *title, const char *text, char **items, int n_items) {
    (void)title;
    if (!isatty(STDIN_FILENO)) return NULL;

    fprintf(stderr, "%s\n", text ? text : "Select a mount prefix:");
    for (int i = 0; i < n_items; i++) {
        fprintf(stderr, "  %d) %s\n", i + 1, items[i]);
    }
    fprintf(stderr, "  m) Manual path\n");
    fprintf(stderr, "  q) Cancel\n> ");
    fflush(stderr);

    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;
    rstrip_newlines(buf);
    if (!buf[0] || buf[0] == 'q' || buf[0] == 'Q') return NULL;
    if (buf[0] == 'm' || buf[0] == 'M') {
        return prompt_manual_prefix_tty("Enter mount prefix (example: /mnt/DRIVE) or empty to cancel:");
    }

    int idx = atoi(buf);
    if (idx < 1 || idx > n_items) return NULL;
    if (items[idx - 1][0] != '/' || is_prefix_dangerous(items[idx - 1])) return NULL;
    return strdup(items[idx - 1]);
}

static char *choose_mount_prefix_any(const char *title, const char *text, char **items, int n_items) {
    char *picked = choose_mount_prefix_zenity(title, text, items, n_items);
    if (!picked) picked = choose_mount_prefix_kdialog(title, text, items, n_items);
    if (!picked) picked = choose_mount_prefix_tty(title, text, items, n_items);
    return picked;
}

static int try_open_path(const char *stage, const char *win, const char *cand) {
    if (!cand || !*cand) return -1;
    dbg(stage, win, cand);
    if (!path_exists(cand)) return -1;
    return open_with_desktop(cand);
}

static int try_open_uri(const char *stage, const char *win, const char *uri) {
    if (!uri || !*uri) return -1;
    dbg(stage, win, uri);
    return open_with_desktop(uri);
}

static char *get_mapping_path(void) {
    const char *env = getenv("WINDOWS_LINK_READER_MAP");
    if (env && *env) return strdup(env);
    return default_map_path();
}

static int handle_one_lnk(const char *lnk_arg, const MapList *maps) {
    if (!lnk_arg || !*lnk_arg) return 1;

    char *lnk_path = file_uri_to_path(lnk_arg);
    if (!lnk_path) return 1;

    FILE *f = fopen(lnk_path, "rb");
    if (!f) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof(msg), "Failed to open .lnk file: %s", lnk_path);
        showError(msg);
        free(lnk_path);
        return 1;
    }

    LnkInfo info = {0};
    if (!parse_lnk(f, &info)) {
        fclose(f);
        freeLnkInfo(&info);
        free(lnk_path);
        return 1;
    }
    fclose(f);

    char *win_raw = build_best_target(&info);
    if (!win_raw || !*win_raw) {
        free(win_raw);
        freeLnkInfo(&info);
        showError("No target path found in .lnk file.");
        free(lnk_path);
        return 1;
    }

    char *target = strdup(win_raw);
    if (!target) {
        free(win_raw);
        freeLnkInfo(&info);
        free(lnk_path);
        return 1;
    }
    normalize_backslashes(target);

    char *lnk_abs = abs_path_or_dup(lnk_path);

    /* 0) Already a POSIX absolute path. */
    if (target[0] == '/') {
        if (try_open_path("raw:posix", win_raw, target) == 0) goto ok;
    }

    /* 1) Per-link cache (drive and UNC). */
    if ((looks_like_drive_path(target) || looks_like_unc_path(target)) && lnk_abs) {
        char *pfx = cache_get_prefix_for_lnk(lnk_abs);
        if (pfx && *pfx) {
            if (looks_like_drive_path(target)) {
                const char *rest = target + 2; /* "/..." */
                char *cand = join_prefix_and_rest(pfx, rest);
                if (cand) {
                    if (try_open_path("cache:drive", win_raw, cand) == 0) {
                        free(cand);
                        free(pfx);
                        goto ok;
                    }
                    free(cand);
                }
            } else if (looks_like_unc_path(target)) {
                char *canon = normalize_unc(target);
                if (canon) {
                    char server[256], share[256];
                    const char *rest = NULL;
                    if (parse_unc_share(canon, server, sizeof(server), share, sizeof(share), &rest)) {
                        char *cand = join_prefix_and_rest(pfx, rest);
                        if (cand) {
                            if (try_open_path("cache:unc", win_raw, cand) == 0) {
                                free(cand);
                                free(canon);
                                free(pfx);
                                goto ok;
                            }
                            free(cand);
                        }
                    }
                    free(canon);
                }
            }
        }
        free(pfx);
    }

    /* 2) UNC resolution. */
    if (looks_like_unc_path(target)) {
        char *canon = normalize_unc(target);
        if (canon) {
            char *mapped = try_map_unc_with_table(canon, maps);
            if (mapped) {
                int rc = try_open_path("unc:table", win_raw, mapped);
                free(mapped);
                if (rc == 0) { free(canon); goto ok; }
            }

            char *gv = try_map_unc_via_gvfs(canon);
            if (gv) {
                int rc = try_open_path("unc:gvfs", win_raw, gv);
                free(gv);
                if (rc == 0) { free(canon); goto ok; }
            }

            char *cifs = try_map_unc_to_cifs_mounts(canon);
            if (cifs) {
                int rc = try_open_path("unc:cifs", win_raw, cifs);
                free(cifs);
                if (rc == 0) { free(canon); goto ok; }
            }

            /* Optional assistant before smb:// fallback. */
            if (lnk_abs) {
                char server[256], share[256];
                const char *rest = NULL;
                if (parse_unc_share(canon, server, sizeof(server), share, sizeof(share), &rest)) {
                    char root[600];
                    snprintf(root, sizeof(root), "//%s/%s", server, share);

                    char **choices = NULL;
                    int n_choices = 0;
                    int cap_choices = 0;

                    /* Prefer known UNC-derived prefixes first (even if the full file doesn't exist). */
                    char *troot = try_map_unc_with_table(root, maps);
                    if (troot) {
                        (void)push_unique(&choices, &n_choices, &cap_choices, troot);
                        free(troot);
                    }
                    char *groot = try_map_unc_via_gvfs(root);
                    if (groot) {
                        (void)push_unique(&choices, &n_choices, &cap_choices, groot);
                        free(groot);
                    }
                    char *croot = try_map_unc_to_cifs_mounts(root);
                    if (croot) {
                        (void)push_unique(&choices, &n_choices, &cap_choices, croot);
                        free(croot);
                    }

                    int n_mnts = 0;
                    char **mnts = collect_mountpoints(&n_mnts);

                    int n_good = 0;
                    char **good = filter_mountpoints_for_rest(mnts, n_mnts, rest, &n_good);

                    char **base = (good && n_good > 0) ? good : mnts;
                    int base_n = (good && n_good > 0) ? n_good : n_mnts;
                    for (int i = 0; i < base_n; i++) {
                        (void)push_unique(&choices, &n_choices, &cap_choices, base[i]);
                    }

                    free_str_list(good, n_good);
                    free_str_list(mnts, n_mnts);

                    char title[64];
                    snprintf(title, sizeof(title), "Open LNK");
                    char text[4096];
                    snprintf(text, sizeof(text),
                             "Select the mount prefix for share %s:\n%s\n\n(Manual path is available if needed.)",
                             root, win_raw);

                    char *prefix = NULL;
                    if (choices && n_choices > 0) {
                        prefix = choose_mount_prefix_any(title, text, choices, n_choices);
                    } else {
                        prefix = prompt_manual_prefix_tty("Enter mount prefix (example: /mnt/share) or empty to cancel:");
                    }
                    free_str_list(choices, n_choices);

                    if (prefix) {
                        char *cand = join_prefix_and_rest(prefix, rest);
                        if (cand) {
                            if (try_open_path("unc:assist", win_raw, cand) == 0) {
                                cache_set_prefix_for_lnk(lnk_abs, prefix);
                                free(cand);
                                free(prefix);
                                free(canon);
                                goto ok;
                            }
                            free(cand);
                        }
                        free(prefix);
                    }
                }
            }

            char *uri = unc_to_smb_uri_encoded(canon);
            free(canon);
            if (uri) {
                int rc = try_open_uri("unc:smb", win_raw, uri);
                free(uri);
                if (rc == 0) goto ok;
            }
        }
    }

    /* 3) Drive letter resolution. */
    if (looks_like_drive_path(target)) {
        char *mapped = try_map_drive_with_table(target, maps);
        if (mapped) {
            int rc = try_open_path("drive:table", win_raw, mapped);
            free(mapped);
            if (rc == 0) goto ok;
        }

        char *guess = try_map_drive_to_mounts_scored(target);
        if (guess) {
            int rc = try_open_path("drive:mounts", win_raw, guess);
            free(guess);
            if (rc == 0) goto ok;
        }

        if (lnk_abs) {
            int n_mnts = 0;
            char **mnts = collect_mountpoints(&n_mnts);

            const char *rest = target + 2; /* "/..." */
            int n_good = 0;
            char **good = filter_mountpoints_for_rest(mnts, n_mnts, rest, &n_good);

            char title[64];
            snprintf(title, sizeof(title), "Open LNK");
            char text[4096];
            snprintf(text, sizeof(text),
                     "Select the mount prefix for drive %c:\n%s\n\n(Manual path is available if needed.)",
                     (char)toupper((unsigned char)target[0]),
                     win_raw);

            char *prefix = NULL;
            if (good && n_good > 0) prefix = choose_mount_prefix_any(title, text, good, n_good);
            else if (mnts && n_mnts > 0) prefix = choose_mount_prefix_any(title, text, mnts, n_mnts);
            else prefix = prompt_manual_prefix_tty("Enter mount prefix (example: /mnt/DRIVE) or empty to cancel:");

            free_str_list(good, n_good);
            free_str_list(mnts, n_mnts);

            if (prefix) {
                char *cand = join_prefix_and_rest(prefix, rest);
                if (cand) {
                    if (try_open_path("drive:assist", win_raw, cand) == 0) {
                        cache_set_prefix_for_lnk(lnk_abs, prefix);
                        free(cand);
                        free(prefix);
                        goto ok;
                    }
                    free(cand);
                }
                free(prefix);
            }
        }
    }

    dbg("fail", win_raw, "(no resolution)");
    {
        char msg[8192];
        snprintf(msg, sizeof(msg),
                 "Could not resolve this shortcut target.\n\nLNK file:\n%s\n\nWindows target:\n%s",
                 lnk_path, win_raw);
        showError(msg);
    }

    free(target);
    free(win_raw);
    freeLnkInfo(&info);
    free(lnk_abs);
    free(lnk_path);
    return 2;

ok:
    free(target);
    free(win_raw);
    freeLnkInfo(&info);
    free(lnk_abs);
    free(lnk_path);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *lnk_args[256];
    int lnk_n = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--debug")) g_debug = 1;
        else if (!strcmp(argv[i], "--assist")) g_assist = 1;
        else if (!strcmp(argv[i], "--version")) {
            printf("%s\n", OPEN_LNK_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Usage: open_lnk [--debug] [--assist] <file.lnk>...\n");
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1] != 0) {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else {
            if (lnk_n < (int)(sizeof(lnk_args) / sizeof(lnk_args[0]))) {
                lnk_args[lnk_n++] = argv[i];
            }
        }
    }

    if (lnk_n == 0) {
        fprintf(stderr, "No .lnk provided.\n");
        return 1;
    }

    /* Load mapping table once. */
    char *mapPath = get_mapping_path();
    MapList maps = {0};
    if (mapPath) (void)load_map_file(mapPath, &maps);

    int rc = 0;
    for (int i = 0; i < lnk_n; i++) {
        int r = handle_one_lnk(lnk_args[i], &maps);
        if (r != 0) rc = r;
    }

    ml_free(&maps);
    free(mapPath);
    return rc;
}
