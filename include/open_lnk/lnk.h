#ifndef OPEN_LNK_LNK_H
#define OPEN_LNK_LNK_H

#include <stdio.h>

/*
 * "Useful" fields extracted from a .lnk file.
 *
 * This is not a full representation of the Shell Link format; it's the subset
 * we need to build a good target path to open.
 *
 * Ownership:
 *   - Every pointer may be NULL.
 *   - Every non-NULL pointer is heap-allocated and must be freed via freeLnkInfo().
 */
typedef struct {
    char *localBasePath;
    char *localBasePathU;
    char *commonPathSuffix;
    char *commonPathSuffixU;
    char *nameString;
    char *relativePath;
    char *workingDir;
    char *arguments;
    char *iconLocation;
} LnkInfo;

/*
 * Free every heap-allocated field inside a LnkInfo.
 * (It does not free the LnkInfo pointer itself.)
 */
void freeLnkInfo(LnkInfo *li);

/*
 * Parse a .lnk stream (FILE* already opened in "rb" mode) and fill LnkInfo.
 *
 * Return:
 *   1 -> success
 *   0 -> error (showError() is called with a human-friendly message)
 */
int parse_lnk(FILE *f, LnkInfo *out);

/*
 * Build the "best" Windows target path from the parsed fields.
 *
 * The .lnk format can store a base path + a suffix, or only a relative path, etc.
 * This function picks the most reliable option and returns a single path string.
 *
 * Return:
 *   - newly allocated char* (caller must free()) if something usable was found
 *   - NULL otherwise
 */
char *build_best_target(LnkInfo *li);

#endif /* OPEN_LNK_LNK_H */
