#ifndef OPEN_LNK_GVFS_H
#define OPEN_LNK_GVFS_H

/*
 * Try to resolve a UNC path ("//server/share/...") through GVFS (GNOME).
 *
 * When GNOME mounts an SMB share, it can appear under:
 *   /run/user/<uid>/gvfs/smb-share:server=SERVER,share=SHARE[,...]
 *
 * Return:
 *   - newly allocated char* (caller must free()) if a match is found
 *   - NULL otherwise
 */
char *try_map_unc_via_gvfs(const char *uncPath);

#endif /* OPEN_LNK_GVFS_H */
