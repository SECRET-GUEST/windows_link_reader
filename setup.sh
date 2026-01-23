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

# Non-interactive sudo helper: only runs if sudo exists AND works without password.
have_sudo_nopass() {
  need_cmd sudo && sudo -n true >/dev/null 2>&1
}

run_sudo_nopass() {
  # Only call this if have_sudo_nopass is true.
  sudo -n "$@"
}

plist_buddy() {
  # Usage: plist_buddy <use_sudo_nopass:0|1> <plist_path> <PlistBuddy command...>
  local use_sudo="$1"
  shift
  local plist_path="$1"
  shift

  local pb="/usr/libexec/PlistBuddy"
  if [ ! -x "$pb" ]; then
    return 1
  fi

  if [ "$use_sudo" -eq 1 ]; then
    run_sudo_nopass "$pb" -c "$*" "$plist_path"
  else
    "$pb" -c "$*" "$plist_path"
  fi
}

install_pkg() {
  local pkgs=("$@")
  if [ "${#pkgs[@]}" -eq 0 ]; then
    return 0
  fi

  if need_cmd apt-get; then
    if have_sudo_nopass; then
      run_sudo_nopass apt-get update -y || warn "apt-get update failed"
      run_sudo_nopass apt-get install -y "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
    return 0
  fi

  if need_cmd apt; then
    if have_sudo_nopass; then
      run_sudo_nopass apt update -y || warn "apt update failed"
      run_sudo_nopass apt install -y "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
    return 0
  fi

  if need_cmd dnf; then
    if have_sudo_nopass; then
      run_sudo_nopass dnf install -y "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
    return 0
  fi

  if need_cmd yum; then
    if have_sudo_nopass; then
      run_sudo_nopass yum install -y "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
    return 0
  fi

  if need_cmd pacman; then
    if have_sudo_nopass; then
      run_sudo_nopass pacman -Sy --noconfirm "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
    return 0
  fi

  if need_cmd zypper; then
    if have_sudo_nopass; then
      run_sudo_nopass zypper --non-interactive install "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
    return 0
  fi

  if need_cmd apk; then
    if have_sudo_nopass; then
      run_sudo_nopass apk add --no-cache "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
    return 0
  fi

  if need_cmd emerge; then
    if have_sudo_nopass; then
      run_sudo_nopass emerge "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
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
    if have_sudo_nopass; then
      run_sudo_nopass port selfupdate || warn "port selfupdate failed"
      run_sudo_nopass port install "${pkgs[@]}" || return 1
    else
      warn "sudo without password not available; please install manually: ${pkgs[*]}"
      return 1
    fi
    return 0
  fi

  warn "No supported package manager found. Please install: ${pkgs[*]}"
  return 1
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

OS="$(uname -s || echo Unknown)"

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
if have_sudo_nopass; then
  if run_sudo_nopass install -m0755 open_lnk "$BIN_SYS"; then
    ok "Installed to $BIN_SYS"
    BIN_INSTALLED="$BIN_SYS"
  else
    warn "System install failed; trying user install"
  fi
fi

if [ -z "${BIN_INSTALLED:-}" ]; then
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

  if need_cmd osacompile; then
    APP_USER_DIR="$HOME/Applications"
    APP_SYS_DIR="/Applications"
    APP_NAME="Open LNK.app"

    APP_PATH_SYS="$APP_SYS_DIR/$APP_NAME"
    APP_PATH_USER="$APP_USER_DIR/$APP_NAME"

    APP_PATH=""
    APP_USE_SUDO=0

    # Preferred: /Applications only if sudo works without password (non-interactive).
    if have_sudo_nopass; then
      if run_sudo_nopass mkdir -p "$APP_SYS_DIR" >/dev/null 2>&1; then
        APP_PATH="$APP_PATH_SYS"
        APP_USE_SUDO=1
      fi
    fi

    # Fallback: ~/Applications
    if [ -z "$APP_PATH" ]; then
      mkdir -p "$APP_USER_DIR"
      APP_PATH="$APP_PATH_USER"
      APP_USE_SUDO=0
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

    if [ "$APP_USE_SUDO" -eq 1 ]; then
      run_sudo_nopass osacompile -o "$APP_PATH" "$tmp_script" >/dev/null 2>&1 || {
        warn "Failed to create wrapper app in /Applications (sudo -n). Falling back to ~/Applications."
        mkdir -p "$APP_USER_DIR"
        APP_PATH="$APP_PATH_USER"
        APP_USE_SUDO=0
        osacompile -o "$APP_PATH" "$tmp_script" >/dev/null 2>&1 || {
          warn "Failed to create wrapper app (osacompile failed)."
          rm -f "$tmp_script" || true
          say "You can still use it from Terminal: open_lnk \"/path/to/file.lnk\""
          pause_end; exit 0
        }
      }
    else
      osacompile -o "$APP_PATH" "$tmp_script" >/dev/null 2>&1 || {
        warn "Failed to create wrapper app (osacompile failed)."
        rm -f "$tmp_script" || true
        say "You can still use it from Terminal: open_lnk \"/path/to/file.lnk\""
        pause_end; exit 0
      }
    fi
    rm -f "$tmp_script" || true
    ok "Wrapper app created."

    INFO_PLIST="$APP_PATH/Contents/Info.plist"
    BUNDLE_ID="com.secretguest.openlnk"
    APP_VER="0.0.16"
    if [ -n "${BIN_INSTALLED:-}" ] && [ -x "$BIN_INSTALLED" ]; then
      APP_VER="$("$BIN_INSTALLED" --version 2>/dev/null || echo 0.0.16)"
    fi

    # Patch Info.plist to be LaunchServices-friendly.
    if [ -f "$INFO_PLIST" ]; then
      say "[*] Patching Info.plist (bundle id, executable, version, .lnk document types)..."

      # Identify executable created by osacompile: usually "applet".
      # Keeping it stable helps "Change All" to stick.
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Set :CFBundleExecutable applet" >/dev/null 2>&1 || \
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleExecutable string applet" >/dev/null 2>&1 || true

      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Set :CFBundleIdentifier $BUNDLE_ID" >/dev/null 2>&1 || \
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleIdentifier string $BUNDLE_ID" >/dev/null 2>&1 || true

      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Set :CFBundleName Open LNK" >/dev/null 2>&1 || \
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleName string Open LNK" >/dev/null 2>&1 || true

      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Set :CFBundleDisplayName Open LNK" >/dev/null 2>&1 || \
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleDisplayName string Open LNK" >/dev/null 2>&1 || true

      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Set :CFBundlePackageType APPL" >/dev/null 2>&1 || \
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundlePackageType string APPL" >/dev/null 2>&1 || true

      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Set :CFBundleShortVersionString $APP_VER" >/dev/null 2>&1 || \
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleShortVersionString string $APP_VER" >/dev/null 2>&1 || true

      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Set :CFBundleVersion $APP_VER" >/dev/null 2>&1 || \
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleVersion string $APP_VER" >/dev/null 2>&1 || true

      # Clean and re-add document types
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Delete :CFBundleDocumentTypes" >/dev/null 2>&1 || true
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleDocumentTypes array" >/dev/null 2>&1 || true
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleDocumentTypes:0 dict" >/dev/null 2>&1 || true
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleDocumentTypes:0:CFBundleTypeName string Windows Shortcut" >/dev/null 2>&1 || true
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleDocumentTypes:0:CFBundleTypeRole string Viewer" >/dev/null 2>&1 || true
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions array" >/dev/null 2>&1 || true
      plist_buddy "$APP_USE_SUDO" "$INFO_PLIST" "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions:0 string lnk" >/dev/null 2>&1 || true
    else
      warn "Info.plist not found; default handler may not work in Finder."
    fi

    # Refresh LaunchServices registration (best-effort).
    LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
    if [ -x "$LSREGISTER" ]; then
      if [ "$APP_USE_SUDO" -eq 1 ]; then
        run_sudo_nopass "$LSREGISTER" -f "$APP_PATH" >/dev/null 2>&1 || true
      else
        "$LSREGISTER" -f "$APP_PATH" >/dev/null 2>&1 || true
      fi
    fi

    say "Tip: Finder -> right click a .lnk -> Get Info -> Open with -> Open LNK -> Change All"
    say "If Open With doesn't refresh: run 'killall Finder' or log out/in."
  else
    warn "osacompile not found; can't create a Finder wrapper app."
    say "You can still use it from Terminal: open_lnk \"/path/to/file.lnk\""
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

  DESK_EXEC="$BIN_INSTALLED"
  if [ -z "${DESK_EXEC:-}" ]; then
    DESK_EXEC="open_lnk"
  fi

  sudo_ok=0
  if [ "$DESK_EXEC" = "$BIN_USER" ]; then
    sudo_ok=0
  elif have_sudo_nopass; then
    sudo_ok=1
  fi

  if [ "$sudo_ok" -eq 1 ]; then
    if [ -f "$ICON_SRC" ]; then
      run_sudo_nopass install -D -m0644 "$ICON_SRC" "$ICON_SYS"
      ok "Icon at $ICON_SYS"
    else
      warn "Icon missing: $ICON_SRC"
    fi

    run_sudo_nopass install -D -m0644 /dev/stdin "$DESK_SYS" <<EOF
[Desktop Entry]
Name=Open LNK
Comment=Open Windows .lnk shortcuts
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
Comment=Open Windows .lnk shortcuts
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
    if have_sudo_nopass; then
      run_sudo_nopass update-desktop-database /usr/share/applications 2>/dev/null || true
    fi
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
  fi

  if need_cmd gtk-update-icon-cache; then
    if have_sudo_nopass; then
      run_sudo_nopass gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    fi
    gtk-update-icon-cache -f "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
  fi

  if need_cmd xdg-mime; then
    say "[*] Setting default handler for .lnk (best-effort)..."
    xdg-mime default "$DESKTOP_NAME" application/x-ms-shortcut 2>/dev/null || \
      warn "xdg-mime failed (MIME type may not be registered system-wide)"
  fi
fi

ok "Done."
if [ "$OS" = "Linux" ]; then
  say "If double-click doesn't work yet, log out/in or run: update-desktop-database"
fi

pause_end
