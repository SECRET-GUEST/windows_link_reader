#!/usr/bin/env bash
set -euo pipefail

say()  { printf '%s\n' "$*"; }
ok()   { printf '[✓] %s\n' "$*"; }
warn() { printf '[!] %s\n' "$*" >&2; }

BIN_SYS="/usr/local/bin/open_lnk"
BIN_USER="$HOME/.local/bin/open_lnk"

DESKTOP_NAME="open_lnk.desktop"
DESK_SYS="/usr/share/applications/$DESKTOP_NAME"
DESK_USER="$HOME/.local/share/applications/$DESKTOP_NAME"

say "[*] Removing binaries..."
if command -v sudo >/dev/null 2>&1; then
  sudo rm -f "$BIN_SYS" 2>/dev/null || true
else
  warn "sudo not available; skipping system binary removal ($BIN_SYS)"
fi
rm -f "$BIN_USER" 2>/dev/null || true

say "[*] Removing desktop entries..."
if command -v sudo >/dev/null 2>&1; then
  sudo rm -f "$DESK_SYS" 2>/dev/null || true
else
  warn "sudo not available; skipping system desktop removal ($DESK_SYS)"
fi
rm -f "$DESK_USER" 2>/dev/null || true

say "[*] Updating desktop/mime databases (best-effort)..."
if command -v update-desktop-database >/dev/null 2>&1; then
  if [ -w /usr/share/applications ] && command -v sudo >/dev/null 2>&1; then
    sudo update-desktop-database /usr/share/applications || true
  else
    update-desktop-database "$HOME/.local/share/applications" || true
  fi
fi

if command -v update-mime-database >/dev/null 2>&1; then
  if [ -w /usr/share/mime ] && command -v sudo >/dev/null 2>&1; then
    sudo update-mime-database /usr/share/mime || true
  else
    mkdir -p "$HOME/.local/share/mime" || true
    update-mime-database "$HOME/.local/share/mime" || true
  fi
fi

ok "Uninstall complete."
