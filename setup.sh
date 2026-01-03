#!/usr/bin/env bash
set -euo pipefail

say()  { printf '%s\n' "$*"; }
ok()   { printf '[OK] %s\n' "$*"; }
warn() { printf '[WARN] %s\n' "$*" >&2; }
err()  { printf '[ERR] %s\n' "$*" >&2; }

pause_end() {
  if [ -t 0 ]; then
    printf '\nPress Enter to finish...'
    # shellcheck disable=SC2162
    read _
  fi
}

cd "$(dirname "$(realpath "$0")")"

OS="$(uname -s 2>/dev/null || echo Unknown)"

# Compiler
if command -v gcc >/dev/null 2>&1; then
  CC=gcc
elif command -v clang >/dev/null 2>&1; then
  CC=clang
else
  err "No C compiler found (install gcc or clang)"
  pause_end
  exit 1
fi

# Build
say "[*] Building open_lnk with $CC..."
SRC_FILES=(src/main.c src/lnk/*.c src/platform/*.c src/resolve/*.c src/util/*.c)
for f in "${SRC_FILES[@]}"; do
  if [ ! -f "$f" ]; then
    err "Missing source file: $f"
    pause_end
    exit 1
  fi
done

"$CC" "${SRC_FILES[@]}" -Iinclude -D_XOPEN_SOURCE=700 -o open_lnk -Wall -Wextra -O2
ok "Build OK"

# Install binary (system then user fallback)
BIN_SYS="/usr/local/bin/open_lnk"
BIN_USER="$HOME/.local/bin/open_lnk"

say "[*] Installing binary..."
INSTALLED_BIN=""

if command -v sudo >/dev/null 2>&1; then
  if sudo install -m0755 open_lnk "$BIN_SYS" 2>/dev/null; then
    INSTALLED_BIN="$BIN_SYS"
    ok "Installed to $BIN_SYS"
  else
    warn "System install failed; trying user install"
  fi
fi

if [ -z "$INSTALLED_BIN" ]; then
  mkdir -p "$(dirname "$BIN_USER")"
  install -m0755 open_lnk "$BIN_USER"
  INSTALLED_BIN="$BIN_USER"
  ok "Installed to $BIN_USER"
fi

if ! command -v open_lnk >/dev/null 2>&1; then
  warn "open_lnk not found in PATH. You may need:"
  warn "  export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

# Optional deps (Debian/Ubuntu)
if [ "$OS" = "Linux" ] && command -v apt-get >/dev/null 2>&1; then
  if command -v sudo >/dev/null 2>&1; then
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

    # Zenity is optional but recommended for GUI assist
    if ! command -v zenity >/dev/null 2>&1; then
      say "[*] Installing zenity (optional)..."
      sudo apt-get update -y && sudo apt-get install -y zenity || warn "zenity not installed"
    else
      ok "zenity available"
    fi
  fi
fi

# Desktop entry + icon (Linux only)
if [ "$OS" = "Linux" ]; then
  DESKTOP_NAME="open-lnk.desktop"

  # NOTE: use %f (single file). %U/%u is for URLs/multiple; for .lnk file managers typically pass a file.
  DESKTOP_CONTENT="[Desktop Entry]
Type=Application
Name=LNK Reader
Comment=Open Windows .lnk shortcuts
Exec=open_lnk %f
TryExec=open_lnk
Icon=open-lnk
Terminal=false
MimeType=application/x-ms-shortcut;
Categories=Utility;FileTools;
"

  ICON_SRC="assets/icons/open-lnk.svg"

  DESK_SYS="/usr/share/applications/$DESKTOP_NAME"
  ICON_SYS="/usr/share/icons/hicolor/scalable/apps/open-lnk.svg"

  DESK_USER="$HOME/.local/share/applications/$DESKTOP_NAME"
  ICON_USER="$HOME/.local/share/icons/hicolor/scalable/apps/open-lnk.svg"

  say "[*] Installing desktop entry + icon..."

  INSTALLED_DESKTOP=""
  INSTALLED_ICON=""

  # System install if possible
  if command -v sudo >/dev/null 2>&1; then
    if [ -f "$ICON_SRC" ]; then
      if sudo install -Dm0644 "$ICON_SRC" "$ICON_SYS" 2>/dev/null; then
        INSTALLED_ICON="$ICON_SYS"
      fi
    else
      warn "Icon not found: $ICON_SRC (skipping icon install)"
    fi

    if printf '%s' "$DESKTOP_CONTENT" | sudo tee "$DESK_SYS" >/dev/null 2>&1; then
      INSTALLED_DESKTOP="$DESK_SYS"
    fi
  fi

  # User fallback
  if [ -z "$INSTALLED_DESKTOP" ]; then
    mkdir -p "$(dirname "$DESK_USER")"
    printf '%s' "$DESKTOP_CONTENT" > "$DESK_USER"
    INSTALLED_DESKTOP="$DESK_USER"
  fi

  if [ -z "$INSTALLED_ICON" ] && [ -f "$ICON_SRC" ]; then
    mkdir -p "$(dirname "$ICON_USER")"
    install -m0644 "$ICON_SRC" "$ICON_USER"
    INSTALLED_ICON="$ICON_USER"
  fi

  ok "Desktop entry: $INSTALLED_DESKTOP"
  if [ -n "$INSTALLED_ICON" ]; then
    ok "Icon: $INSTALLED_ICON"
  fi

  # Refresh caches (best effort)
  if command -v update-desktop-database >/dev/null 2>&1; then
    if [ -w /usr/share/applications ] && command -v sudo >/dev/null 2>&1; then
      sudo update-desktop-database /usr/share/applications || true
    else
      update-desktop-database "$HOME/.local/share/applications" || true
    fi
  fi

  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    if [ -w /usr/share/icons/hicolor ] && command -v sudo >/dev/null 2>&1; then
      sudo gtk-update-icon-cache /usr/share/icons/hicolor || true
    else
      gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" || true
    fi
  fi

  # MIME default association (best effort)
  if command -v xdg-mime >/dev/null 2>&1; then
    say "[*] Setting default handler for .lnk (best effort)..."
    xdg-mime default "$DESKTOP_NAME" application/x-ms-shortcut || true
  else
    warn "xdg-mime not available; set default app in your file manager if needed"
  fi
fi

ok "Done."
say "Test: open_lnk /path/to/file.lnk"
if [ "$OS" = "Linux" ]; then
  say "If double-click doesn't work yet: log out/in, or run update-desktop-database."
fi
