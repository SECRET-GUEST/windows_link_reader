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

**LNK Reader** lets you open `.lnk` shortcut files on **Linux** and **macOS**.

A `.lnk` file is usually a shortcut created on Windows.

With LNK Reader, you can double-click a `.lnk` file and open its real target, even when the file points to:

- another drive
- an external disk
- a mounted partition
- a shared network folder
- an SMB share like `\\server\share`

The goal is simple:

> Double-click the shortcut.  
> Open the real file or folder.

No terminal needed for normal use.

---

## What is this for?

This app is useful if you are on Linux or macOS and you receive `.lnk` files created on Windows.

For example, someone sends you a shortcut that points to:

```text
\\192.168.1.50\shared_folder\project
```

or:

```text
F:\Documents\Project
```

Normally, Linux and macOS do not open `.lnk` files like Windows does.

LNK Reader tries to read the shortcut, understand where it points, and open the correct place on your system.

---

## What it can open

LNK Reader can open shortcuts pointing to:

- normal files
- folders
- mounted drives
- external disks
- network folders
- SMB shares

Examples:

```text
F:\Documents\file.pdf
```

```text
\\server\share\folder
```

```text
\\192.168.1.50\share_name\folder\file.txt
```

---

## What it cannot do

LNK Reader does **not** magically give access to private folders.

If a network share needs a username or password, your operating system may still ask for it.

LNK Reader can help open the right address, but it cannot bypass permissions.

---

## Installation

### Easy installation

Run:

```bash
./setup.sh
```

This installs the app and tries to set up desktop integration.

---

### Manual installation

You can also build and install it manually:

```bash
make
sudo make install
```

---

## Uninstall

Run:

```bash
./uninstall.sh
```

This removes the installed app and desktop integration.

It does not delete your personal files.

---

## How to use it

### Normal usage

Double-click a `.lnk` file.

Or right-click it and choose:

```text
Open With → LNK Reader
```

On macOS, after installation, use:

```text
Open With → Open LNK
```

---

## macOS setup note

On macOS, the real command-line tool is called `open_lnk`.

Finder usually does not show command-line tools in the “Open With” menu.

That is why the installer creates a small app wrapper:

```text
Open LNK.app
```

After running `setup.sh`, you can set it as the default app for `.lnk` files:

```text
Right-click a .lnk file
Get Info
Open with
Choose Open LNK
Change All
```

If Finder does not refresh immediately, restart Finder:

```bash
killall Finder
```

---

## Network shares and SMB

Many `.lnk` files point to shared folders.

Example:

```text
\\192.168.1.50\share_name
```

LNK Reader understands this kind of path.

On macOS, it can try to open it directly as:

```text
smb://192.168.1.50/share_name
```

This helps avoid asking the user to manually select a mounted folder when the system can already handle SMB links.

The operating system may still ask for credentials if the share is protected.

---

## Optional mappings

Sometimes a shortcut points to a Windows path, but your Linux or macOS system uses another path.

For example, the shortcut says:

```text
F:\Projects
```

But on your system, that drive is mounted here:

```text
/media/me/PROJECTS
```

You can tell LNK Reader how to translate paths using a mapping file.

Mapping file location:

```text
~/.config/windows-link-reader/mappings.conf
```

---

## Mapping examples

### Drive mapping

```ini
F:=/media/me/PROJECTS
```

This means:

```text
F:\something
```

becomes:

```text
/media/me/PROJECTS/something
```

---

### Network share mapped to a local folder

```ini
//server/share=/mnt/share
```

This means:

```text
\\server\share\folder
```

becomes:

```text
/mnt/share/folder
```

---

### Network share mapped directly to SMB

```ini
//server/share=smb://server/share
```

This means:

```text
\\server\share\folder
```

becomes:

```text
smb://server/share/folder
```

Another example:

```ini
//192.168.1.50/share_name=smb://192.168.1.50/share_name
```

This is especially useful on macOS.

---

## Supported shortcuts in mappings

On the right side of a mapping, you can use:

```text
~
~/folder
$HOME
$HOME/folder
${HOME}
${HOME}/folder
```

Example:

```ini
Z:=$HOME/my_drive
```

For network share names, you can also use:

```text
$USER
${USER}
```

Example:

```ini
\\server\home\$USER=$HOME/server_home
```

No other shell expansion is done.

---

## What happens if LNK Reader cannot find the target?

If LNK Reader cannot automatically find the target, it can show a small assistant window.

The assistant asks you to select the correct mounted folder.

For example, if a shortcut points to:

```text
F:\Documents
```

LNK Reader may ask you where drive `F:` is mounted on your system.

Once you select the correct folder, LNK Reader remembers it for next time.

---

## Cache

LNK Reader stores remembered choices here:

```text
~/.cache/windows-link-reader/links.conf
```

or:

```text
$XDG_CACHE_HOME/windows-link-reader/links.conf
```

You usually do not need to edit this file manually.

To clear the cache:

```bash
open_lnk --clear-cache
```

---

## Logs

When launched from a graphical desktop, LNK Reader writes a small log file here:

```text
~/.cache/windows-link-reader/open_lnk.log
```

or:

```text
$XDG_CACHE_HOME/windows-link-reader/open_lnk.log
```

This can help when reporting a bug.

---

## Developer usage

You can run the tool manually:

```bash
open_lnk file.lnk
```

Debug mode:

```bash
open_lnk --debug file.lnk
```

Show version:

```bash
open_lnk --version
```

Clear cache:

```bash
open_lnk --clear-cache
```

---

## Requirements

### Build requirements

- C compiler, for example `gcc` or `clang`
- `make`

### Runtime requirements

Linux:

- `xdg-open`

macOS:

- `open`, included by default

Optional on Linux:

- `zenity`, for graphical dialogs
- `notify-send`, for desktop notifications

---

## Limitations

LNK Reader is a best-effort tool.

It supports many common `.lnk` files, but not every possible advanced shortcut feature.

Network folders may still require login credentials.

SMB links depend on the operating system’s SMB support.

If a shortcut points to a file that does not exist anymore, LNK Reader cannot open it.

---

## Troubleshooting

### The shortcut does not open

Try running:

```bash
open_lnk --debug file.lnk
```

Then check the log file:

```text
~/.cache/windows-link-reader/open_lnk.log
```

---

### The wrong folder opens

Clear the remembered choices:

```bash
open_lnk --clear-cache
```

Then try opening the `.lnk` file again.

---

### A network share asks for a password

That is normal if the SMB share is protected.

LNK Reader can open the SMB address, but the operating system handles authentication.

---

## Support

If you find a real bug or have a useful suggestion, please open an issue:

```text
https://github.com/SECRET-GUEST/windows_link_reader/issues
```

When reporting a bug, please include:

- your operating system
- what the `.lnk` file points to, if possible
- whether the target is local, mounted, or network-based
- the log file, if available

---

## License

See the license file in this repository.
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
