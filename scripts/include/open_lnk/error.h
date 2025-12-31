#ifndef OPEN_LNK_ERROR_H
#define OPEN_LNK_ERROR_H

/*
 * Display an error to the user.
 *
 * We try to send a desktop notification (best-effort), but we ALWAYS print the
 * message to stderr as a reliable fallback.
 *
 * Notification strategy:
 *   - macOS: Notification Center via `osascript` (if available)
 *   - Linux: `notify-send` -> `zenity` -> `kdialog` (if available)
 *
 * Notes:
 *   - Notification tools can fail (no DBus, no DISPLAY, etc.). This must not
 *     prevent the CLI from printing a useful error.
 */
void showError(const char *message);

#endif /* OPEN_LNK_ERROR_H */
