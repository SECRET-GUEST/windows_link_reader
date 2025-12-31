#!/usr/bin/env bash
set -euo pipefail

#  helpers 
say()  { printf '%s\n' "$*"; }
ok()   { printf '[✓] %s\n' "$*"; }
warn() { printf '[!] %s\n' "$*" >&2; }
err()  { printf '[✗] %s\n' "$*" >&2; }

# Pause at end only if running in a terminal
pause_end() {
  if [ -t 0 ]; then
    printf '\nPress Enter to finish...'
    # shellcheck disable=SC2162
    read _
  fi
}

#  enter script dir 
cd "$(dirname "$(realpath "$0")")"

OS="$(uname -s || echo Unknown)"

#  compiler 
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

#  install binary (system then user fallback) 
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

# Ensure open_lnk resolves
if ! command -v open_lnk >/dev/null 2>&1; then
  warn "open_lnk is not on PATH. Consider: export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

#  optional deps (Debian/Ubuntu) 
if [ "$OS" = "Linux" ] && command -v apt-get >/dev/null 2>&1; then
  if ! command -v notify-send >/dev/null 2>&1; then
    say "[*] Installing libnotify-bin..."
    sudo apt-get update -y && sudo apt-get install -y libnotify-bin || warn "libnotify-bin not installed"
  else ok "notify-send available"; fi
  if ! command -v xdg-open >/dev/null 2>&1; then
    say "[*] Installing xdg-utils..."
    sudo apt-get update -y && sudo apt-get install -y xdg-utils || warn "xdg-utils not installed"
  else ok "xdg-open available"; fi
fi

#  desktop entry (Linux) 
if [ "$OS" = "Linux" ]; then
  say "[*] Creating desktop entry..."
  DESKTOP_NAME="open_lnk.desktop"
  DESKTOP_CONTENT="[Desktop Entry]
Name=Open LNK
Comment=Open Windows .lnk shortcuts
Exec=open_lnk %U
TryExec=open_lnk
Type=Application
MimeType=application/x-ms-shortcut
NoDisplay=false
Terminal=false
Categories=Utility;FileTools;"

  DESK_SYS="/usr/share/applications/$DESKTOP_NAME"
  DESK_USER="$HOME/.local/share/applications/$DESKTOP_NAME"

  DESK_INSTALLED=""
  # Try system location first
  if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
    if printf '%s' "$DESKTOP_CONTENT" | sudo tee "$DESK_SYS" >/dev/null; then
      DESK_INSTALLED="$DESK_SYS"
      ok "Desktop entry at $DESK_INSTALLED"
    else
      warn "System desktop write failed; falling back to user desktop"
    fi
  fi
  # User fallback (always ensures creation)
  if [ -z "$DESK_INSTALLED" ]; then
    mkdir -p "$(dirname "$DESK_USER")"
    printf '%s' "$DESKTOP_CONTENT" > "$DESK_USER"
    DESK_INSTALLED="$DESK_USER"
    ok "Desktop entry at $DESK_INSTALLED"
  fi

  # Update caches if tools exist
  if command -v update-mime-database >/dev/null 2>&1; then
    if [ -w /usr/share/mime ] && command -v sudo >/dev/null 2>&1; then
      sudo update-mime-database /usr/share/mime || true
    else
      mkdir -p "$HOME/.local/share/mime" || true
      update-mime-database "$HOME/.local/share/mime" || true
    fi
  fi
  if command -v update-desktop-database >/dev/null 2>&1; then
    if [ -w /usr/share/applications ] && command -v sudo >/dev/null 2>&1; then
      sudo update-desktop-database /usr/share/applications || true
    else
      update-desktop-database "$HOME/.local/share/applications" || true
    fi
  fi

  # Set default handler for .lnk if xdg-mime exists
  if command -v xdg-mime >/dev/null 2>&1; then
    say "[*] Setting default handler for .lnk..."
    xdg-mime default "$DESKTOP_NAME" application/x-ms-shortcut || true
  else
    warn "xdg-mime not available; set default app via your file manager if needed"
  fi
fi

ok "Done. You can run: open_lnk file.lnk"
[ "$OS" = "Linux" ] && say "If double-click doesn't work yet, log out/in or run: update-desktop-database"
