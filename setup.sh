#!/usr/bin/env bash
set -euo pipefail

say()  { printf '%s\n' "$*"; }
ok()   { printf '[✓] %s\n' "$*"; }
warn() { printf '[!] %s\n' "$*" >&2; }
err()  { printf '[✗] %s\n' "$*" >&2; }

pause_end() {
  if [ -t 0 ]; then
    printf '\nPress Enter to finish...'
    read -r _
  fi
}

# Reliable script directory (works even if launched via symlink/other cwd)
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

OS="$(uname -s || echo Unknown)"

if command -v gcc >/dev/null 2>&1; then
  CC=gcc
elif command -v clang >/dev/null 2>&1; then
  CC=clang
else
  err "No C compiler found (install gcc or clang)"
  pause_end; exit 1
fi

say "[*] Building open_lnk from ./src with $CC..."
SRC_FILES=(src/main.c src/lnk/*.c src/platform/*.c src/resolve/*.c src/util/*.c)
for f in "${SRC_FILES[@]}"; do
  if [ ! -f "$f" ]; then
    err "Missing source file: $f"
    pause_end; exit 1
  fi
done

"$CC" "${SRC_FILES[@]}" -Iinclude -D_XOPEN_SOURCE=700 -o open_lnk -Wall -Wextra -O2

BIN_SYS="/usr/local/bin/open_lnk"
BIN_USER="$HOME/.local/bin/open_lnk"

say "[*] Installing binary..."
if command -v sudo >/dev/null 2>&1; then
  if sudo install -m0755 open_lnk "$BIN_SYS"; then
    ok "Installed to $BIN_SYS"
  else
    warn "System install failed; trying user install"
    mkdir -p "$(dirname "$BIN_USER")"
    install -m0755 open_lnk "$BIN_USER"
    ok "Installed to $BIN_USER"
  fi
else
  mkdir -p "$(dirname "$BIN_USER")"
  install -m0755 open_lnk "$BIN_USER"
  ok "Installed to $BIN_USER"
fi

if ! command -v open_lnk >/dev/null 2>&1; then
  warn "open_lnk is not on PATH. Consider: export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

if [ "$OS" = "Linux" ] && command -v apt-get >/dev/null 2>&1; then
  if ! command -v xdg-open >/dev/null 2>&1; then
    say "[*] Installing xdg-utils..."
    sudo apt-get update -y && sudo apt-get install -y xdg-utils || warn "xdg-utils not installed"
  else
    ok "xdg-open available"
  fi

  if ! command -v notify-send >/dev/null 2>&1; then
    say "[*] Installing libnotify-bin..."
    sudo apt-get update -y && sudo apt-get install -y libnotify-bin || warn "libnotify-bin not installed"
  else
    ok "notify-send available"
  fi

  if ! command -v zenity >/dev/null 2>&1; then
    warn "zenity not found (assistant UI will not be available). On Ubuntu: sudo apt-get install zenity"
  else
    ok "zenity available"
  fi
fi

if [ "$OS" = "Linux" ]; then
  ICON_SRC="$SCRIPT_DIR/assets/icons/open-lnk.svg"
  DESKTOP_NAME="open_lnk.desktop"

  DESK_SYS="/usr/share/applications/$DESKTOP_NAME"
  DESK_USER="$HOME/.local/share/applications/$DESKTOP_NAME"

  ICON_SYS="/usr/share/icons/hicolor/scalable/apps/open-lnk.svg"
  ICON_USER="$HOME/.local/share/icons/hicolor/scalable/apps/open-lnk.svg"

  say "[*] Installing desktop entry + icon..."

  # We will install either system-wide (if sudo non-interactive works)
  # or per-user. To make icon resolution 100% reliable, the .desktop uses
  # an absolute path to the installed SVG (Icon=/path/to/open-lnk.svg).
  if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
    # SYSTEM
    if [ -f "$ICON_SRC" ]; then
      sudo install -D -m0644 "$ICON_SRC" "$ICON_SYS"
      ok "Icon at $ICON_SYS"
    else
      warn "Icon missing: $ICON_SRC"
    fi

    sudo install -D -m0644 /dev/stdin "$DESK_SYS" <<EOF
[Desktop Entry]
Name=Open LNK
Comment=Open Windows .lnk shortcuts
Exec=open_lnk %U
TryExec=open_lnk
Type=Application
MimeType=application/x-ms-shortcut;
Icon=$ICON_SYS
Terminal=false
Categories=Utility;FileTools;
EOF
    ok "Desktop entry at $DESK_SYS"
  else
    # USER
    if [ -f "$ICON_SRC" ]; then
      install -D -m0644 "$ICON_SRC" "$ICON_USER"
      ok "Icon at $ICON_USER"
    else
      warn "Icon missing: $ICON_SRC"
    fi

    install -D -m0644 /dev/stdin "$DESK_USER" <<EOF
[Desktop Entry]
Name=Open LNK
Comment=Open Windows .lnk shortcuts
Exec=open_lnk %U
TryExec=open_lnk
Type=Application
MimeType=application/x-ms-shortcut;
Icon=$ICON_USER
Terminal=false
Categories=Utility;FileTools;
EOF
    ok "Desktop entry at $DESK_USER"
  fi

  # Best-effort refresh (not strictly required when Icon is absolute path,
  # but helps MIME handlers & menus update).
  if command -v update-desktop-database >/dev/null 2>&1; then
    sudo update-desktop-database /usr/share/applications 2>/dev/null || true
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
  fi

  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    sudo gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    gtk-update-icon-cache -f "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
  fi

  if command -v xdg-mime >/dev/null 2>&1; then
    say "[*] Setting default handler for .lnk..."
    xdg-mime default "$DESKTOP_NAME" application/x-ms-shortcut || true
  fi
fi

ok "Done."
[ "$OS" = "Linux" ] && say "If double-click doesn't work yet, log out/in or run: update-desktop-database"
