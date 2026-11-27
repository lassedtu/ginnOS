<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# conf

libconf: the settings files.

## &lt;conf.h&gt;

Source: [`src/lib/conf/include/conf.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/conf/include/conf.h)

settings files: sections of "key = value" lines.

a settings file looks like this:

```c
# the look of the desktop
[theme]
mode = dark
accent = #3b82f6
font_size = 13
```

- a line that starts with '#' or ';' is a comment. there are no comments
  after a value, so "#3b82f6" is a value.
- keys before the first \[section] are in the section "".
- spaces around the key and the value are removed. a value in double
  quotes keeps its spaces ("  x  "); the quotes are removed.
- if a key is in a section two times, the last one is used.
- a bad line (no '=', no key, a \[section without ']') is kept as it is
  when the file is written again, and gives a warning on stderr.

a conf\_t has two layers: the system defaults (/etc/ginnos/&lt;name&gt;.conf,
read only) and the user's values (~/.config/ginnos/&lt;name&gt;.conf). a get
looks at the user's values first. a set changes only the user's values,
and conf\_save writes only the user's file. comments, blank lines and the
order of the lines stay as they were.

### conf\_open

```c
conf_t *conf_open(const char *name);
```

open the settings called name: /etc/ginnos/&lt;name&gt;.conf and
~/.config/ginnos/&lt;name&gt;.conf. a file that is not there is empty.

- `name`: letters, digits, '-' and '\_' only.

Returns: the settings, or NULL (a bad name, or no memory).

### conf\_load

```c
conf_t *conf_load(const char *system_path, const char *user_path);
```

open two files. user\_path is the file conf\_save writes.

- `system_path`: may be NULL (no defaults).
- `user_path`: may be NULL (conf\_save then fails).

### conf\_from\_text

```c
conf_t *conf_from_text(const char *system_text, const char *user_text);
```

settings from text in memory (for tests). conf\_save fails on them.

- `system_text`: the defaults, may be NULL.
- `user_text`: the user's values, may be NULL.

### conf\_get

```c
const char *conf_get(const conf_t *conf, const char *section, const char *key,
                     const char *fallback);
```

a value.

Returns: the user's value, else the system value, else fallback. the string is valid until the next set, unset or free.

### conf\_get\_int

```c
long conf_get_int(const conf_t *conf, const char *section, const char *key, long fallback);
```

a whole number: decimal, or hexadecimal with 0x.

Returns: fallback if the key is not there or is not a number.

### conf\_get\_bool

```c
bool conf_get_bool(const conf_t *conf, const char *section, const char *key, bool fallback);
```

true, yes, on, 1 / false, no, off, 0 (any letter case).

Returns: fallback if the key is not there or is not one of these.

### conf\_get\_color

```c
uint32_t conf_get_color(const conf_t *conf, const char *section, const char *key,
                        uint32_t fallback);
```

a colour: #rrggbb or #rrggbbaa.

Returns: 0xAARRGGBB, not premultiplied (gfx\_rgba makes a gfx colour of it); alpha is 0xFF for #rrggbb. fallback if the key is not there or is not a colour.

### conf\_set, conf\_set\_int, conf\_set\_bool, conf\_set\_color

```c
int conf_set(conf_t *conf, const char *section, const char *key, const char *value);
int conf_set_int(conf_t *conf, const char *section, const char *key, long value);
int conf_set_bool(conf_t *conf, const char *section, const char *key, bool value);
int conf_set_color(conf_t *conf, const char *section, const char *key, uint32_t argb);
```

set a user value. the line is changed where it is; a new key goes after
the last line of its section, and a new section goes at the end.

- `section`: a name without '\[', ']' or a line break.
- `key`: a name without '=', a line break, or a '#' or ';' or '\[' at its start.
- `value`: text without a line break.

Returns: 0, or -1 (a bad name or value, or no memory).

### conf\_unset

```c
int conf_unset(conf_t *conf, const char *section, const char *key);
```

remove the user's value, so the system value is used again.

Returns: 0 (also when there was no user value), or -1 (bad arguments).

### conf\_save

```c
int conf_save(const conf_t *conf);
```

write the user's file: to a new file, fsync, then rename over the old
one, so a crash leaves the old file or the new one. the directory is
made if it is not there.

Returns: 0, or -1 with errno set.

### conf\_user\_text

```c
char *conf_user_text(const conf_t *conf);
```

the user's file as text, as conf\_save writes it (for tests).

Returns: a string to free, or NULL (no memory).

### conf\_bad\_lines

```c
int conf_bad_lines(const conf_t *conf);
```

how many bad lines the two files had.
