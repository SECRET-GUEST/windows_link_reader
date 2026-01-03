#ifndef OPEN_LNK_STR_H
#define OPEN_LNK_STR_H

/*
 * In-place trim (spaces/tabs/newlines).
 *
 * Behavior:
 *   - Modifies the original buffer.
 *   - Returns a pointer to the first non-space character.
 *
 * This is handy when parsing config files with simple "key=value" lines.
 */
char *str_trim(char *s);

#endif /* OPEN_LNK_STR_H */
