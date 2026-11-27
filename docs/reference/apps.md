<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# apps

libapps: the .app files of the launcher.

## &lt;apps.h&gt;

Source: [`src/lib/apps/include/apps.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/apps/include/apps.h)

the programs of the desktop: the .app files.

each program the launcher shows has a file /usr/share/applications/
&lt;id&gt;.app in the settings format of libconf:

```c
[app]
name = Window demo
comment = A test of the window server
exec = heimdemo 2
icon = application-x-executable
category = Development
```

name and exec must be there. icon is a Tela icon name (the icon library
gives "unknown" for a name it does not have). category is one of
APP\_CATEGORIES; another or no category is "Other". hidden = true keeps
the program out of the launcher (it can still be found by its id).

exec is split into words at spaces. a word in double quotes can have
spaces in it. the first word is the program: a path, or a name in /bin.

### apps\_load

```c
app_list_t *apps_load(const char *dir);
```

read the .app files of a directory (APPS\_DIR for the system ones). a
file without a name or exec is skipped, with a warning on stderr.

Returns: the list (empty if there is no directory), or NULL (no memory).

### apps\_find

```c
const app_t *apps_find(const app_list_t *list, const char *id);
```

the program with this id.

Returns: it, or NULL.

### app\_split\_exec

```c
int app_split_exec(const char *exec, char *buffer, char *argv[APP_ARGS_MAX + 1]);
```

split exec into words.

- `buffer`: receives the words (it must hold strlen(exec) + 1 bytes).
- `argv`: receives up to APP\_ARGS\_MAX words and a NULL.

Returns: the number of words, or -1 (none, too many, or a quote that is not closed).
