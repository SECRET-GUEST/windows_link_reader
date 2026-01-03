#ifndef OPEN_LNK_UNC_H
#define OPEN_LNK_UNC_H

#include <stddef.h>

/*
 * Normalize a UNC string:
 *   - accepts "\\\\server\\\\share" or "//server/share"
 *   - always returns a canonical "//server/share" form without a trailing slash
 *
 * Return:
 *   - newly allocated string (caller must free())
 *   - NULL on allocation failure
 */
char *normalize_unc(const char *s);

/*
 * Parse a canonical UNC string:
 *   "//server/share[/rest/of/path]"
 *
 * Output:
 *   - `server` receives "server"
 *   - `share`  receives "share"
 *   - `rest_out` (optional) receives a pointer into the input string:
 *       - "/rest/of/path" (including the leading slash), or
 *       - "" if there is no extra path after the share name
 */
int parse_unc_share(const char *unc,
                    char *server, size_t server_sz,
                    char *share, size_t share_sz,
                    const char **rest_out);

/*
 * Fallback: build an encoded "smb://" URI suitable for xdg-open/open.
 *
 * Example:
 *   "//srv/share/My Folder/a.txt" -> "smb://srv/share/My%20Folder/a.txt"
 *
 * Return:
 *   - newly allocated URI (caller must free())
 *   - NULL if the input cannot be parsed as UNC
 */
char *unc_to_smb_uri_encoded(const char *unc);

#endif /* OPEN_LNK_UNC_H */
