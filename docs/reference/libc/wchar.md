<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;wchar.h&gt;

Source: [`src/libc/include/wchar.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/wchar.h)

wide characters. ginnOS has one multibyte encoding, UTF-8: a
       wchar\_t is a Unicode code point.

## mbstate\_t

```c
typedef struct
{
    unsigned int pending; // bytes of a character still to come (not used)
} mbstate_t;
```

the state of a conversion that stopped in a character (UTF-8 needs none)

## mbrtowc

```c
size_t mbrtowc(wchar_t *wc, const char *s, size_t n, mbstate_t *state);
```

the character at s, of at most n bytes, into \*wc (if wc is not NULL).

Returns: the bytes it used, 0 for the null character, (size\_t)-1 for bytes that are not UTF-8 (errno EILSEQ), (size\_t)-2 when n stops in the character.

## mbrlen

```c
size_t mbrlen(const char *s, size_t n, mbstate_t *state);
```

as mbrtowc with s as a whole string: (size\_t)-1 or the length

## wcrtomb

```c
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *state);
```

wc as UTF-8 at s (MB\_CUR\_MAX bytes of room): the bytes, or (size\_t)-1

## mbsinit

```c
int mbsinit(const mbstate_t *state);
```

always 1: no conversion stops in a character

## btowc

```c
wint_t btowc(int c);
```

a character of one byte as a wide character, or WEOF for a byte from 0x80

## wctob

```c
int wctob(wint_t wc);
```

a wide character of one byte as that byte, else EOF

## wcwidth

```c
int wcwidth(wchar_t wc);
```

the columns a character takes in a terminal: 0, 1 or 2; -1 if it cannot be shown
