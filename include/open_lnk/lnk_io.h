#ifndef OPEN_LNK_LNK_IO_H
#define OPEN_LNK_LNK_IO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Low-level I/O helpers for reading the binary .lnk format.
 *
 * All functions that return char* allocate memory:
 *   - The caller owns the returned string and must free() it.
 */

char *lnk_utf16le_to_utf8(const uint16_t *wstr, size_t max_chars);

/*
 * Read a LNK "StringData" entry.
 *
 * Format:
 *   uint16 count
 *   then "count" characters:
 *     - UTF-16LE code units if unicode != 0
 *     - bytes if unicode == 0
 */
char *lnk_read_string_data(FILE *f, int unicode);

/*
 * Read a NUL-terminated byte string from the stream.
 *
 * The `cap` parameter is a safety limit to avoid runaway reads on corrupted data.
 */
char *lnk_read_c_string(FILE *f, size_t cap);

/*
 * Read a NUL-terminated UTF-16LE string (uint16_t code units) from the stream.
 *
 * The `max_chars` parameter is a safety limit (in UTF-16 code units) to avoid
 * runaway reads on corrupted data.
 */
char *lnk_read_w_string(FILE *f, size_t max_chars);

#endif /* OPEN_LNK_LNK_IO_H */
