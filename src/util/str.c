/*
 *  String helpers
 *
 * For now this module contains only str_trim(), used when parsing the mapping
 * configuration file.
 */

#include "open_lnk/str.h"

#include <ctype.h>
#include <string.h>

char *str_trim(char *s) {
    if (!s) return s;

    /* Move the pointer forward until the first non-space character. */
    while (*s && isspace((unsigned char)*s)) s++;

    /*
     * `end` points one past the last character in the string.
     * - `end[-1]` is the last character.
     * We move backwards while the last character is whitespace.
     */
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;

    /* NUL-terminate at the new end. */
    *end = 0;
    return s;
}
