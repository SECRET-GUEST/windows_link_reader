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

**open_lnk** is a lightweight desktop utility that opens Windows `.lnk` (Shell Link) shortcut files directly on Linux (and macOS).

It is designed first and foremost for **double-click / “Open with” usage**, not for manual command-line interaction.

The goal is simple:
👉 *You double-click a `.lnk` file, it opens the correct target — even if it lives on a different drive, partition, or network share.*

---

## 📚 Table of Contents

* [Overview](#lnk-reader-️)
* [How it works](#-how-it-works-high-level)
* [Key Features](#-key-features)
* [Prerequisites](#-prerequisites)
* [Installation](#-installation)
* [Uninstall](#-uninstall)
* [Usage](#-usage)
* [Configuration files](#️-configuration-files)
* [Limitations](#-limitations)
* [License](#-license)
* [Support](#-support)

---

## 🧠 How it works (high level)

When a `.lnk` file is opened, `open_lnk`:

1. Parses the Windows **Shell Link** binary format (subset of the official Microsoft specification).
2. Extracts the most reliable target path from the available fields.
3. Translates Windows paths to Linux/macOS equivalents:

   * Drive letters (`X:\...`)
   * UNC paths (`\\server\share\...`)
4. Tries multiple resolution strategies automatically.
5. Opens the resolved path or network URI using the system default handler.

If the target **cannot be resolved automatically**, a **graphical assistant** is shown to help the user select the correct mount point, and the choice is remembered for next time.

[Demo video](https://github.com/SECRET-GUEST/windows_link_reader/assets/92639080/f92222d6-e028-4166-8e6d-a9c7bd40f144)

---

## 🌟 Key Features

### Core features

* Parses common Shell Link fields (ANSI + Unicode):

  * `LocalBasePath`, `CommonPathSuffix`, `RelativePath`
  * `WorkingDir`, `Arguments`, `IconLocation`
* Full UTF-16LE → UTF-8 conversion (including surrogate pairs)
* Windows path normalization (`\` → `/`)
* Best-effort resolution with safe fallbacks
* Opens targets via:

  * `xdg-open` (Linux)
  * `open` (macOS)

---

### Smart path resolution

#### Drive letters (`X:/...`)

Resolution order:

1. **Per-link cache** (exact `.lnk` → mount prefix association)
2. User mapping file (`mappings.conf`)
3. Automatic mount probing (`/proc/mounts`, scored)
4. **Graphical assistant** (if still unresolved)

#### UNC paths (`//server/share/...`)

Resolution order:

1. User mapping file
2. GVFS mounts (GNOME)
3. CIFS mounts (`/proc/mounts`)
4. Fallback to `smb://` URI (percent-encoded)

---

### 🧠 Intelligent per-link cache

When a `.lnk` cannot be resolved automatically:

* A **GUI dialog** lists currently mounted locations
* The user selects the correct mount point (or uses a folder chooser)
* If the merged preview exists, `open_lnk` saves:
  * A **global mapping rule** (drive letter or UNC share) into `mappings.conf`
  * A **per-link cache** entry for this specific `.lnk` file

This means:

* A shortcut pointing to drive **A:** will not interfere with one pointing to **F:**
* Re-opening the same `.lnk` is instant
* No global or dangerous assumptions are made

The cache is stored safely and updated atomically (latest-wins, no duplicates).
Invalid cache entries are also self-healed automatically:

* entries pointing to the Trash are ignored and removed
* entries whose rebuilt target no longer exists are removed before the normal resolution flow continues
* assistant-selected cache entries are only saved after a successful open

---

### 🪟 Graphical assistant (no terminal required)

* Automatically shown **only when resolution fails**
* Implemented via standard desktop dialogs (`zenity` or compatible tools)
* Works when launched from:

  * File manager (double-click)
  * “Open with”
* No command-line interaction required for normal users

**What it shows (so it’s not a black box):**

* Windows **prefix** (server/share or drive)
* Windows **suffix**
* Detected Linux mount points
* A preview of the final merged path
  Matching is done on the **prefix only**.

---

### Error handling & diagnostics

* Clear desktop notifications on failure
* Safe fallbacks (parent directory, URI)
* Optional debug output (for developers)
* GUI runs also write a small log file: `~/.cache/windows-link-reader/open_lnk.log` (or `$XDG_CACHE_HOME/windows-link-reader/open_lnk.log`)

---

## 🔍 Prerequisites

### Build-time

* A C compiler (`gcc` or `clang`)
* `make`

### Runtime

* Linux: `xdg-open` (from `xdg-utils`)
* macOS: `open` (built-in)

Optional (recommended on Linux):

* `zenity` or compatible dialog tool (graphical assistant)
* `notify-send` (desktop notifications)

---

## 📥 Installation

### Convenience installer

You just have to run:

```bash
./setup.sh
```


### Alternative manual way : 

You also can run it with make

```bash
make
sudo make install
```

What it does:

* Builds and installs `open_lnk`
* Installs desktop integration on Linux (desktop entry + icon)
* On macOS, creates a small Finder wrapper app **Open LNK.app** so you can use *Open With* / double-click (`/Applications` preferred, fallback: `~/Applications`)

---

## 🧹 Uninstall

Run:

```bash
./uninstall.sh
```

This removes:

* `open_lnk` binary (system and user locations)
* Desktop entries/icons and refreshes caches (Linux, best-effort)
* macOS wrapper app (`/Applications/Open LNK.app` and `~/Applications/Open LNK.app`) if present

No reboot required.

---

## ▶️ Usage

**Normal usage (recommended):**

* Double-click a `.lnk` file
* Or right-click → *Open with* → **LNK Reader** / **Open LNK**

**macOS note:**
`open_lnk` is a command-line tool. Finder cannot list CLI binaries in *Open With*.
After running `setup.sh`, use the generated **Open LNK.app** wrapper to set it as default handler:
Finder → Get Info → Open with → **Open LNK** → Change All.
If Finder doesn't refresh the list, run: `killall Finder` (or log out/in).

**Simple maintenance command:**

```bash
open_lnk --clear-cache
```

This removes the per-link cache file and exits without processing any `.lnk`.

---

## ⚙️ Configuration files

### Mapping file (optional)

Used for global drive / UNC mappings:

```
~/.config/windows-link-reader/mappings.conf
```

Example:

```ini
# Drive letter mapping
F:=/media/me/F_Daten
X:=~/nas/Z
Y:=$HOME/nas/Z
Z:=${HOME}/nas/Z

# UNC mapping
//server/share=/mnt/share
//server/share=~/mnt/share
\\share_url.fr\home\$USER=$HOME/nas/P
```

On the right-hand side only, `open_lnk` supports these limited HOME shortcuts:

* `~`
* `~/...`
* `$HOME`
* `$HOME/...`
* `${HOME}`
* `${HOME}/...`

No other shell expansion is performed.

On the left-hand side only for UNC rules, `open_lnk` supports these limited USER tokens:

* `$USER`
* `${USER}`

These USER tokens are expanded before UNC normalization and matching. No other shell variables or shell syntax are expanded on the UNC side.

### Per-link cache

Stored separately and managed automatically:

```text
~/.cache/windows-link-reader/links.conf
```

or:

```text
$XDG_CACHE_HOME/windows-link-reader/links.conf
```

Notes:

* No manual editing is normally required
* invalid or suspicious entries are removed automatically
* `open_lnk --clear-cache` deletes this file if you want a full reset

---

## ⛔ Limitations

* Not a full implementation of every Shell Link feature (some ExtraData blocks are ignored)
* Resolution is best-effort and depends on available mounts
* Network shares may still require authentication handled by the OS

---

## ❓ Support

Please open an issue on GitHub if you encounter a (real) problem or have (a useful) suggestion:
[https://github.com/SECRET-GUEST/windows_link_reader/issues](https://github.com/SECRET-GUEST/windows_link_reader/issues)


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
