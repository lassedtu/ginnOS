# The apps

ginnOS has these apps on the desktop. Open them from the launcher (see
[The desktop](desktop.md#the-launcher)), or type the command in the
terminal.

| App | Command | Category | What it does |
| --- | --- | --- | --- |
| Mimir | `mimir` | System | the terminal |
| Yggdrasil | `yggdrasil` | System | the file manager |
| Saga | `saga` | Accessories | the text editor |
| Freyja | `freyja` | Accessories | the image viewer |
| Huginn | `huginn` | System | the system monitor |
| Settings | `settings` | System | the settings |
| Gallery | `idunn-gallery` | Development | all the idunn widgets, in light and dark |
| Window demo | `heimdemo 2` | Development | a test of the keyboard, the pointer and the clipboard |
| DOOM | `doom` | Games | Doom in a window (only in an image built with `make DOOM=1`) |

The launcher reads the apps from the `.app` files in
`/usr/share/applications`. The format is in
[`apps.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/apps/include/apps.h).

## Mimir

Mimir is the terminal. It starts the shell, `/bin/sh`, in your home
folder. See [The shell](shell.md).

| Key | Result |
| --- | --- |
| Ctrl+Shift+C | copy the selected text |
| Ctrl+Shift+V | paste |
| Shift+Page Up, Shift+Page Down | scroll back and forward (the mouse wheel also scrolls) |
| Ctrl+plus, Ctrl+minus, Ctrl+0 | larger text, smaller text, the normal size |

Drag to select text. A double-click selects a word, and a triple-click
selects a line. If a program in the terminal uses the mouse, hold Shift
to select text.

The window closes when the shell stops. The normal text size is on the
Terminal page of Settings.

## Yggdrasil

Yggdrasil is the file manager. The sidebar has the Places (Home,
Desktop, Documents, Downloads, Pictures, Music), Computer and Trash. The
toolbar has Back, Forward, Parent folder, the path of the folder, the
Filter field, Sort, and List or Icons.

Right-click a file or a folder for its menu: Open, Open in Mimir (or Run
in Mimir for a program), Cut, Copy, Rename, Move to Trash, Delete
permanently..., and Properties. Right-click an empty area for New
folder, New file, Paste and Open in Mimir.

| Key | Result |
| --- | --- |
| Backspace, Alt+Up | the parent folder |
| Alt+Left, Alt+Right | back, forward |
| Ctrl+L | type a path |
| Ctrl+F | filter by name |
| Ctrl+H | show or hide the hidden files |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste |
| Ctrl+Shift+N | a new folder |
| F2 | rename |
| F5 | read the folder again |
| Delete | move to the Trash |
| Shift+Delete | delete permanently |

The Trash is `~/.local/share/Trash`. In the Trash, the menu has Restore,
Delete permanently... and Empty Trash....

## Saga

Saga is the text editor. To open a file from the terminal, type
`saga <file>`. If the file does not exist, Saga makes it when you save.

| Key | Result |
| --- | --- |
| Ctrl+N, Ctrl+O | new, open |
| Ctrl+S, Ctrl+Shift+S | save, save as |
| Ctrl+F, Ctrl+H | find, replace |
| F3, Shift+F3 | the next match, the match before |
| Ctrl+plus, Ctrl+minus, Ctrl+0 | larger text, smaller text, the normal size |
| Ctrl+W | close |

## Freyja

Freyja shows pictures: PNG, JPEG, BMP, PPM and PGM. Open a picture from Yggdrasil, or
type `freyja <picture>`.

| Key | Result |
| --- | --- |
| Left, Right (also Page Up, Page Down, Space) | the picture before, the next picture in the folder |
| plus, minus | zoom in, zoom out |
| 0 or F, 1 | fit in the window, 100 % |
| R, Shift+R | turn right, turn left (Freyja does not save the turn) |
| Ctrl+O, Ctrl+W | open, close |

## Huginn

Huginn shows what the system does. It reads `/proc` each second.

- **Processes** lists the processes with their CPU, memory, user and
  PID. Sort sorts them by CPU, Memory, Name or PID. Type in the Filter by
  name or PID field to find a process. End process asks, then sends
  `SIGTERM` to the selected process. Right-click a process for Kill
  process, which sends `SIGKILL`.
- **Resources** shows graphs of the CPU and the memory, one column for
  each second.

## Settings

Settings has a page for each group of settings. The Search field finds
a setting by its name.

| Page | What you can set |
| --- | --- |
| Appearance | the theme (Light or Dark), the accent colour (Rose, Blue, Green, Orange, Violet, Neutral), the text size, the scale, animations, and the wallpaper |
| Display | the screen resolution (only with a graphics card that can change it, for example in QEMU) |
| Keyboard | the keyboard layout, with a field to test it |
| Sound | the output volume of the sound card |
| Terminal | the text size of Mimir |
| About | the version, the architecture, the processor, the memory, the screen and the sound card |

A change applies to all open windows at once. Each setting is in a file
in `~/.config/ginnos/`, so you can also change it with the `conf`
command:

```sh
$ conf theme theme.accent blue
```
