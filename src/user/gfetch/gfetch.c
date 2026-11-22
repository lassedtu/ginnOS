/**
 * @file gfetch.c
 * @brief the system at a glance: Ginnungagap, and the facts next to it.
 *
 * usage: gfetch [-n]
 *
 *   -n  no colours (also when the output is not a terminal)
 *
 * the facts come from uname, /proc, the framebuffer, the keyboard and
 * the CPU itself. a terminal narrower than the logo and the facts gets
 * the facts under the logo.
 */

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <sys/kbd.h>
#include <sys/utsname.h>
#include <unistd.h>

#define FACTS_MAX 20
#define FACT_MAX  96

// Ginnungagap, the void before the worlds: the ice of Niflheim and the
// fire of Muspelheim swirl out of one core, as the arms of a galaxy
static const char *const logo[] = {
    "          ..:sssssss:.          ss:",
    "       :sgggggggggggggggs.       .gs",
    "    .sgggs:.        .:sgggs:       ss",
    "   sggs.                :gggs.      gs",
    " .ggs       .sssgsss.     sggg.     :g",
    ".gg:     .sggggggggggg:    gggg      g",
    "gg.     sgggggssssgggggs   sggg:     g",
    "g:     sgggg:  :sggggggg.  sggg:     g",
    "g     .gggg   sggggggggs   gggg.     g",
    "g     :gggs  .gggggggs:  :ggggs     :g",
    "g     :gggs   sgggggssssgggggs     .gg",
    "g      gggg    :gggggggggggs.     :gg.",
    "g:     .gggs     .sssgsss.       sgg.",
    "sg      .sggg:                .sggs",
    " ss       :sgggs:.        .:sgggs.",
    "  sg.       .sgggggggggggggggs:",
    "   :ss          .:sssssss:..",
};

// the colour of each character of the logo: 0 the core; a to d the ice
// arm from the core out, w to z the fire arm
static const char *const logo_colors[] = {
    "          yyyyyyyyyyyy          ddd",
    "       yyyyyyyyyyyyyyyyyyy       ddd",
    "    yyyyyyyy        yyyyyxxx       dd",
    "   yyyyy                xxxxxx      dd",
    " yyyy       baaaaaaaa     xxxxx     dd",
    "yyyy     bbbbaaaaaaaaaa    xxxx      d",
    "yyy     bbbbbbaaaaaaaaaa   xxxxx     d",
    "yy     bbbbbb  wwwwaaaaaa  xxxxx     d",
    "z     bbbbb   wwww00aaaa   xxxxx     d",
    "z     bbbbb  wwwwwaaaaa  xxxxxx     cc",
    "z     bbbbb   wwwwwwwwwwxxxxxx     ccc",
    "z      bbbb    wwwwwwwwwwxxxx     cccc",
    "zz     bbbbb     wwwwwwwwx       cccc",
    "zz      bbbbbb                ccccc",
    " zz       bbbccccc        cccccccc",
    "  zzz       ccccccccccccccccccc",
    "   zzz          cccccccccccc",
};
#define LOGO_LINES (int)(sizeof(logo) / sizeof(logo[0]))
#define LOGO_WIDTH 38

static bool color = true;

static struct
{
    char label[16];
    char value[FACT_MAX];
} facts[FACTS_MAX];
static int fact_count;
static char title[FACT_MAX]; // user@host

static void fact(const char *label, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void fact(const char *label, const char *fmt, ...)
{
    if (fact_count == FACTS_MAX)
        return;
    snprintf(facts[fact_count].label, sizeof(facts[0].label), "%s", label);
    va_list args;
    va_start(args, fmt);
    vsnprintf(facts[fact_count].value, FACT_MAX, fmt, args);
    va_end(args);
    fact_count++;
}

static const char *ansi(const char *code)
{
    return color ? code : "";
}

/**
 * the first line of a file that starts with key, without the key and the
 * white space after it.
 */
static bool read_field(const char *path, const char *key, char *out, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[256];
    bool found = false;
    size_t n = strlen(key);
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, key, n) != 0)
            continue;
        const char *v = line + n;
        while (*v == ' ' || *v == '\t' || *v == ':')
            v++;
        snprintf(out, size, "%s", v);
        out[strcspn(out, "\n")] = '\0';
        found = true;
        break;
    }
    fclose(f);
    return found;
}

/** the name of a process, from /proc/<pid>/status */
static bool process_name(int pid, char *out, size_t size)
{
    char path[48];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    return read_field(path, "Name", out, size);
}

/** true if a process of this name runs */
static bool running(const char *name)
{
    DIR *dir = opendir("/proc");
    if (!dir)
        return false;
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(dir)))
    {
        int pid = atoi(e->d_name);
        char got[64];
        if (pid > 0 && process_name(pid, got, sizeof(got)))
            found = strcmp(got, name) == 0;
    }
    closedir(dir);
    return found;
}

static void cpu_name(char *out, size_t size)
{
    uint32_t r[12];
    uint32_t top;
    __asm__ volatile("cpuid" : "=a"(top), "=b"(r[0]), "=c"(r[1]), "=d"(r[2]) : "a"(0x80000000u),
                     "c"(0));
    if (top < 0x80000004u)
    {
        snprintf(out, size, "unknown");
        return;
    }
    for (uint32_t i = 0; i < 3; i++)
        __asm__ volatile("cpuid"
                         : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]),
                           "=d"(r[i * 4 + 3])
                         : "a"(0x80000002u + i), "c"(0));
    char brand[49];
    memcpy(brand, r, 48);
    brand[48] = '\0';
    // the brand string is padded with spaces at the front, and has runs of them
    const char *s = brand;
    while (*s == ' ')
        s++;
    size_t n = 0;
    for (; *s && n + 1 < size; s++)
        if (!(*s == ' ' && (n == 0 || out[n - 1] == ' ')))
            out[n++] = *s;
    while (n && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
}

/** a name for the display card: by its PCI vendor and device */
static void gpu_name(char *out, size_t size)
{
    FILE *f = fopen("/proc/pci", "r");
    snprintf(out, size, "none");
    if (!f)
        return;
    char line[160];
    while (fgets(line, sizeof(line), f))
    {
        unsigned cls, vendor, device;
        if (sscanf(line, "%*s %x %x:%x", &cls, &vendor, &device) != 3 || (cls >> 16) != 0x03)
            continue;
        const char *name = NULL;
        if (vendor == 0x1AF4)
            name = "virtio-gpu";
        else if (vendor == 0x1234 && device == 0x1111)
            name = "QEMU standard VGA";
        else if (vendor == 0x8086)
            name = "Intel graphics";
        else if (vendor == 0x1002)
            name = "AMD Radeon";
        else if (vendor == 0x10DE)
            name = "NVIDIA";
        else if (vendor == 0x15AD)
            name = "VMware SVGA";
        if (name)
            snprintf(out, size, "%s (%04x:%04x)", name, vendor, device);
        else
            snprintf(out, size, "%04x:%04x", vendor, device);
        break;
    }
    fclose(f);
}

static void uptime_text(char *out, size_t size)
{
    char text[32];
    if (!read_field("/proc/uptime", "", text, sizeof(text)))
    {
        snprintf(out, size, "unknown");
        return;
    }
    long seconds = atol(text);
    long days = seconds / 86400;
    long hours = seconds / 3600 % 24;
    long mins = seconds / 60 % 60;
    size_t n = 0;
    out[0] = '\0';
    if (days)
        n += (size_t)snprintf(out + n, size - n, "%ld day%s, ", days, days == 1 ? "" : "s");
    if (days || hours)
        n += (size_t)snprintf(out + n, size - n, "%ld hour%s, ", hours, hours == 1 ? "" : "s");
    snprintf(out + n, size - n, "%ld min%s", mins, mins == 1 ? "" : "s");
}

static void gather(void)
{
    struct utsname u;
    bool have_uname = uname(&u) == 0;
    struct passwd *user = getpwuid(geteuid());
    snprintf(title, sizeof(title), "%s@%s", user ? user->pw_name : "user",
             have_uname ? u.nodename : "ginnos");

    if (have_uname)
    {
        fact("OS", "%s %s", u.sysname, u.machine);
        fact("Kernel", "%s (%s)", u.release, u.version);
    }

    char text[FACT_MAX];
    uptime_text(text, sizeof(text));
    fact("Uptime", "%s", text);

    if (process_name(getppid(), text, sizeof(text)))
        fact("Shell", "%s", text);

    if (running("asgard"))
        fact("Desktop", "asgard on heimdalld");
    else if (running("heimdalld"))
        fact("Desktop", "heimdalld");
    else
        fact("Desktop", "none (the text console)");

    const char *term = getenv("TERM_PROGRAM");
    fact("Terminal", "%s", term ? term : "the text console");

    int fb = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    fb_var_screeninfo_t var;
    if (fb >= 0 && ioctl(fb, FBIOGET_VSCREENINFO, &var) == 0)
        fact("Display", "%ux%u, %u bits", var.xres, var.yres, var.bits_per_pixel);
    if (fb >= 0)
        close(fb);

    int kbd = open("/dev/kbd", O_RDONLY | O_CLOEXEC);
    struct kbd_layout_name layout = {0};
    if (kbd >= 0 && ioctl(kbd, KBD_IOC_GET_LAYOUT, &layout.index) == 0 &&
        ioctl(kbd, KBD_IOC_LAYOUT_NAME, &layout) == 0)
        fact("Keyboard", "%s", layout.name);
    if (kbd >= 0)
        close(kbd);

    cpu_name(text, sizeof(text));
    fact("CPU", "%s", text);
    gpu_name(text, sizeof(text));
    fact("GPU", "%s", text);

    char total[32], free_kb[32];
    if (read_field("/proc/meminfo", "MemTotal", total, sizeof(total)) &&
        read_field("/proc/meminfo", "MemFree", free_kb, sizeof(free_kb)))
    {
        long t = atol(total) / 1024;
        long f = atol(free_kb) / 1024;
        fact("Memory", "%ld MiB / %ld MiB", t - f, t);
    }

    if (read_field("/proc/audio", "Device", text, sizeof(text)))
        fact("Sound", "%s", text);

    DIR *bin = opendir("/bin");
    if (bin)
    {
        int count = 0;
        struct dirent *e;
        while ((e = readdir(bin)))
            if (e->d_name[0] != '.')
                count++;
        closedir(bin);
        fact("Programs", "%d in /bin", count);
    }
}

/** the colour of a letter of logo_colors */
static const char *logo_color(char tag)
{
    switch (tag)
    {
    case '0':
        return "\033[1;97m"; // the core: bright white
    case 'a':
        return "\033[97m"; // the ice: white, then cyan, then blue
    case 'b':
        return "\033[96m";
    case 'c':
        return "\033[36m";
    case 'd':
        return "\033[34m";
    case 'w':
        return "\033[93m"; // the fire: yellow, then red
    case 'x':
        return "\033[33m";
    case 'y':
        return "\033[91m";
    default:
        return "\033[31m";
    }
}

/**
 * a line of the logo in its colours: each arm goes from light at the core
 * to dark at its end.
 */
static void print_logo_line(int i)
{
    const char *line = i < LOGO_LINES ? logo[i] : "";
    const char *map = i < LOGO_LINES ? logo_colors[i] : "";
    const char *now = "";
    int width = 0;
    for (int x = 0; line[x]; x++, width++)
    {
        const char *want = logo_color(map[x]);
        if (line[x] != ' ' && strcmp(want, now) != 0)
        {
            fputs(ansi(want), stdout);
            now = want;
        }
        putchar(line[x]);
    }
    fputs(ansi("\033[0m"), stdout);
    for (; width < LOGO_WIDTH; width++)
        putchar(' ');
}

static int value_width = FACT_MAX; // the columns a value can have

/** line i of the facts: the title, a rule, the facts, the colours */
static void print_fact_line(int i)
{
    if (i == 0)
    {
        const char *at = strchr(title, '@');
        int user = at ? (int)(at - title) : (int)strlen(title);
        // the user in fire, the host in ice
        printf("%s%.*s%s@%s%s%s", ansi("\033[1;31m"), user, title, ansi("\033[0m"),
               ansi("\033[1;36m"), at ? at + 1 : "", ansi("\033[0m"));
        return;
    }
    if (i == 1)
    {
        for (size_t n = 0; n < strlen(title); n++)
            putchar('-');
        return;
    }
    int f = i - 2;
    if (f < fact_count)
    {
        // a value too long for the terminal ends with "..."
        const char *v = facts[f].value;
        int length = (int)strlen(v);
        bool cut = length > value_width;
        printf("%s%-9s%s %.*s%s", ansi("\033[1;36m"), facts[f].label, ansi("\033[0m"),
               cut ? value_width - 3 : length, v, cut ? "..." : "");
        return;
    }
    // a line free, then the 16 colours in two rows
    int row = f - fact_count - 1;
    if (color && (row == 0 || row == 1))
    {
        for (int c = 0; c < 8; c++)
            printf("\033[%dm   ", row == 0 ? 40 + c : 100 + c);
        printf("\033[0m");
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0)
            color = false;
        else
        {
            fprintf(stderr, "usage: gfetch [-n]\n");
            return 1;
        }
    }
    if (!isatty(1))
        color = false;
    gather();

    int fact_lines = 2 + fact_count + (color ? 3 : 0);
    struct winsize size;
    int columns = ioctl(1, TIOCGWINSZ, &size) == 0 && size.ws_col ? size.ws_col : 80;
    // the facts beside the logo when there is room for most values (a
    // line must not reach the last column: the console wraps there)
    int beside = columns - 1 - 2 - LOGO_WIDTH - 3 - 10;
    bool narrow = beside < 24;
    value_width = narrow ? columns - 1 - 2 - 10 : beside;
    if (value_width < 8)
        value_width = 8;

    putchar('\n');
    if (narrow)
    {
        // narrow: the logo, then the facts under it
        for (int i = 0; i < LOGO_LINES; i++)
        {
            printf("  ");
            print_logo_line(i);
            putchar('\n');
        }
        putchar('\n');
        for (int i = 0; i < fact_lines; i++)
        {
            printf("  ");
            print_fact_line(i);
            putchar('\n');
        }
    }
    else
    {
        int lines = LOGO_LINES > fact_lines ? LOGO_LINES : fact_lines;
        for (int i = 0; i < lines; i++)
        {
            printf("  ");
            print_logo_line(i);
            printf("   ");
            print_fact_line(i);
            putchar('\n');
        }
    }
    putchar('\n');
    return 0;
}
