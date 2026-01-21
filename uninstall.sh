#!/usr/bin/env bash
set -euo pipefail

say()  { printf '%s\n' "$*"; }
ok()   { printf '[OK] %s\n' "$*"; }
warn() { printf '[WARN] %s\n' "$*" >&2; }

need_cmd() { command -v "$1" >/dev/null 2>&1; }

run_sudo() {
  if need_cmd sudo; then
    sudo "$@"
  else
    "$@"
  fi
}

OS="$(uname -s || echo Unknown)"

BIN_SYS="/usr/local/bin/open_lnk"
BIN_USER="$HOME/.local/bin/open_lnk"
BIN_SYS_ALT="/usr/bin/open_lnk"

DESKTOP_NAME="open_lnk.desktop"
DESK_SYS="/usr/share/applications/$DESKTOP_NAME"
DESK_USER="$HOME/.local/share/applications/$DESKTOP_NAME"

ICON_SYS="/usr/share/icons/hicolor/scalable/apps/open-lnk.svg"
ICON_USER="$HOME/.local/share/icons/hicolor/scalable/apps/open-lnk.svg"

say "[*] Removing binaries..."
if need_cmd sudo; then
  run_sudo rm -f "$BIN_SYS" "$BIN_SYS_ALT" 2>/dev/null || true
else
  warn "sudo not available; skipping system binary removal ($BIN_SYS, $BIN_SYS_ALT)"
fi
rm -f "$BIN_USER" 2>/dev/null || true

if [ "$OS" = "Linux" ]; then
  say "[*] Removing desktop entries + icons..."
  if need_cmd sudo; then
    run_sudo rm -f "$DESK_SYS" 2>/dev/null || true
    run_sudo rm -f "$ICON_SYS" 2>/dev/null || true
  else
    warn "sudo not available; skipping system desktop/icon removal"
  fi
  rm -f "$DESK_USER" 2>/dev/null || true
  rm -f "$ICON_USER" 2>/dev/null || true

  say "[*] Updating desktop databases (best-effort)..."
  if need_cmd update-desktop-database; then
    run_sudo update-desktop-database /usr/share/applications 2>/dev/null || true
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
  fi

  if need_cmd gtk-update-icon-cache; then
    run_sudo gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    gtk-update-icon-cache -f "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
  fi

  if need_cmd update-mime-database; then
    run_sudo update-mime-database /usr/share/mime 2>/dev/null || true
    mkdir -p "$HOME/.local/share/mime" 2>/dev/null || true
    update-mime-database "$HOME/.local/share/mime" 2>/dev/null || true
  fi

  if need_cmd xdg-mime; then
    say "[*] Clearing default handler (best-effort)..."
    # There's no universal "unset" in xdg-mime; we just report current handler.
    xdg-mime query default application/x-ms-shortcut 2>/dev/null || true
  fi
elif [ "$OS" = "Darwin" ]; then
  APP_SYS="/Applications/Open LNK.app"
  APP_USER="$HOME/Applications/Open LNK.app"
  say "[*] Removing macOS wrapper app (if present)..."
  run_sudo rm -rf "$APP_SYS" 2>/dev/null || true
  rm -rf "$APP_USER" 2>/dev/null || true
  ok "Removed: $APP_SYS / $APP_USER (if they existed)"

  say "[*] Note: Finder may keep old 'Open With' entries cached."
  say "    If needed: log out/in, or run: killall Finder"
else
  say "[*] Non-Linux OS detected ($OS). Removed binaries (where applicable)."
fi

ok "Uninstall complete."
