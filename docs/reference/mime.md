<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# mime

libmime: file types and their icons.

## &lt;mime.h&gt;

Source: [`src/lib/mime/include/mime.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/mime/include/mime.h)

file types: the type name and the icon of a file.

the table is /usr/share/ginnos/mime.conf (its format is described in
the file). a file is matched by its name's extension, then by its first
bytes, then as text.

### mime\_open

```c
mime_table_t *mime_open(const char *path);
```

read a table file (MIME\_CONF for the system one).

Returns: the table, or NULL (no file). bad lines are skipped.

### mime\_from\_text

```c
mime_table_t *mime_from_text(const char *text);
```

a table from text in memory (for tests).

### mime\_lookup

```c
const mime_type_t *mime_lookup(const mime_table_t *table, const char *name,
                               const uint8_t *head, size_t head_len);
```

the type of a file from its name and its first bytes.

- `name`: the file name (a path is fine), or NULL.
- `head`: the first bytes of the file, or NULL.

Returns: the type; never NULL (application/octet-stream when nothing matches). it stays valid until the table is closed.

### mime\_lookup\_file

```c
const mime_type_t *mime_lookup_file(const mime_table_t *table, const char *path);
```

the type of a file on disk: inode/directory for a directory, else by
its name and first bytes.
