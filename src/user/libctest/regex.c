/**
 * @file regex.c
 * @brief libctest: regular expressions, basic and extended.
 */

#include "libctest.h"

#include <regex.h>
#include <string.h>

/** true when pattern (with cflags) matches text */
static int matches(const char *pattern, int cflags, const char *text)
{
    regex_t re;
    if (regcomp(&re, pattern, cflags | REG_NOSUB) != REG_OK)
        return -1;
    int result = regexec(&re, text, 0, NULL, 0) == REG_OK;
    regfree(&re);
    return result;
}

static void test_basic(void)
{
    CHECK(matches("ab*c", 0, "xxabbbcxx") == 1);
    CHECK(matches("^ab", 0, "xab") == 0);
    CHECK(matches("a\\{2,3\\}", 0, "caab") == 1);
    CHECK(matches("a\\{2,3\\}", 0, "cab") == 0);
    CHECK(matches("\\(ab\\)\\1", 0, "xabab") == 1); // a back reference
    CHECK(matches("a+", 0, "a+") == 1);             // + is not special in BRE
    CHECK(matches("[[:digit:]][[:alpha:]]", 0, "--7q") == 1);
    CHECK(matches("[^a-c]", 0, "abc") == 0);
}

static void test_extended(void)
{
    CHECK(matches("colou?r", REG_EXTENDED, "color") == 1);
    CHECK(matches("^(cat|dog)s?$", REG_EXTENDED, "dogs") == 1);
    CHECK(matches("^(cat|dog)s?$", REG_EXTENDED, "doge") == 0);
    CHECK(matches("[0-9]+\\.[0-9]+", REG_EXTENDED, "v 3.14") == 1);
    CHECK(matches("HELLO", REG_EXTENDED | REG_ICASE, "say hello") == 1);
    CHECK(matches("^b", REG_EXTENDED | REG_NEWLINE, "a\nb") == 1);
    CHECK(matches("^b", REG_EXTENDED, "a\nb") == 0);
}

/** where the match and its parts are */
static void test_offsets(void)
{
    regex_t re;
    CHECK(regcomp(&re, "([a-z]+)@([a-z]+)", REG_EXTENDED) == REG_OK);
    CHECK(re.re_nsub == 2);
    regmatch_t m[3];
    CHECK(regexec(&re, "mail: ann@host!", 3, m, 0) == REG_OK);
    CHECK(m[0].rm_so == 6 && m[0].rm_eo == 14);
    CHECK(m[1].rm_so == 6 && m[1].rm_eo == 9);
    CHECK(m[2].rm_so == 10 && m[2].rm_eo == 14);
    regfree(&re);
}

/** UTF-8: one character is one character, with its case */
static void test_utf8_regex(void)
{
    CHECK(matches("^bl.b$", REG_EXTENDED, "bl\xc3\xa5" "b") == 1);
    CHECK(matches("^[[:alpha:]]+$", REG_EXTENDED, "s\xc3\xb8" "en") == 1);
    CHECK(matches("\xc3\x86", REG_EXTENDED | REG_ICASE, "\xc3\xa6") == 1); // AE and ae
}

static void test_errors(void)
{
    regex_t re;
    CHECK(regcomp(&re, "a(b", REG_EXTENDED) == REG_EPAREN);
    CHECK(regcomp(&re, "[a", REG_EXTENDED) == REG_EBRACK);
    CHECK(regcomp(&re, "[[:nothing:]]", REG_EXTENDED) == REG_ECTYPE);
    char message[64];
    size_t need = regerror(REG_EBRACK, NULL, message, sizeof(message));
    CHECK(strcmp(message, "Missing ']'") == 0 && need == strlen(message) + 1);
    regerror(REG_EBRACK, NULL, message, 4);
    CHECK(strcmp(message, "Mis") == 0);
}

void test_regex(void)
{
    test_basic();
    test_extended();
    test_offsets();
    test_utf8_regex();
    test_errors();
}
