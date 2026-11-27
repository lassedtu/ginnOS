<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;langinfo.h&gt;

Source: [`src/libc/include/langinfo.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/langinfo.h)

facts about the locale. ginnOS has the "C" locale with UTF-8
       text, so the answers are fixed.

## nl\_langinfo

```c
char *nl_langinfo(nl_item item);
```

Returns: the text for an item, or "" for an item it does not know.
