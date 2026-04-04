/*
 *  Mapping file (mappings.conf)
 *
 * File format (one rule per line):
 *   F:=/media/user/F_Daten
 *   //server/share=/mnt/share
 *   \\server\\share=/mnt/share
 *
 * Rules:
 *   - Empty lines and lines starting with '#' are ignored.
 *   - "Dangerous" prefixes are ignored (/proc, /sys, /dev, ...).
 *     This is a defensive measure to avoid mapping a Windows path to a
 *     sensitive part of the system by mistake.
 */

#include "open_lnk/mapping.h"

#include "open_lnk/fs.h"
#include "open_lnk/str.h"
#include "open_lnk/unc.h"
#include "open_lnk/compat.h"

#include <ctype.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_tty_stdin(void) {
    return isatty(STDIN_FILENO);
}

static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    if (home && *home) return home;

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir && *pw->pw_dir) return pw->pw_dir;
    return NULL;
}

static const char *get_login_name(void) {
    const char *user = getenv("USER");
    if (user && *user) return user;

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name && *pw->pw_name) return pw->pw_name;
    return NULL;
}

static char *expand_mapping_home_prefix(const char *prefix) {
    const char *suffix = NULL;
    const char *home = NULL;

    if (!prefix || !*prefix) return NULL;

    if (strcmp(prefix, "~") == 0) suffix = "";
    else if (strncmp(prefix, "~/", 2) == 0) suffix = prefix + 1;
    else if (strcmp(prefix, "$HOME") == 0) suffix = "";
    else if (strncmp(prefix, "$HOME/", 6) == 0) suffix = prefix + 5;
    else if (strcmp(prefix, "${HOME}") == 0) suffix = "";
    else if (strncmp(prefix, "${HOME}/", 8) == 0) suffix = prefix + 7;
    else return strdup(prefix);

    home = get_home_dir();
    if (!home || !*home) return NULL;

    size_t home_len = strlen(home);
    size_t suffix_len = strlen(suffix);
    char *out = (char *)malloc(home_len + suffix_len + 1);
    if (!out) return NULL;

    memcpy(out, home, home_len);
    memcpy(out + home_len, suffix, suffix_len + 1);
    return out;
}

static int is_plain_user_token_at(const char *p) {
    if (!p) return 0;
    if (strncmp(p, "$USER", 5) != 0) return 0;

    unsigned char next = (unsigned char)p[5];
    if (isalnum(next) || next == '_') return 0;
    return 1;
}

static char *expand_mapping_unc_user_tokens(const char *s) {
    static const char token_braced[] = "${USER}";
    const char *user = NULL;
    const char *p = NULL;
    size_t user_len = 0;
    size_t out_len = 0;
    char *out = NULL;
    char *dst = NULL;

    if (!s) return NULL;
    if (!*s) return strdup("");

    user = get_login_name();
    if (!user || !*user) return strdup(s);
    user_len = strlen(user);

    for (p = s; *p;) {
        if (is_plain_user_token_at(p)) {
            out_len += user_len;
            p += 5;
            continue;
        }
        if (strncmp(p, token_braced, sizeof(token_braced) - 1) == 0) {
            out_len += user_len;
            p += sizeof(token_braced) - 1;
            continue;
        }
        out_len++;
        p++;
    }

    out = (char *)malloc(out_len + 1);
    if (!out) return NULL;

    for (p = s, dst = out; *p;) {
        if (is_plain_user_token_at(p)) {
            memcpy(dst, user, user_len);
            dst += user_len;
            p += 5;
            continue;
        }
        if (strncmp(p, token_braced, sizeof(token_braced) - 1) == 0) {
            memcpy(dst, user, user_len);
            dst += user_len;
            p += sizeof(token_braced) - 1;
            continue;
        }
        *dst++ = *p++;
    }

    *dst = 0;
    return out;
}

static int is_prefix_dangerous(const char *pfx) {
    /*
     * We only want mappings pointing to user-accessible mounts.
     * Mapping a drive to "/" or "/proc" would be a footgun and potentially
     * confusing/dangerous, so we block a few obvious locations.
     */
    if (!pfx || !*pfx) return 1;
    if (strcmp(pfx, "/") == 0) return 1;

    /* Allow common desktop removable mounts under /run/media/... */
    if (strncmp(pfx, "/run/media/", 11) == 0) return 0;
    /* Allow GVFS mounts (common for smb://) under /run/user/<uid>/gvfs/... */
    if (strncmp(pfx, "/run/user/", 10) == 0) {
        const char *gv = strstr(pfx, "/gvfs");
        if (gv && (gv[5] == 0 || gv[5] == '/')) return 0;
    }

    const char *bad[] = { "/proc", "/sys", "/dev", "/run", "/snap", "/var/lib/snapd", NULL };
    for (int i = 0; bad[i]; i++) {
        size_t n = strlen(bad[i]);
        if (strncmp(pfx, bad[i], n) == 0 && (pfx[n] == 0 || pfx[n] == '/')) return 1;
    }
    return 0;
}

static void ensure_parent_dir(const char *filepath) {
    /*
     * Create the parent directory path for a file (mkdir -p style).
     *
     * Example:
     *   filepath = "/home/me/.config/windows-link-reader/mappings.conf"
     * Creates:
     *   /home/me/.config/windows-link-reader
     */
    if (!filepath) return;
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", filepath);
    char *slash = strrchr(tmp, '/');
    if (!slash) return;
    *slash = 0;

    char path[PATH_MAX];
    size_t n = strlen(tmp);
    if (n == 0) return;

    /* Simplified mkdir -p: create each level when we cross a '/'. */
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        path[j++] = tmp[i];
        path[j] = 0;
        if (tmp[i] == '/' && i > 0) mkdir(path, 0755);
    }
    mkdir(path, 0755);
}

static int ml_grow(MapList *l) {
    /* Grow the dynamic array backing MapList (doubling strategy). */
    if (l->len < l->cap) return 1;
    size_t nc = l->cap ? l->cap * 2 : 8;
    MapEntry *ni = (MapEntry*)realloc(l->items, nc * sizeof(MapEntry));
    if (!ni) return 0;
    l->items = ni;
    l->cap = nc;
    return 1;
}

static int ml_push_drive(MapList *l, char drive, const char *prefix) {
    /* Append a drive-letter rule (ex: 'F' -> "/media/me/F_Daten"). */
    if (!l || !prefix) return 0;
    if (!ml_grow(l)) return 0;
    MapEntry *e = &l->items[l->len++];
    memset(e, 0, sizeof(*e));
    e->type = MAP_DRIVE;
    e->drive = (char)toupper((unsigned char)drive);
    e->prefix = strdup(prefix);
    return e->prefix != NULL;
}

static int ml_push_unc(MapList *l, const char *unc_norm, const char *prefix) {
    /* Append a UNC rule (ex: "//server/share" -> "/mnt/share"). */
    if (!l || !unc_norm || !prefix) return 0;
    if (!ml_grow(l)) return 0;
    MapEntry *e = &l->items[l->len++];
    memset(e, 0, sizeof(*e));
    e->type = MAP_UNC;
    e->unc = strdup(unc_norm);
    e->prefix = strdup(prefix);
    return (e->unc && e->prefix);
}

void ml_free(MapList *l) {
    if (!l) return;
    for (size_t i = 0; i < l->len; i++) {
        free(l->items[i].unc);
        free(l->items[i].prefix);
    }
    free(l->items);
    l->items = NULL;
    l->len = 0;
    l->cap = 0;
}

char *default_map_path(void) {
    /*
     * Default mapping location follows the XDG base directory spec.
     * - If $XDG_CONFIG_HOME is set:   $XDG_CONFIG_HOME/windows-link-reader/mappings.conf
     * - Otherwise:                   ~/.config/windows-link-reader/mappings.conf
     *
     * We also try to resolve HOME through getpwuid() as a fallback.
     */
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = get_home_dir();
    if (!home || !*home) return NULL;

    char buf[PATH_MAX];
    if (xdg && *xdg) snprintf(buf, sizeof(buf), "%s/windows-link-reader/mappings.conf", xdg);
    else snprintf(buf, sizeof(buf), "%s/.config/windows-link-reader/mappings.conf", home);

    return strdup(buf);
}

int load_map_file(const char *path, MapList *out) {
    if (!path || !out) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        /* Trim whitespace and ignore empty/comment lines. */
        char *s = str_trim(line);
        if (!*s || *s == '#') continue;

        /*
         * Drive rule:
         *   "F:=/some/prefix"
         * The ":=" marker is used to make drive rules easy to spot/parse.
         */
        if (isalpha((unsigned char)s[0]) && s[1] == ':' && s[2] == '=') {
            char drive = (char)toupper((unsigned char)s[0]);
            char *prefix = str_trim(s + 3);
            char *expanded = NULL;
            if (!*prefix) continue;

            expanded = expand_mapping_home_prefix(prefix);
            if (!expanded || !*expanded) {
                free(expanded);
                continue;
            }
            if (is_prefix_dangerous(expanded)) {
                free(expanded);
                continue;
            }
            ml_push_drive(out, drive, expanded);
            free(expanded);
            continue;
        }

        /*
         * UNC rule:
         *   "//server/share=/mnt/share"
         * We also accept backslashes ("\\\\server\\\\share") because users might
         * copy/paste directly from Windows.
         */
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;

        char *left = str_trim(s);
        char *right = str_trim(eq + 1);
        char *expanded = NULL;
        char *unc_left = NULL;
        char *unc = NULL;
        if (!*left || !*right) continue;

        expanded = expand_mapping_home_prefix(right);
        if (!expanded || !*expanded) {
            free(expanded);
            continue;
        }
        if (is_prefix_dangerous(expanded)) {
            free(expanded);
            continue;
        }

        unc_left = expand_mapping_unc_user_tokens(left);
        if (!unc_left || !*unc_left) {
            free(unc_left);
            free(expanded);
            continue;
        }

        unc = normalize_unc(unc_left);
        if (!unc) {
            free(unc_left);
            free(expanded);
            continue;
        }
        ml_push_unc(out, unc, expanded);
        free(unc_left);
        free(unc);
        free(expanded);
    }

    fclose(f);
    return 1;
}

int append_drive_map_file(const char *path, char drive, const char *prefix) {
    /* Append a new drive rule at the end of the mapping file (best-effort). */
    if (!path || !prefix) return 0;
    if (is_prefix_dangerous(prefix)) return 0;

    ensure_parent_dir(path);
    FILE *f = fopen(path, "a");
    if (!f) return 0;

    fprintf(f, "%c:=%s\n", (char)toupper((unsigned char)drive), prefix);
    fclose(f);
    return 1;
}

int append_unc_map_file(const char *path, const char *unc_root, const char *prefix) {
    /* Append a new UNC rule at the end of the mapping file (best-effort). */
    if (!path || !unc_root || !*unc_root || !prefix) return 0;
    if (is_prefix_dangerous(prefix)) return 0;

    char *canon = normalize_unc(unc_root);
    if (!canon) return 0;

    char server[256], share[256];
    const char *rest = NULL;
    if (!parse_unc_share(canon, server, sizeof(server), share, sizeof(share), &rest)) {
        free(canon);
        return 0;
    }

    char root[600];
    snprintf(root, sizeof(root), "//%s/%s", server, share);
    free(canon);

    ensure_parent_dir(path);
    FILE *f = fopen(path, "a");
    if (!f) return 0;

    fprintf(f, "%s=%s\n", root, prefix);
    fclose(f);
    return 1;
}

char *prompt_for_prefix_drive(char drive) {
    /*
     * Interactive fallback: if we cannot resolve "X:/..." automatically, we ask
     * the user to type where that drive is mounted on Linux.
     *
     * This is only used for terminal usage; GUI usage usually relies on the
     * mapping file or mounts-based heuristics.
     */
    if (!is_tty_stdin()) return NULL;

    fprintf(stderr,
            "No mapping found for %c:. Enter Linux mount prefix (example: /run/media/$USER/DRIVE) or empty to skip:\n> ",
            (char)toupper((unsigned char)drive));
    fflush(stderr);

    char buf[PATH_MAX];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;

    char *s = str_trim(buf);
    if (!*s) return NULL;
    if (s[0] != '/') return NULL;
    if (is_prefix_dangerous(s)) return NULL;

    return strdup(s);
}

/* Read a single line from a command (stdout). Returns heap string or NULL. */
static char *read_cmd_stdout_line(const char *cmd) {
    if (!cmd || !*cmd) return NULL;
    FILE *p = popen(cmd, "r");
    if (!p) return NULL;
    char buf[PATH_MAX];
    if (!fgets(buf, sizeof(buf), p)) { pclose(p); return NULL; }
    pclose(p);
    char *s = str_trim(buf);
    if (!*s) return NULL;
    return strdup(s);
}

static char *prompt_gui_zenity(char drive) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "zenity --entry --title=\"LNK Reader\" "
             "--text=\"No mapping found for %c:. Enter Linux mount prefix (example: /run/media/$USER/DRIVE)\\n(leave empty to skip)\" ",
             (char)toupper((unsigned char)drive));
    return read_cmd_stdout_line(cmd);
}

static char *prompt_gui_kdialog(char drive) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "kdialog --inputbox \"No mapping found for %c:. Enter Linux mount prefix (example: /run/media/$USER/DRIVE)\\n(leave empty to skip)\" \"\"",
             (char)toupper((unsigned char)drive));
    return read_cmd_stdout_line(cmd);
}

char *prompt_for_prefix_drive_any(char drive) {
    /* Prefer terminal prompt when available. */
    char *pfx = prompt_for_prefix_drive(drive);
    if (pfx) return pfx;

    /* No TTY: try GUI prompts (Linux only). */
    char *s = prompt_gui_zenity(drive);
    if (!s) s = prompt_gui_kdialog(drive);
    if (!s) return NULL;

    char *t = str_trim(s);
    if (!*t) { free(s); return NULL; }
    if (t[0] != '/') { free(s); return NULL; }
    if (is_prefix_dangerous(t)) { free(s); return NULL; }

    /* Keep returned string heap-allocated and trimmed. */
    char *out = strdup(t);
    free(s);
    return out;
}
