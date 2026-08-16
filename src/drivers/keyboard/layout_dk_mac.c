/**
 * @file layout_dk_mac.c
 * @brief Danish Mac keyboard layout for PS/2 keyboards.
 */

#include "keyboard_layout.h"
#include "../../common/memory.h"

// PS/2 scan code set 1 key positions:
// 0x01=Esc  0x02=1  0x03=2  0x04=3  0x05=4  0x06=5  0x07=6  0x08=7
// 0x09=8  0x0A=9  0x0B=0  0x0C=+  0x0D=acute  0x0E=BS  0x0F=Tab
// 0x10=Q  0x11=W  0x12=E  0x13=R  0x14=T  0x15=Y  0x16=U  0x17=I
// 0x18=O  0x19=P  0x1A=Å  0x1B=diaeresis  0x1C=Enter  0x1D=LCtrl
// 0x1E=A  0x1F=S  0x20=D  0x21=F  0x22=G  0x23=H  0x24=J  0x25=K
// 0x26=L  0x27=Æ  0x28=Ø  0x29=TLDE(§/$)  0x2A=LShift
// 0x2B=BKSL(')  0x2C=Z  0x2D=X  0x2E=C  0x2F=V
// 0x30=B  0x31=N  0x32=M  0x33=,  0x34=.  0x35=-  0x36=RShift
// 0x37=KP*  0x38=LAlt  0x39=Space  0x3A=CapsLock
// 0x56=LSGT (ISO key between LShift and Z)

static keyboard_layout_t layout_dk_mac;
static int layout_initialized = 0;

static void init_layout(void)
{
    if (layout_initialized)
        return;
    layout_initialized = 1;

    layout_dk_mac.name = "dk-mac";

    memset((void *)layout_dk_mac.normal, 0, 128);
    memset((void *)layout_dk_mac.shift, 0, 128);
    memset((void *)layout_dk_mac.altgr, 0, 128);
    memset((void *)layout_dk_mac.altgr_shift, 0, 128);

    // === Normal map ===
    char *n = (char *)layout_dk_mac.normal;
    n[0x01] = 27; // Escape
    n[0x02] = '1';
    n[0x03] = '2';
    n[0x04] = '3';
    n[0x05] = '4';
    n[0x06] = '5';
    n[0x07] = '6';
    n[0x08] = '7';
    n[0x09] = '8';
    n[0x0A] = '9';
    n[0x0B] = '0';
    n[0x0C] = '+';
    // 0x0D = dead acute (no output)
    n[0x0E] = '\b'; // Backspace
    n[0x0F] = '\t'; // Tab
    n[0x10] = 'q';
    n[0x11] = 'w';
    n[0x12] = 'e';
    n[0x13] = 'r';
    n[0x14] = 't';
    n[0x15] = 'y';
    n[0x16] = 'u';
    n[0x17] = 'i';
    n[0x18] = 'o';
    n[0x19] = 'p';
    // 0x1A = å (non-ASCII, skip for now)
    // 0x1B = diaeresis (dead key, skip)
    n[0x1C] = '\n'; // Enter
    // 0x1D = Left Ctrl (modifier, not a char)
    n[0x1E] = 'a';
    n[0x1F] = 's';
    n[0x20] = 'd';
    n[0x21] = 'f';
    n[0x22] = 'g';
    n[0x23] = 'h';
    n[0x24] = 'j';
    n[0x25] = 'k';
    n[0x26] = 'l';
    // 0x27 = æ (non-ASCII, skip)
    // 0x28 = ø (non-ASCII, skip)
    n[0x29] = '<'; // ISO key (LSGT) — QEMU on macOS sends this as 0x29
    // 0x2A = Left Shift (modifier)
    n[0x2B] = '\''; // BKSL key (apostrophe)
    n[0x2C] = 'z';
    n[0x2D] = 'x';
    n[0x2E] = 'c';
    n[0x2F] = 'v';
    n[0x30] = 'b';
    n[0x31] = 'n';
    n[0x32] = 'm';
    n[0x33] = ',';
    n[0x34] = '.';
    n[0x35] = '-';
    // 0x37 = Keypad *
    n[0x39] = ' '; // Spacebar
    n[0x56] = '$'; // TLDE key (§/$ on Danish Mac) — QEMU macOS may send this as 0x56

    // === Shift map ===
    char *s = (char *)layout_dk_mac.shift;
    s[0x01] = 27;
    s[0x02] = '!';
    s[0x03] = '"';
    s[0x04] = '#';
    s[0x05] = '$'; // Shift+4 (€ on real DK Mac, but no Unicode support)
    s[0x06] = '%';
    s[0x07] = '&';
    s[0x08] = '/';
    s[0x09] = '(';
    s[0x0A] = ')';
    s[0x0B] = '=';
    s[0x0C] = '?';
    s[0x0D] = '`';
    s[0x0E] = '\b';
    s[0x0F] = '\t';
    s[0x10] = 'Q';
    s[0x11] = 'W';
    s[0x12] = 'E';
    s[0x13] = 'R';
    s[0x14] = 'T';
    s[0x15] = 'Y';
    s[0x16] = 'U';
    s[0x17] = 'I';
    s[0x18] = 'O';
    s[0x19] = 'P';
    // 0x1A = Å, 0x1B = ^
    s[0x1B] = '^';
    s[0x1C] = '\n';
    s[0x1E] = 'A';
    s[0x1F] = 'S';
    s[0x20] = 'D';
    s[0x21] = 'F';
    s[0x22] = 'G';
    s[0x23] = 'H';
    s[0x24] = 'J';
    s[0x25] = 'K';
    s[0x26] = 'L';
    // 0x27 = Æ, 0x28 = Ø (non-ASCII)
    s[0x29] = '>'; // ISO key shifted — QEMU on macOS sends this as 0x29
    s[0x2B] = '*'; // BKSL shifted
    s[0x2C] = 'Z';
    s[0x2D] = 'X';
    s[0x2E] = 'C';
    s[0x2F] = 'V';
    s[0x30] = 'B';
    s[0x31] = 'N';
    s[0x32] = 'M';
    s[0x33] = ';';
    s[0x34] = ':';
    s[0x35] = '_';
    s[0x39] = ' ';
    s[0x56] = '#'; // TLDE shifted

    // === AltGr (Option) map ===
    char *a = (char *)layout_dk_mac.altgr;
    a[0x03] = '@';  // AltGr+2 = @  (standard DK Mac)
    a[0x05] = '$';  // AltGr+4 = $ (or €, but no Unicode)
    a[0x08] = '\\'; // AltGr+7 = backslash
    a[0x09] = '[';  // AltGr+8 = [
    a[0x0A] = ']';  // AltGr+9 = ]
    a[0x0D] = '|';  // AltGr+acute = | (from dk basic)
    a[0x17] = '|';  // AltGr+I = | (dk mac specific)
    a[0x1B] = '~';  // AltGr+diaeresis = ~
    a[0x2B] = '@';  // AltGr+BKSL = @ (dk mac: apostrophe key)
    a[0x29] = '\\'; // AltGr+ISO key = backslash (QEMU sends 0x29)

    // === AltGr+Shift map ===
    char *as = (char *)layout_dk_mac.altgr_shift;
    as[0x09] = '{'; // AltGr+Shift+8 = {
    as[0x0A] = '}'; // AltGr+Shift+9 = }
}

const keyboard_layout_t *keyboard_get_layout(void)
{
    init_layout();
    return &layout_dk_mac;
}
