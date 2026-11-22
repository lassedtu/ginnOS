/**
 * @file keytext.c
 * @brief heimdalld: the text a key types, in UTF-8.
 *
 * the kernel's layouts are ASCII (the text console shows only that), so
 * the characters beyond ASCII are here, per layout: Danish æ, ø, å and €,
 * and the dead keys for accents (´ then e types é). everything else comes
 * from the kernel's tables.
 */

#include "heimdalld.h"

#include <string.h>

typedef struct
{
    uint8_t code;
    const char *normal;
    const char *shift;
} extra_key_t;

typedef struct
{
    uint8_t code; // pressed without Shift or Alt
    int accent;   // in accents[]
} dead_key_t;

typedef struct
{
    const char *mark;  // the accent alone (a dead key and then Space)
    const char *bases; // ASCII letters it goes on
    const char *const *composed;
} accent_t;

static const char *const acute_letters[] = {"á", "é", "í", "ó", "ú", "ý",
                                            "Á", "É", "Í", "Ó", "Ú", "Ý"};
static const char *const diaeresis_letters[] = {"ä", "ë", "ï", "ö", "ü", "ÿ",
                                                "Ä", "Ë", "Ï", "Ö", "Ü"};

static const accent_t accents[] = {
    {"´", "aeiouyAEIOUY", acute_letters},
    {"¨", "aeiouyAEIOU", diaeresis_letters},
};

typedef struct
{
    const char *name; // the kernel's layout name
    const extra_key_t *keys;
    int key_count;
    const dead_key_t *dead;
    int dead_count;
} layout_t;

static const extra_key_t dk_mac_keys[] = {
    {0x1A, "å", "Å"},
    {0x27, "æ", "Æ"},
    {0x28, "ø", "Ø"},
    {0x05, NULL, "€"}, // Shift+4
};

static const dead_key_t dk_mac_dead[] = {
    {0x0D, 0}, // ´
    {0x1B, 1}, // ¨
};

static const layout_t layouts[] = {
    {"dk-mac", dk_mac_keys, 4, dk_mac_dead, 2},
};

static const layout_t *layout;
static int pending = -1; // an accent from a dead key, waiting for a letter

void keytext_init(const char *layout_name)
{
    layout = NULL;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
    {
        if (strcmp(layouts[i].name, layout_name) == 0)
            layout = &layouts[i];
    }
}

/**
 * the ASCII character of the kernel's layout, as the text console does
 * it: either Alt key picks the AltGr table; with Ctrl, or with Alt and no
 * AltGr character, it types nothing.
 */
static char ascii_char(uint16_t code, uint32_t mods)
{
    if (!S.have_keymap || code >= KBD_KEYMAP_KEYS || (mods & HEIM_MOD_CTRL))
        return 0;
    bool shift = mods & HEIM_MOD_SHIFT;
    char c;
    if (mods & (HEIM_MOD_ALT | HEIM_MOD_ALTGR))
    {
        c = shift ? S.keymap.altgr_shift[code] : 0;
        if (!c)
            c = S.keymap.altgr[code];
    }
    else
        c = shift ? S.keymap.shift[code] : S.keymap.normal[code];
    return (unsigned char)c >= 0x20 && c != 0x7F ? c : 0;
}

/**
 * the text of the key without dead keys: a layout's own character, or
 * the kernel's.
 */
static void plain_text(uint16_t code, uint32_t mods, char out[8])
{
    out[0] = '\0';
    bool alt = mods & (HEIM_MOD_ALT | HEIM_MOD_ALTGR | HEIM_MOD_CTRL);
    if (layout && !alt)
    {
        for (int i = 0; i < layout->key_count; i++)
        {
            const extra_key_t *k = &layout->keys[i];
            const char *text = (mods & HEIM_MOD_SHIFT) ? k->shift : k->normal;
            if (k->code == code && text)
            {
                strcpy(out, text);
                return;
            }
        }
    }
    out[0] = ascii_char(code, mods);
    out[1] = '\0';
}

void keytext_of(uint16_t code, uint32_t mods, char out[8])
{
    bool plain = !(mods & (HEIM_MOD_SHIFT | HEIM_MOD_ALT | HEIM_MOD_ALTGR | HEIM_MOD_CTRL));
    if (layout && plain)
    {
        for (int i = 0; i < layout->dead_count; i++)
        {
            if (layout->dead[i].code == code)
            {
                pending = layout->dead[i].accent;
                out[0] = '\0';
                return;
            }
        }
    }
    plain_text(code, mods, out);
    if (pending < 0 || !out[0])
        return;

    // the accent on the letter, or alone (Space), or before the character
    const accent_t *a = &accents[pending];
    pending = -1;
    const char *base = out[1] == '\0' ? strchr(a->bases, out[0]) : NULL;
    if (base && out[0])
        strcpy(out, a->composed[base - a->bases]);
    else if (strcmp(out, " ") == 0)
        strcpy(out, a->mark);
    else if (strlen(a->mark) + strlen(out) < 8)
    {
        char rest[8];
        strcpy(rest, out);
        strcpy(out, a->mark);
        strcat(out, rest);
    }
}
