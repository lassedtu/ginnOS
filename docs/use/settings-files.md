# The settings files

Each setting of ginnOS is in a text file. Settings and the apps write
these files for you. You can also change them with the `conf` command or
in a text editor.

The code: [`src/lib/conf/`](https://github.com/lassedtu/ginnOS/tree/main/src/lib/conf)
(the library), [`src/user/conf/`](https://github.com/lassedtu/ginnOS/tree/main/src/user/conf)
(the command)

## Where the files are

Each settings file has a name, for example `theme`, and two layers:

| File | What it has |
| --- | --- |
| `/etc/ginnos/<name>.conf` | the defaults for all users. Only root can change it. |
| `~/.config/ginnos/<name>.conf` | your own values |

A program reads your value first. If you have no value, it uses the
default. A change from Settings or from `conf` goes into your own file
only.

## The format

```ini
# the look of the desktop
[theme]
mode = dark
accent = #3b82f6
font_size = 13
```

- A line that starts with `#` or `;` is a comment. A comment cannot
  follow a value on the same line, so `#3b82f6` is a value.
- `[name]` starts a section. Each `key = value` line is in the section
  above it.
- The spaces around a key and a value are removed. To keep spaces in a
  value, put the value in double quotes.
- If a key is two times in a section, the last one applies.

## Change a setting with conf

```
$ conf theme theme.mode
dark
$ conf theme theme.mode light
$ conf -u theme theme.mode
```

| Command | Result |
| --- | --- |
| `conf <name> <section>.<key>` | shows the value |
| `conf <name> <section>.<key> <value>` | sets your value |
| `conf -u <name> <section>.<key>` | removes your value, so the default applies again |

When the desktop runs, `conf` tells all open programs about the change,
and they apply it at once.

## The files and their keys

| File | Key | Values | Default |
| --- | --- | --- | --- |
| `theme` | `theme.mode` | `light`, `dark` | `dark` |
| `theme` | `theme.accent` | `rose`, `blue`, `green`, `orange`, `violet`, `neutral`, or `#rrggbb` | `rose` |
| `theme` | `theme.font_size` | `13`, `14`, `15` | `13` |
| `theme` | `theme.scale` | `1`, `2`, `auto` (2 on a screen 2400 pixels wide or wider) | `auto` |
| `theme` | `theme.motion` | `true`, `false` (no animations) | `true` |
| `theme` | `theme.radius` | the corner radius, `4` to `12` | `6` |
| `desktop` | `desktop.wallpaper` | the path of a PNG, JPEG or BMP file, or `none` for no picture | `/usr/share/wallpapers/default.png` |
| `keyboard` | `keyboard.layout` | `dk-mac`, `us` | `dk-mac` |
| `mimir` | `terminal.font_size` | `8` to `32` | `13` |
| `saga` | `editor.font_size` | `9` to `32` | `13` |
| `yggdrasil` | `view.sort` | `name`, `size`, `type`, `date` | `name` |
| `yggdrasil` | `view.reverse` | `yes`, `no` | `no` |
| `open-with` | `open.<type>` | the command that opens a file of that type | see below |
| `boot` | `boot.session` | `login`, `text` | `login` |

`boot` is read only from `/etc/ginnos/boot.conf`, at boot. See
[The desktop](desktop.md#start-the-desktop-from-the-console).

The `theme` file can also change one colour of one mode. Put the name
of the colour in a `[light]` or `[dark]` section:

```ini
[dark]
background = #09090b
```

Some programs keep their state in a settings file: `asgard` keeps the
apps you used last (`launcher.recent`), and the login screen keeps the
last user (`login.last`).

### Open with

`open-with.conf` tells Yggdrasil which program opens a file of each
type. The defaults:

```ini
[open]
text/* = saga %f
image/* = freyja %f
audio/wav = mimir -k play %f
application/x-doom-wad = doom -iwad %f
```

- `%f` is the file. If the command has no `%f`, the file goes at the
  end.
- `text/*` is all the types of a kind. `*` is all other types.
- The types are in `/usr/share/ginnos/mime.conf`.

A program file runs, and does not use this table.
