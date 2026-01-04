#!/usr/bin/env bash
set -euo pipefail

say()  { printf '%s\n' "$*"; }
ok()   { printf '[OK] %s\n' "$*"; }
warn() { printf '[WARN] %s\n' "$*" >&2; }
err()  { printf '[ERR] %s\n' "$*" >&2; }

pause_end() {
  if [ -t 0 ]; then
    printf '\nPress Enter to finish...'
    read -r _
  fi
}

need_cmd() { command -v "$1" >/dev/null 2>&1; }

run_sudo() {
  if need_cmd sudo; then
    sudo "$@"
  else
    "$@"
  fi
}

# Best-effort package install across many distros (NO zenity).
# Usage: install_pkg <pkgname> [<pkgname2>...]
install_pkg() {
  local pkgs=("$@")
  if [ "${#pkgs[@]}" -eq 0 ]; then
    return 0
  fi

  if need_cmd apt-get; then
    run_sudo apt-get update -y || warn "apt-get update failed"
    run_sudo apt-get install -y "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd apt; then
    run_sudo apt update -y || warn "apt update failed"
    run_sudo apt install -y "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd dnf; then
    run_sudo dnf install -y "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd yum; then
    run_sudo yum install -y "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd pacman; then
    run_sudo pacman -Sy --noconfirm "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd zypper; then
    run_sudo zypper --non-interactive install "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd apk; then
    run_sudo apk add --no-cache "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd emerge; then
    run_sudo emerge "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd nix-env; then
    nix-env -iA nixpkgs."${pkgs[0]}" || return 1
    if [ "${#pkgs[@]}" -gt 1 ]; then
      warn "nix-env: installed only the first package (${pkgs[0]}). Install others manually if needed."
    fi
    return 0
  fi

  if need_cmd brew; then
    brew update >/dev/null 2>&1 || true
    brew install "${pkgs[@]}" || return 1
    return 0
  fi

  if need_cmd port; then
    run_sudo port selfupdate || warn "port selfupdate failed"
    run_sudo port install "${pkgs[@]}" || return 1
    return 0
  fi

  warn "No supported package manager found. Please install: ${pkgs[*]}"
  return 1
}

# Reliable script directory (works even if launched via symlink/other cwd)
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

OS="$(uname -s || echo Unknown)"

# Compiler selection
if need_cmd gcc; then
  CC=gcc
elif need_cmd clang; then
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
if need_cmd sudo; then
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

if ! need_cmd open_lnk; then
  warn "open_lnk is not on PATH. Consider: export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

# Soft dependencies (best-effort): xdg-open + notify-send
if [ "$OS" = "Linux" ]; then
  if ! need_cmd xdg-open; then
    say "[*] Installing xdg-open provider (xdg-utils)..."
    install_pkg xdg-utils || warn "xdg-utils not installed (xdg-open missing)"
  else
    ok "xdg-open available"
  fi

  if ! need_cmd notify-send; then
    say "[*] Installing notify-send provider..."
    install_pkg libnotify-bin || install_pkg libnotify || warn "notify-send not installed"
  else
    ok "notify-send available"
  fi
fi

if [ "$OS" = "Darwin" ]; then
  if need_cmd open; then
    ok "macOS 'open' available"
  else
    warn "macOS 'open' command not found (unexpected)."
  fi
fi

# Desktop integration for Linux
if [ "$OS" = "Linux" ]; then
  ICON_SRC="$SCRIPT_DIR/assets/icons/open-lnk.svg"
  DESKTOP_NAME="open_lnk.desktop"

  DESK_SYS="/usr/share/applications/$DESKTOP_NAME"
  DESK_USER="$HOME/.local/share/applications/$DESKTOP_NAME"

  ICON_SYS="/usr/share/icons/hicolor/scalable/apps/open-lnk.svg"
  ICON_USER="$HOME/.local/share/icons/hicolor/scalable/apps/open-lnk.svg"

  say "[*] Installing desktop entry + icon..."

  if need_cmd sudo && sudo -n true >/dev/null 2>&1; then
    if [ -f "$ICON_SRC" ]; then
      sudo install -D -m0644 "$ICON_SRC" "$ICON_SYS"
      ok "Icon at $ICON_SYS"
    else
      warn "Icon missing: $ICON_SRC"
    fi

    sudo install -D -m0644 /dev/stdin "$DESK_SYS" <<EOF
[Desktop Entry]
Name=Open LNK
Comment=Open WinDdos .lnk shortcuts
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
    if [ -f "$ICON_SRC" ]; then
      install -D -m0644 "$ICON_SRC" "$ICON_USER"
      ok "Icon at $ICON_USER"
    else
      warn "Icon missing: $ICON_SRC"
    fi

    install -D -m0644 /dev/stdin "$DESK_USER" <<EOF
[Desktop Entry]
Name=Open LNK
Comment=Open WinDdos .lnk shortcuts
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

  if need_cmd update-desktop-database; then
    sudo update-desktop-database /usr/share/applications 2>/dev/null || true
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
  fi

  if need_cmd gtk-update-icon-cache; then
    sudo gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    gtk-update-icon-cache -f "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
  fi

  if need_cmd xdg-mime; then
    say "[*] Setting default handler for .lnk..."
    xdg-mime default "$DESKTOP_NAME" application/x-ms-shortcut || true
  fi
fi

ok "Done."
if [ "$OS" = "Linux" ]; then
  say "If double-click doesn't work yet, log out/in or run: update-desktop-database"
fi
