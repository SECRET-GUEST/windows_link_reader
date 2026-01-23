#ifndef OPEN_LNK_MAPPING_H
#define OPEN_LNK_MAPPING_H

#include <stddef.h>

/*
 * User-provided mapping rules.
 *
 * We use these mappings to translate Windows paths into Linux paths.
 *
 * Supported concepts:
 *   - Drive letter mapping:
 *       "F:/path" -> "/media/user/F_Daten/path"
 *   - UNC mapping:
 *       "//server/share/..." -> "/mnt/share/..."
 *
 * The mapping file is parsed by src/resolve/mapping_file.c.
 */

typedef enum {
    MAP_DRIVE = 1,
    MAP_UNC   = 2
} MapType;

typedef struct {
    MapType type;
    char drive;   /* 'C'..'Z' (MAP_DRIVE) */
    char *unc;    /* canonical "//server/share" form (MAP_UNC) */
    char *prefix; /* prefix Linux (/mnt/..., /media/...) */
} MapEntry;

typedef struct {
    MapEntry *items;
    size_t len;
    size_t cap;
} MapList;

/* Free all heap allocations owned by a MapList. */
void ml_free(MapList *l);

/*
 * Default mapping file path:
 *   ~/.config/windows-link-reader/mappings.conf
 * (or $XDG_CONFIG_HOME/windows-link-reader/mappings.conf if set)
 */
char *default_map_path(void);

/*
 * Load a mapping file into `out` (append mode: existing entries stay).
 *
 * Return:
 *   1 -> file was opened and processed
 *   0 -> file could not be opened (missing, permissions, ...)
 */
int load_map_file(const char *path, MapList *out);

/*
 * Append a "X:=/prefix" drive rule to the mapping file.
 * The file is created (and its parent directory created) if needed.
 */
int append_drive_map_file(const char *path, char drive, const char *prefix);

/*
 * Append a "//server/share=/prefix" UNC rule to the mapping file.
 * The UNC root is normalized to the canonical "//server/share" form.
 */
int append_unc_map_file(const char *path, const char *unc_root, const char *prefix);

/*
 * Interactive fallback: ask the user to type a Linux mount prefix for a drive letter.
 *
 * This only works when stdin is a TTY (interactive terminal).
 *
 * Return:
 *   - newly allocated string (caller must free()) on success
 *   - NULL if the user skipped, input was invalid, or not a TTY
 */
char *prompt_for_prefix_drive(char drive);

/*
 * Like prompt_for_prefix_drive(), but also supports GUI usage:
 * - If stdin is a TTY: prompts in the terminal.
 * - Otherwise (Linux): tries zenity/kdialog input boxes.
 *
 * Return:
 *   - newly allocated string (caller must free()) on success
 *   - NULL if the user skipped/cancelled, input was invalid, or no UI was available
 */
char *prompt_for_prefix_drive_any(char drive);

/*
 * Resolve a Windows path/UNC through the mapping table.
 *
 * These functions only return a path if it exists on disk (stat() succeeds),
 * otherwise they return NULL.
 */
char *try_map_drive_with_table(const char *winPath, const MapList *maps);
char *try_map_unc_with_table(const char *uncPath, const MapList *maps);

#endif /* OPEN_LNK_MAPPING_H */
