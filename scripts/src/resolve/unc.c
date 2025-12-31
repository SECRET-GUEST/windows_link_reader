/*
 *  UNC helpers + smb:// fallback
 *
 * We work with a canonical UNC form:
 *   "//server/share" (Unix slashes)
 *
 * Canonicalization makes comparisons easier across:
 *   - mappings.conf rules
 *   - /proc/mounts device fields
 *   - runtime inputs that may use backslashes
 */

#include "open_lnk/unc.h"

#include "open_lnk/fs.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *normalize_unc(const char *s) {
    if (!s) return NULL;

    /*
     * Make a writable copy, then normalize separators.
     * We accept both:
     *   "\\\\server\\\\share"
     *   "//server/share"
     */
    char *tmp = strdup(s);
    if (!tmp) return NULL;
    normalize_backslashes(tmp);

    char *p = tmp;
    if (strncmp(p, "//", 2) == 0) p += 2;

    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "//%s", p);

    /* Strip trailing slashes (but keep the leading "//" prefix). */
    size_t n = strlen(buf);
    while (n > 2 && buf[n - 1] == '/') { buf[n - 1] = 0; n--; }

    free(tmp);
    return strdup(buf);
}

int parse_unc_share(const char *unc,
                    char *server, size_t server_sz,
                    char *share, size_t share_sz,
                    const char **rest_out) {
    /*
     * Input must be in canonical form:
     *   "//server/share/optional/rest"
     *
     * We split it into:
     *   - server
     *   - share
     *   - optional rest (starting at the 3rd slash)
     */
    if (!unc || strncmp(unc, "//", 2) != 0) return 0;
    const char *p = unc + 2;

    const char *s1 = strchr(p, '/');
    if (!s1) return 0;

    size_t nserv = (size_t)(s1 - p);
    if (nserv == 0 || nserv >= server_sz) return 0;
    memcpy(server, p, nserv);
    server[nserv] = 0;

    const char *p2 = s1 + 1;
    const char *s2 = strchr(p2, '/');
    size_t nsh = s2 ? (size_t)(s2 - p2) : strlen(p2);
    if (nsh == 0 || nsh >= share_sz) return 0;
    memcpy(share, p2, nsh);
    share[nsh] = 0;

    if (rest_out) *rest_out = s2 ? s2 : "";
    return 1;
}

/*
 * Percent-encode a string for the "path" part of a URI.
 * - Encodes everything except RFC 3986 unreserved characters.
 * - Keeps '/' characters intact so paths remain readable.
 */
static int is_unreserved(unsigned char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return 1;
    if (c == '-' || c == '.' || c == '_' || c == '~') return 1;
    return 0;
}

static char *uri_encode_path(const char *s) {
    if (!s) return NULL;

    size_t inlen = strlen(s);
    size_t cap = inlen * 3 + 1;
    char *out = (char*)malloc(cap);
    if (!out) return NULL;

    /* Worst-case: every byte becomes "%XX" (3 chars). */
    size_t j = 0;
    for (size_t i = 0; i < inlen; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '/') {
            out[j++] = '/';
        } else if (is_unreserved(c)) {
            out[j++] = (char)c;
        } else {
            static const char *hex = "0123456789ABCDEF";
            out[j++] = '%';
            out[j++] = hex[(c >> 4) & 0xF];
            out[j++] = hex[c & 0xF];
        }
    }
    out[j] = 0;
    return out;
}

char *unc_to_smb_uri_encoded(const char *unc) {
    /*
     * Convert:
     *   "//server/share/rest"
     * into:
     *   "smb://server/share/rest"
     * with percent-encoding for unsafe characters (spaces, etc.).
     */
    char server[256], share[256];
    const char *rest = NULL;
    if (!parse_unc_share(unc, server, sizeof(server), share, sizeof(share), &rest)) return NULL;

    char tmp[PATH_MAX];
    if (!rest || !*rest) snprintf(tmp, sizeof(tmp), "/%s", share);
    else snprintf(tmp, sizeof(tmp), "/%s%s", share, rest);

    /* Encode "/share/rest" so it is safe as a URI path. */
    char *enc = uri_encode_path(tmp);
    if (!enc) return NULL;

    char out[PATH_MAX];
    snprintf(out, sizeof(out), "smb://%s%s", server, enc);
    free(enc);
    return strdup(out);
}
