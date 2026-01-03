#ifndef OPEN_LNK_MOUNTS_H
#define OPEN_LNK_MOUNTS_H

/*
 * Automatic resolution through /proc/mounts (Linux).
 *
 * These functions try to guess where a Windows path is mounted on the local
 * machine (ex: external drive, CIFS mount, GVFS mount).
 *
 * Return:
 *   - newly allocated Linux path (caller must free()) if it exists
 *   - NULL if no suitable mount could be found
 */
char *try_map_drive_to_mounts_scored(const char *winPath);
char *try_map_unc_to_cifs_mounts(const char *uncPath);

#endif /* OPEN_LNK_MOUNTS_H */
