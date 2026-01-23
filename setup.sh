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
BIN_INSTALLED=""

say "[*] Installing binary..."
if need_cmd sudo; then
  if sudo install -m0755 open_lnk "$BIN_SYS"; then
    ok "Installed to $BIN_SYS"
    BIN_INSTALLED="$BIN_SYS"
  else
    warn "System install failed; trying user install"
    mkdir -p "$(dirname "$BIN_USER")"
    install -m0755 open_lnk "$BIN_USER"
    ok "Installed to $BIN_USER"
    BIN_INSTALLED="$BIN_USER"
  fi
else
  mkdir -p "$(dirname "$BIN_USER")"
  install -m0755 open_lnk "$BIN_USER"
  ok "Installed to $BIN_USER"
  BIN_INSTALLED="$BIN_USER"
fi

if ! need_cmd open_lnk; then
  warn "open_lnk is not on PATH. Consider: export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

if [ -n "${BIN_INSTALLED:-}" ] && [ -x "$BIN_INSTALLED" ]; then
  say "[*] Installed version: $("$BIN_INSTALLED" --version 2>/dev/null || echo unknown)"
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

  if ! need_cmd zenity && ! need_cmd kdialog; then
    say "[*] Installing GUI dialog tool (zenity recommended)..."
    install_pkg zenity || warn "zenity not installed (GUI assistant may be unavailable)"
  else
    ok "GUI dialog tool available"
  fi
fi

if [ "$OS" = "Darwin" ]; then
  if need_cmd open; then
    ok "macOS 'open' available"
  else
    warn "macOS 'open' command not found (unexpected)."
  fi

  # macOS note:
  # Finder can only "Open with" .app bundles, not CLI binaries.
  # We optionally generate a tiny wrapper app so users can double-click .lnk files.
  if need_cmd osacompile; then
    APP_USER_DIR="$HOME/Applications"
    APP_SYS_DIR="/Applications"
    APP_NAME="Open LNK.app"

    APP_PATH_SYS="$APP_SYS_DIR/$APP_NAME"
    APP_PATH_USER="$APP_USER_DIR/$APP_NAME"

    APP_PATH=""
    if need_cmd sudo; then
      # Preferred: system-wide /Applications (visible in Finder -> Applications).
      if run_sudo mkdir -p "$APP_SYS_DIR" 2>/dev/null; then
        APP_PATH="$APP_PATH_SYS"
      fi
    fi
    if [ -z "$APP_PATH" ]; then
      mkdir -p "$APP_USER_DIR"
      APP_PATH="$APP_PATH_USER"
    fi

    say "[*] Creating Finder wrapper app: $APP_PATH"

    TMPDIR="${TMPDIR:-/tmp}"
    tmp_script="$(mktemp "$TMPDIR/open_lnk_wrapper.XXXXXX.scpt")"
    cat >"$tmp_script" <<EOF
on open theItems
  repeat with anItem in theItems
    set p to POSIX path of anItem
    do shell script (quoted form of "$BIN_INSTALLED") & " " & quoted form of p
  end repeat
end open
EOF

    if run_sudo osacompile -o "$APP_PATH" "$tmp_script" >/dev/null 2>&1; then
      ok "Wrapper app created."
      # Ensure LaunchServices can register the app as a default handler (.lnk).
      # Some macOS versions won't "stick" without a valid CFBundleIdentifier and document types.
      INFO_PLIST="$APP_PATH/Contents/Info.plist"
      PLISTBUDDY="/usr/libexec/PlistBuddy"
      BUNDLE_ID="com.secretguest.openlnk"
      APP_VER="0.0.0"
      if [ -n "${BIN_INSTALLED:-}" ] && [ -x "$BIN_INSTALLED" ]; then
        APP_VER="$("$BIN_INSTALLED" --version 2>/dev/null || echo 0.0.0)"
      fi

      if [ -f "$INFO_PLIST" ] && [ -x "$PLISTBUDDY" ]; then
        say "[*] Registering .lnk document type (Info.plist)..."

        run_sudo "$PLISTBUDDY" -c "Set :CFBundleIdentifier $BUNDLE_ID" "$INFO_PLIST" >/dev/null 2>&1 || \
          run_sudo "$PLISTBUDDY" -c "Add :CFBundleIdentifier string $BUNDLE_ID" "$INFO_PLIST" >/dev/null 2>&1 || true

        run_sudo "$PLISTBUDDY" -c "Set :CFBundleName 'Open LNK'" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Set :CFBundleDisplayName 'Open LNK'" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Set :CFBundlePackageType APPL" "$INFO_PLIST" >/dev/null 2>&1 || true

        run_sudo "$PLISTBUDDY" -c "Set :CFBundleShortVersionString $APP_VER" "$INFO_PLIST" >/dev/null 2>&1 || \
          run_sudo "$PLISTBUDDY" -c "Add :CFBundleShortVersionString string $APP_VER" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Set :CFBundleVersion $APP_VER" "$INFO_PLIST" >/dev/null 2>&1 || \
          run_sudo "$PLISTBUDDY" -c "Add :CFBundleVersion string $APP_VER" "$INFO_PLIST" >/dev/null 2>&1 || true

        run_sudo "$PLISTBUDDY" -c "Delete :CFBundleDocumentTypes" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Add :CFBundleDocumentTypes array" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Add :CFBundleDocumentTypes:0 dict" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeName string 'Windows Shortcut'" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeRole string Viewer" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions array" "$INFO_PLIST" >/dev/null 2>&1 || true
        run_sudo "$PLISTBUDDY" -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions:0 string lnk" "$INFO_PLIST" >/dev/null 2>&1 || true
      else
        warn "Could not update Info.plist (PlistBuddy missing?). Default handler may not 'stick' in Finder."
      fi

      # Best-effort: refresh LaunchServices registration.
      LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
      if [ -x "$LSREGISTER" ]; then
        run_sudo "$LSREGISTER" -f "$APP_PATH" >/dev/null 2>&1 || true
      fi

      say "Tip: Finder -> right click a .lnk -> Get Info -> Open with -> Open LNK -> Change All"
      say "If Open With doesn't refresh: run 'killall Finder' or log out/in."
    else
      warn "Failed to create wrapper app (osacompile failed)."
      say "You can still use it from Terminal: open_lnk \"/path/to/file.lnk\""
    fi
    rm -f "$tmp_script" || true
  else
    warn "osacompile not found; can't create a Finder wrapper app."
    say "You can still use it from Terminal: open_lnk \"/path/to/file.lnk\""
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

  DESK_EXEC="$BIN_INSTALLED"
  if [ -z "${DESK_EXEC:-}" ]; then
    DESK_EXEC="open_lnk"
  fi

  # If we installed to ~/.local/bin, always install desktop integration in user scope
  # to avoid a system-wide desktop entry pointing into a specific HOME.
  if [ "$DESK_EXEC" = "$BIN_USER" ]; then
    sudo_ok=0
  elif need_cmd sudo && sudo -n true >/dev/null 2>&1; then
    sudo_ok=1
  else
    sudo_ok=0
  fi

  if [ "$sudo_ok" -eq 1 ]; then
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
Exec=$DESK_EXEC %F
TryExec=$DESK_EXEC
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
Exec=$DESK_EXEC %F
TryExec=$DESK_EXEC
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
