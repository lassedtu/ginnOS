/**
 * @file libctest.c
 * @brief Test the C library: formatting, parsing, strings, sorting,
 * streams, memory, the environment, non-local jumps and math.
 *
 * prints one line per failed check and "libctest: N checks, ok" (or the
 * number of failures) at the end. wide.c tests the wide characters,
 * regex.c the regular expressions,
 * posix.c the POSIX parts the Unix tools need,
 * files.c the *at calls.
 */

#include "libctest.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int checks;
int failures;

static char text[256];

/**
 * format into text and compare.
 */
#define FORMATS(expected, ...)                                             \
    do                                                                     \
    {                                                                      \
        snprintf(text, sizeof(text), __VA_ARGS__);                         \
        checks++;                                                          \
        if (strcmp(text, expected) != 0)                                   \
        {                                                                  \
            failures++;                                                    \
            printf("libctest: line %d: \"%s\" not \"%s\"\n", __LINE__, text, expected); \
        }                                                                  \
    } while (0)

static void test_printf(void)
{
    FORMATS("42", "%d", 42);
    FORMATS("-7", "%i", -7);
    FORMATS("  42", "%4d", 42);
    FORMATS("42  |", "%-4d|", 42);
    FORMATS("0042", "%04d", 42);
    FORMATS("+42", "%+d", 42);
    FORMATS(" 42", "% d", 42);
    FORMATS("00042", "%.5d", 42);
    FORMATS("ff FF 0xff", "%x %X %#x", 255, 255, 255);
    FORMATS("17 017", "%o %#o", 15, 15);
    FORMATS("4294967295", "%u", 4294967295u);
    FORMATS("-2147483648", "%d", (int)0x80000000);
    FORMATS("123456789012", "%lld", 123456789012LL);
    FORMATS("18446744073709551615", "%llu", 18446744073709551615ULL);
    FORMATS("-9223372036854775808", "%lld", (long long)(1ULL << 63));
    FORMATS("ab   |  abc|ab", "%-5s|%5s|%.2s", "ab", "abc", "abc");
    FORMATS("x", "%c", 'x');
    FORMATS("%", "%%");
    FORMATS("0x1000", "%p", (void *)0x1000);
    FORMATS("12", "%*d", 2, 12);
    FORMATS("3.141593", "%f", 3.14159265);
    FORMATS("3.14", "%.2f", 3.14159265);
    FORMATS("-0.50", "%.2f", -0.5);
    FORMATS("1.000000e+03", "%e", 1000.0);
    FORMATS("0.0001", "%g", 0.0001);
    FORMATS("1e+06", "%g", 1000000.0);
    FORMATS("2.5", "%g", 2.5);
    FORMATS("1234567890123.0", "%.1f", 1234567890123.0);
    FORMATS("42 " PRIu64, "%d %s", 42, PRIu64);

    int written = snprintf(text, 4, "%s", "abcdef");
    CHECK(written == 6 && strcmp(text, "abc") == 0);
}

static void test_scan(void)
{
    int a;
    unsigned int b;
    char word[16];
    long long big;
    CHECK(sscanf("12 0x1f word", "%d %x %s", &a, &b, word) == 3);
    CHECK(a == 12 && b == 31 && strcmp(word, "word") == 0);
    CHECK(sscanf("-5,9", "%d,%u", &a, &b) == 2 && a == -5 && b == 9);
    CHECK(sscanf("123456789012", "%lld", &big) == 1 && big == 123456789012LL);
    CHECK(sscanf("key=value", "%[^=]=%s", word, text) == 2 && strcmp(word, "key") == 0);
    CHECK(sscanf("", "%d", &a) == EOF);

    char *end;
    CHECK(strtol("  -123abc", &end, 10) == -123 && strcmp(end, "abc") == 0);
    CHECK(strtol("0x7f", NULL, 0) == 127 && strtol("017", NULL, 0) == 15);
    CHECK(strtoul("4294967295", NULL, 10) == 4294967295ul);
    errno = 0;
    CHECK(strtol("99999999999", NULL, 10) == 2147483647L && errno == ERANGE);
    CHECK(strtoll("-9223372036854775808", NULL, 10) == (long long)(1ULL << 63));
    CHECK(strtol("zz", &end, 10) == 0 && strcmp(end, "zz") == 0);
    CHECK(atol("77") == 77 && abs(-3) == 3 && div(7, 2).quot == 3 && div(7, 2).rem == 1);
}

static void test_strings(void)
{
    char buf[32] = "hello";
    CHECK(strlen(buf) == 5 && strnlen(buf, 3) == 3);
    strncat(buf, " world!!", 6);
    CHECK(strcmp(buf, "hello world") == 0);
    CHECK(strstr(buf, "wor") == buf + 6 && strstr(buf, "xyz") == NULL);
    CHECK(strchr(buf, 'o') == buf + 4 && strrchr(buf, 'o') == buf + 7);
    CHECK(strspn("aabbc", "ab") == 4 && strcspn("abc,de", ",") == 3);
    CHECK(strpbrk("abc", "xc") != NULL && memchr("abc", 'c', 3) != NULL);
    CHECK(strcasecmp("Hello", "hELLO") == 0 && strncasecmp("abX", "ABy", 2) == 0);

    char *copy = strdup("copy");
    CHECK(copy && strcmp(copy, "copy") == 0);
    free(copy);
    copy = strndup("truncate", 5);
    CHECK(copy && strcmp(copy, "trunc") == 0);
    free(copy);

    char list[] = "a,b,,c";
    char *save;
    char *first = strtok_r(list, ",", &save);
    char *second = strtok_r(NULL, ",", &save);
    char *third = strtok_r(NULL, ",", &save);
    CHECK(first && second && third && strcmp(third, "c") == 0 && !strtok_r(NULL, ",", &save));

    CHECK(isdigit('5') && !isdigit('a') && isalpha('Z') && isspace('\t') && isxdigit('F'));
    CHECK(toupper('a') == 'A' && tolower('Q') == 'q' && ispunct('!') && !isprint('\n'));
    CHECK(strcmp(strerror(ENOENT), "no such file or directory") == 0);
}

static int compare_ints(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static void test_sort(void)
{
    int values[50];
    srand(7);
    for (int i = 0; i < 50; i++)
        values[i] = rand() % 1000;
    qsort(values, 50, sizeof(int), compare_ints);
    int sorted = 1;
    for (int i = 1; i < 50; i++)
        sorted &= values[i - 1] <= values[i];
    CHECK(sorted);

    int key = values[17];
    int *found = bsearch(&key, values, 50, sizeof(int), compare_ints);
    CHECK(found && *found == key);
    key = 5000;
    CHECK(bsearch(&key, values, 50, sizeof(int), compare_ints) == NULL);
}

static void test_streams(void)
{
    const char *path = "/tmp/libctest.txt";
    FILE *out = fopen(path, "w");
    CHECK(out != NULL);
    if (!out)
        return;
    for (int i = 0; i < 100; i++)
        fprintf(out, "line %d\n", i);
    fputs("end\n", out);
    CHECK(fclose(out) == 0);

    FILE *in = fopen(path, "r");
    CHECK(in != NULL);
    if (!in)
        return;
    char line[32];
    int count = 0;
    while (fgets(line, sizeof(line), in))
        count++;
    CHECK(count == 101 && strcmp(line, "end\n") == 0 && feof(in));

    CHECK(fseek(in, 5, SEEK_SET) == 0 && fgetc(in) == '0' && ftell(in) == 6);
    CHECK(ungetc('X', in) == 'X' && fgetc(in) == 'X');
    rewind(in);
    char bytes[4];
    CHECK(fread(bytes, 1, 4, in) == 4 && memcmp(bytes, "line", 4) == 0);
    fclose(in);

    FILE *append = fopen(path, "a+");
    fprintf(append, "more\n");
    fclose(append);
    in = fopen(path, "r");
    fseek(in, -5, SEEK_END);
    CHECK(fgets(line, sizeof(line), in) && strcmp(line, "more\n") == 0);
    fclose(in);
    remove(path);
    CHECK(fopen(path, "r") == NULL && errno == ENOENT);
}

static void test_memory(void)
{
    // many small blocks
    char *blocks[100];
    for (int i = 0; i < 100; i++)
    {
        blocks[i] = malloc((size_t)(i * 7 + 1));
        if (blocks[i])
            memset(blocks[i], i, (size_t)(i * 7 + 1));
    }
    int intact = 1;
    for (int i = 0; i < 100; i++)
        intact &= blocks[i] && blocks[i][i * 7] == (char)i;
    CHECK(intact);
    for (int i = 0; i < 100; i += 2)
        free(blocks[i]);
    for (int i = 1; i < 100; i += 2)
        free(blocks[i]);

    // a large block gets its own mapping; writing all of it works
    size_t big_size = 1024 * 1024;
    char *big = malloc(big_size);
    CHECK(big != NULL);
    if (big)
    {
        memset(big, 0x5a, big_size);
        CHECK(big[0] == 0x5a && big[big_size - 1] == 0x5a);
        big = realloc(big, big_size * 2);
        CHECK(big && big[big_size - 1] == 0x5a);
        free(big);
    }

    int *zeros = calloc(100, sizeof(int));
    int all_zero = zeros != NULL;
    for (int i = 0; zeros && i < 100; i++)
        all_zero &= zeros[i] == 0;
    CHECK(all_zero);
    free(zeros);
}

static jmp_buf jump;

static void jump_back(int value)
{
    longjmp(jump, value);
}

static void test_control(void)
{
    volatile int steps = 0;
    int result = setjmp(jump);
    steps++;
    if (result == 0)
        jump_back(5);
    CHECK(result == 5 && steps == 2);

    CHECK(setenv("LIBCTEST", "one", 1) == 0 && strcmp(getenv("LIBCTEST"), "one") == 0);
    CHECK(setenv("LIBCTEST", "two", 0) == 0 && strcmp(getenv("LIBCTEST"), "one") == 0);
    CHECK(setenv("LIBCTEST", "three", 1) == 0 && strcmp(getenv("LIBCTEST"), "three") == 0);
    CHECK(unsetenv("LIBCTEST") == 0 && getenv("LIBCTEST") == NULL);

    time_t epoch = 951782400; // 2000-02-29 00:00:00
    struct tm tm;
    gmtime_r(&epoch, &tm);
    CHECK(tm.tm_year == 100 && tm.tm_mon == 1 && tm.tm_mday == 29 && tm.tm_wday == 2);
    CHECK(timegm(&tm) == epoch);
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %a", &tm);
    CHECK(strcmp(text, "2000-02-29 00:00:00 Tue") == 0);

    assert(checks > 0);
}

static void test_math(void)
{
    // volatile: the compiler may not work these out at build time
    volatile double two = 2.0, ten = 10.0, three = 3.0, e = M_E;
    CHECK(pow(two, ten) == 1024.0 && pow(ten, -two) == 0.01 && pow(-two, three) == -8.0);
    CHECK(pow(three, 40.0) == 12157665459056928801.0 && pow(two, 0.5) == M_SQRT2);
    CHECK(isnan(pow(-two, 0.5)) && pow(0.0, -two) == HUGE_VAL && pow(1.0, NAN) == 1.0);
    CHECK(exp(1.0 + two - two) == M_E && exp(two - two) == 1.0);
    CHECK(exp(1000.0 + two) == HUGE_VAL && exp(-1000.0 - two) == 0.0);
    CHECK(log(e) == 1.0 && log(two - 1.0) == 0.0);
    CHECK(log10(ten * ten * ten) == 3.0 && log2(1024.0 + two - two) == 10.0);

    // printf gives the exact digits, rounded correctly
    FORMATS("0.1000000000000000055511151231257827", "%.34f", 0.1);
    FORMATS("1.7976931348623157e+308", "%.16e", 1.7976931348623157e308);
    FORMATS("123456789012345677877719597056", "%.0f", 1.2345678901234568e29);
    FORMATS("2.5e-05 100000 1.00 -0.00e+00", "%.3g %g %#.3g %+.2e", 0.000025, 100000.0, 1.0,
            -0.0);
    FORMATS("2.50000000000000000000 -00003.142 2.2     | 0.2 0.3", "%.20f %010.3f %-8.1f| %.1f %.1f",
            2.5, -3.14159, 2.25, 0.25, 0.35);
    FORMATS("0x1.8p+1 0x1p-1074 0x1.999999999999ap-4 0X1.8P+1", "%a %a %a %A", 3.0,
            4.9406564584124654e-324, 0.1, 3.0);
    FORMATS("  -inf nan INF", "%6f %f %F", -(double)INFINITY, (double)NAN, (double)INFINITY);
    FORMATS("0.125 0.3333333333", "%Lg %.10Lf", 0.125L, 1.0L / 3);

    // strtod rounds correctly, and reads hexadecimal numbers, inf and nan
    char *end;
    CHECK(strtod("1.25", &end) == 1.25 && *end == '\0' && strtod("0.1", NULL) == 0.1);
    CHECK(strtod("2.2250738585072011e-308", NULL) == 2.2250738585072011e-308);
    CHECK(strtod("0x1.8p1", NULL) == 3.0 && strtod("-0X10", NULL) == -16.0);
    CHECK(strtod("  1.5x", &end) == 1.5 && *end == 'x' && strtod("0x", &end) == 0 && *end == 'x');
    CHECK(strtod("abc", &end) == 0 && strcmp(end, "abc") == 0);
    CHECK(isinf(strtod("-Infinity", NULL)) && isnan(strtod("nan", NULL)));
    CHECK(signbit(strtod("-0", NULL)) && strtof("0.1", NULL) == 0.1f);
    errno = 0;
    CHECK(strtod("1e400", NULL) == HUGE_VAL && errno == ERANGE);

    // a number printed with %.17g or %a reads back the same
    double values[] = {0.1, 1.0 / 3, 2.0 / 3, 1e23, 5e-324, 1.7976931348623157e308, 123.456};
    int same = 1;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
    {
        char a[64], b[64];
        snprintf(a, sizeof(a), "%.17g", values[i]);
        snprintf(b, sizeof(b), "%a", values[i]);
        same &= strtod(a, NULL) == values[i] && strtod(b, NULL) == values[i];
    }
    CHECK(same);
}

int main(void)
{
    test_printf();
    test_scan();
    test_strings();
    test_sort();
    test_streams();
    test_memory();
    test_control();
    test_math();
    test_wide();
    test_regex();
    test_posix();
    test_files();
    test_threads();

    if (failures)
        printf("libctest: %d of %d checks failed\n", failures, checks);
    else
        printf("libctest: %d checks, ok\n", checks);
    return failures ? 1 : 0;
}
