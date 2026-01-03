#ifndef OPEN_LNK_FS_H
#define OPEN_LNK_FS_H

/*
 * Return 1 if the path exists (stat() succeeds), otherwise 0.
 *
 * This is a simple existence check:
 *   - It does not open the file.
 *   - It does not distinguish "missing" from "permission denied".
 */
int path_exists(const char *path);

/*
 * In-place conversion from Windows backslashes to Unix slashes.
 *
 * Example:
 *   "C:\\Temp\\a.txt" -> "C:/Temp/a.txt"
 *
 * We do this early so that:
 *   - Linux/macOS filesystem APIs work as expected (stat/open).
 *   - Our later heuristics can detect "X:/..." and "//server/share/..." forms.
 */
void normalize_backslashes(char *s);

#endif /* OPEN_LNK_FS_H */
