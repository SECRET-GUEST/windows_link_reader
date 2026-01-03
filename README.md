![C](https://img.shields.io/badge/C-GCC-purple)
![Version](https://img.shields.io/badge/LINUX-yellow) ![Version](https://img.shields.io/badge/MacOS-white) 
```
██╗     ███╗   ██╗██╗  ██╗    ██████╗ ███████╗ █████╗ ██████╗ ███████╗██████╗ 
██║     ████╗  ██║██║ ██╔╝    ██╔══██╗██╔════╝██╔══██╗██╔══██╗██╔════╝██╔══██╗
██║     ██╔██╗ ██║█████╔╝     ██████╔╝█████╗  ███████║██║  ██║█████╗  ██████╔╝
██║     ██║╚██╗██║██╔═██╗     ██╔══██╗██╔══╝  ██╔══██║██║  ██║██╔══╝  ██╔══██╗
███████╗██║ ╚████║██║  ██╗    ██║  ██║███████╗██║  ██║██████╔╝███████╗██║  ██║
╚══════╝╚═╝  ╚═══╝╚═╝  ╚═╝    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═════╝ ╚══════╝╚═╝  ╚═╝
                                                                              
```

# LNK Reader 🖥️

`open_lnk` is a small CLI tool that reads Windows `.lnk` (Shell Link) shortcut files on Linux/macOS, resolves the target to a local path or a network URI, and opens it with your desktop default handler.

At a high level it:

1. Parses the `.lnk` binary format (subset of the Microsoft Shell Link specification).
2. Builds the best target path it can from the available fields.
3. Resolves Windows-style paths to Linux/macOS locations (mapping file, mounts, GVFS, etc.).
4. Opens the final path/URI using `xdg-open` (Linux) or `open` (macOS).

[Demo video](https://github.com/SECRET-GUEST/windows_link_reader/assets/92639080/f92222d6-e028-4166-8e6d-a9c7bd40f144)

---

## 🌟 Features

- Parses common Shell Link fields (ANSI + Unicode):
  - `LocalBasePath`, `CommonPathSuffix`, `RelativePath`, `WorkingDir`, `Arguments`, `IconLocation`
- UTF-16LE to UTF-8 conversion (including surrogate pairs)
- Windows path normalization (`\` → `/`) for Unix filesystem checks
- **Debug mode** (`--debug` or `WINDOWS_LINK_READER_DEBUG=1`)
  - Always shows the parsed Windows target path
  - Shows the final Linux path or URI candidate
- **Assistant mode** (`--assist` or `WINDOWS_LINK_READER_ASSIST=1`)
  - Helps create or fix mappings interactively when resolution fails
  - Works both in terminal and via GUI dialogs (`zenity` / `kdialog`)
- Drive-letter resolution:
  - User mapping file (recommended)
  - Automatic `/proc/mounts` probing with scoring (Linux)
  - Optional interactive assistant fallback
- UNC resolution:
  - User mapping file (recommended)
  - GVFS mounts (GNOME)
  - CIFS mounts (`/proc/mounts`)
  - SMB URI fallback (`smb://...`, percent-encoded)
- Safer resolution logic:
  - Avoids accidental “match by chance”
  - Rejects ambiguous mount candidates
- Error reporting:
  - Best-effort desktop notifications (Linux/macOS)
  - Always prints a clear error to stderr
- Installer/uninstaller scripts for convenience (`setup.sh`, `uninstall.sh`)

---

## 🔍 Prerequisites

Build-time:

- A C compiler: `gcc` or `clang`

Runtime:

- Linux: `xdg-open` (usually from `xdg-utils`)
- macOS: `open` (built-in)

Optional (nice-to-have):

- Linux notifications: `notify-send`
- GUI assistant dialogs: `zenity` or `kdialog`

---

## 📥 Installation

```bash
chmod +x setup.sh
./setup.sh
```

What `setup.sh` does:

* Compiles `open_lnk` from the sources under `src/` (includes from `include/`)
* Installs the binary:

  * Tries `/usr/local/bin/open_lnk` (via `sudo`)
  * Falls back to `~/.local/bin/open_lnk`
* On Linux:

  * Creates a desktop entry (`open_lnk.desktop`)
  * Tries to register a default handler for `.lnk`
* On Debian/Ubuntu systems:

  * May try to install optional packages via `apt-get` (network required)

From a terminal:

```bash
open_lnk /path/to/file.lnk
```

Debug / assist modes:

```bash
open_lnk --debug file.lnk
open_lnk --assist file.lnk
```

Or via environment variables:

```bash
WINDOWS_LINK_READER_DEBUG=1 open_lnk file.lnk
WINDOWS_LINK_READER_ASSIST=1 open_lnk file.lnk
```

After installation on Linux, you can also double-click `.lnk` files (depending on your desktop environment and MIME database state).

---

## ▶️ Configuration (Drive / UNC mappings)

### Mapping file location

The tool loads mappings from:

1. `$WINDOWS_LINK_READER_MAP` (if set), otherwise
2. `$XDG_CONFIG_HOME/windows-link-reader/mappings.conf` (if `XDG_CONFIG_HOME` is set), otherwise
3. `~/.config/windows-link-reader/mappings.conf`

The file is created automatically when needed (for example in assistant mode).

### Mapping file format

One rule per line:

```ini
# Drive letter mapping:
F:=/media/me/F_Daten

# UNC mapping:
//server/share=/mnt/share
\\server\\share=/mnt/share
```

Notes:

* Empty lines and lines starting with `#` are ignored.
* Prefixes considered dangerous are ignored (examples: `/`, `/proc`, `/sys`, `/dev`, …).

---

## 🧭 How resolution works (quick overview)

### UNC paths (`//server/share/...`)

1. Mapping table (`mappings.conf`)
2. GVFS mount lookup (GNOME)
3. CIFS mount lookup (`/proc/mounts`)
4. Fallback: build an encoded `smb://...` URI and ask the desktop to open it

### Drive paths (`X:/...`)

1. Mapping table (`mappings.conf`)
2. `/proc/mounts` scoring (best-effort guess)
3. Assistant prompt (terminal or GUI) to learn the correct mount prefix

---

## 🗑️ Uninstallation

```bash
chmod +x uninstall.sh
./uninstall.sh
```

What `uninstall.sh` removes (best-effort):

- The installed binary:
  - `/usr/local/bin/open_lnk` (via `sudo`, if available)
  - `~/.local/bin/open_lnk`
- Desktop entry:
  - `/usr/share/applications/open_lnk.desktop` (via `sudo`, if available)
  - `~/.local/share/applications/open_lnk.desktop`
- Attempts to refresh desktop/mime databases if the tools exist

---

## 🌴 Tree

This version is split into small modules:

- Entry point: `src/main.c`
- Public headers: `include/open_lnk/`
- LNK parsing and target building: `src/lnk/`
- Path resolution (mappings/mounts/GVFS/UNC helpers): `src/resolve/`
- Platform integration (open + error notifications): `src/platform/`
- Generic helpers: `src/util/`

The old single-file source name (`lnkReader.c`) is kept as a short "pointer" file for legacy reference.

---

## ⛔ Limitations

- This is not a full implementation of every possible Shell Link feature (for example, many ExtraData blocks are ignored).
- Opening is best-effort:
  - `open_with_desktop()` launches the system opener but does not wait for it.
  - A desktop environment may behave differently depending on configuration.
- Some paths may not be resolvable without a correct mapping file or an existing mount.


---

## 📜 License

Released under [MIT License](LICENSE).

---

## ❓ Support

Open an [issue](https://github.com/SECRET-GUEST/windows_link_reader/issues) .


```
     _ ._  _ , _ ._            _ ._  _ , _ ._    _ ._  _ , _ ._      _ ._  _ , _ .__  _ , _ ._   ._  _ , _ ._   _ , _ ._   .---.  _ ._   _ , _ .__  _ , _ ._   ._  _ , _ ._      _ ._  _ , _ .__  _ , _ . .---<__. \ _
   (_ ' ( `  )_  .__)        (_ ' ( `  )_  .__ (_ ' ( `  )_  .__)  (_ '    ___   ._( `  )_  .__)  ( `  )_  .__)   )_  .__)/     \(_ ' (    )_  ._( `  )_  .__)  ( `  )_  .__)  (_ ' ( `  )_  ._( `` )_  . `---._  \ \ \
 ( (  (    )   `)  ) _)    ( (  (    )   `)  ) (  (    )   `)  ) _ (  (   (o o) )     )   `)  ) _    )   `)  ) _    `)  ) \.@-@./(  (    )   `)     )   `)  ) _    )   `)  ) _ (  (    )   `)         `) ` ),----`- `.))  
(__ (_   (_ . _) _) ,__)  (__ (_   (_ . _) _) _ (_   (_ . _) _) ,__ (_   (  V  ) _) (_ . _) _) ,_  (_ . _) _) ,_ . _) _) ,/`\_/`\ (_   (  . _) _) (_ . _) _) ,_  (_ . _) _) ,__ (_   (_ . _) _) (__. _) _)/ ,--.   )  |
    `~~`\ ' . /`~~`           `~~`\ ' . /`~~`   `~~`\ ' . /`~~`     `~~`/--m-m- ~~`\ ' . /`~~`   `\ ' . /`~~`  `\ ' . /  //  _  \\ ``\ '  . /`~~`\ ' . /`~~`   `\ ' . /`~~`     `~~`\ ' . /`~~`\ ' . /`~~/_/    >     |
         ;   ;                     ;   ;             ;   ;               ;   ;      ;   ;          ;   ;         ;   ;  | \     )|_   ;    ;      ;   ;          ;   ;               ;   ;      ;   ;    |,\__-'      |
         /   \                     /   \             /   \               /   \      /   \          /   \         /   \ /`\_`>  <_/ \  /    \      /   \          /   \               /   \      /   \     \__         \
________/_ __ \___________________/_ __ \___________/_ __ \______ __ ___/_ __ \____/_ __ \________/_ __ \_______/_ __ \\__/'---'\__/_/_  __ \____/_ __ \________/_ __ \_____ _______/_ __ \____/_ __ \____ __\___      )
```

