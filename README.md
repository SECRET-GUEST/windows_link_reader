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
👉 *You double-click a `.lnk` file, it opens the correct target ; even if it lives on a different drive, partition, or network share.*

---

## 📚 Table of Contents

- [Overview](#lnk-reader-️)
- [How it works](#-how-it-works-high-level)
- [Key Features](#-key-features)
- [Prerequisites](#-prerequisites)
- [Installation](#-installation)
- [Usage](#-usage)
- [Configuration files](#️-configuration-files)
- [Limitations](#-limitations)
- [License](#-license)
- [Support](#-support)

---

## 🧠 How it works (high level)

When a `.lnk` file is opened, `open_lnk`:

1. Parses the Windows **Shell Link** binary format (subset of the official Microsoft specification).
2. Extracts the most reliable target path from the available fields.
3. Translates Windows paths to Linux/macOS equivalents:
   - Drive letters (`X:\...`)
   - UNC paths (`\\server\share\...`)
4. Tries multiple resolution strategies automatically.
5. Opens the resolved path or network URI using the system default handler.

If the target **cannot be resolved automatically**, a **graphical assistant** is shown to help the user select the correct mount point ; and the choice is remembered for next time.

[Demo video](https://github.com/SECRET-GUEST/windows_link_reader/assets/92639080/f92222d6-e028-4166-8e6d-a9c7bd40f144)

---

## 🌟 Key Features

### Core features

- Parses common Shell Link fields (ANSI + Unicode):
  - `LocalBasePath`, `CommonPathSuffix`, `RelativePath`
  - `WorkingDir`, `Arguments`, `IconLocation`
- Full UTF-16LE → UTF-8 conversion (including surrogate pairs)
- Windows path normalization (`\` → `/`)
- Best-effort resolution with safe fallbacks
- Opens targets via:
  - `xdg-open` (Linux)
  - `open` (macOS)

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

- A **GUI dialog** lists currently mounted locations
- The user selects the correct mount point (or enters one manually)
- The association is saved **only for this specific `.lnk` file**

This means:

- A shortcut pointing to drive **A:** will not interfere with one pointing to **F:**
- Re-opening the same `.lnk` is instant
- No global or dangerous assumptions are made

The cache is stored safely and updated atomically (latest-wins, no duplicates).

---

### 🪟 Graphical assistant (no terminal required)

- Automatically shown **only when resolution fails**
- Implemented via standard desktop dialogs (`zenity` or compatible tools)
- Works when launched from:
  - File manager (double-click)
  - “Open with”
- No command-line interaction required for normal users

---

### Error handling & diagnostics

- Clear desktop notifications on failure
- Safe fallbacks (parent directory, URI)
- Optional debug output (for developers)

---

## 🔍 Prerequisites

### Build-time

- A C compiler (`gcc` or `clang`)
- `make`

### Runtime

- Linux: `xdg-open` (from `xdg-utils`)
- macOS: `open` (built-in)

Optional (recommended on Linux):

- `zenity` or compatible dialog tool (graphical assistant)
- `notify-send` (desktop notifications)

---

## 📥 Installation

### From source (manual)

```bash
make
sudo make install
````

(You can also use `setup.sh` for convenience by simply run it as a program.)

---

## ▶️ Usage

**Normal usage (recommended):**

* Double-click a `.lnk` file
* Or right-click → *Open with* → **LNK Reader**

Command-line usage exists mainly for debugging and development and is **not required** for normal users.

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

# UNC mapping
//server/share=/mnt/share
```

### Per-link cache

Stored separately and managed automatically.
No manual editing required.

---

## ⛔ Limitations

* Not a full implementation of every Shell Link feature (some ExtraData blocks are ignored)
* Resolution is best-effort and depends on available mounts
* Network shares may still require authentication handled by the OS

---

## 📜 License

Released under the **MIT License**.

---

## ❓ Support

Please open an issue on GitHub if you encounter a problem or have suggestions:
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

