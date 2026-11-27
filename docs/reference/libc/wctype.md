<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;wctype.h&gt;

Source: [`src/libc/include/wctype.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/wctype.h)

the classes of wide characters (Unicode code points).

the ASCII characters are as in &lt;ctype.h&gt;. above ASCII, ginnOS knows the
letters (and their case) of Latin-1, Latin Extended-A and B, Greek and
Cyrillic, and the letters of the large scripts (CJK, kana, Hangul)
without case; the other characters from U+00A0 are printable
punctuation. this is enough for regular expressions and terminals.

## wctype, iswctype

```c
wctype_t wctype(const char *name);
int iswctype(wint_t wc, wctype_t type);
```

a class by name ("alpha", "digit", ...), or 0

## wctrans, towctrans

```c
wctrans_t wctrans(const char *name);
wint_t towctrans(wint_t wc, wctrans_t trans);
```

a mapping by name ("tolower", "toupper"), or 0
