/**
 * @file conf_test.c
 * @brief host tests of libconf (make host-test).
 */

#include "conf.h"
#include "hosttest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool same(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

/** the user's file after the changes is the expected text. */
static bool text_is(const conf_t *conf, const char *expected)
{
    char *text = conf_user_text(conf);
    bool ok = same(text, expected);
    if (!ok)
        fprintf(stderr, "--- got:\n%s--- expected:\n%s---\n", text ? text : "(null)", expected);
    free(text);
    return ok;
}

static void test_reading(void)
{
    conf_t *c = conf_from_text(NULL, "# a comment\n"
                                     "top = level\n"
                                     "\n"
                                     "[theme]\n"
                                     "  mode   =   dark  \n"
                                     "accent=#3B82F6\n"
                                     "glass = #ffffff80\n"
                                     "; another comment\n"
                                     "font_size = 13\n"
                                     "hex = 0x1F\n"
                                     "minus = -4\n"
                                     "padded = \"  two spaces  \"\n"
                                     "empty =\n"
                                     "twice = first\n"
                                     "twice = second\n"
                                     "[ spaced section ]\r\n"
                                     "on = Yes\r\n");
    CHECK(c != NULL);
    CHECK(same(conf_get(c, "", "top", NULL), "level"));
    CHECK(same(conf_get(c, "theme", "mode", NULL), "dark"));
    CHECK(same(conf_get(c, "theme", "padded", NULL), "  two spaces  "));
    CHECK(same(conf_get(c, "theme", "empty", "x"), ""));
    CHECK(same(conf_get(c, "theme", "twice", NULL), "second")); // the last one
    CHECK(same(conf_get(c, "theme", "none", "fallback"), "fallback"));
    CHECK(same(conf_get(c, "other", "mode", "fallback"), "fallback")); // other section
    CHECK(conf_get(c, "", "mode", NULL) == NULL);
    CHECK_EQ(conf_get_int(c, "theme", "font_size", 0), 13);
    CHECK_EQ(conf_get_int(c, "theme", "hex", 0), 31);
    CHECK_EQ(conf_get_int(c, "theme", "minus", 0), -4);
    CHECK_EQ(conf_get_int(c, "theme", "mode", 7), 7); // not a number
    CHECK_EQ(conf_get_int(c, "theme", "empty", 7), 7);
    CHECK_EQ(conf_get_color(c, "theme", "accent", 0), 0xFF3B82F6);
    CHECK_EQ(conf_get_color(c, "theme", "glass", 0), 0x80FFFFFF);
    CHECK_EQ(conf_get_color(c, "theme", "mode", 1), 1);
    CHECK(conf_get_bool(c, "spaced section", "on", false) == true); // \r and spaces removed
    CHECK(conf_get_bool(c, "theme", "mode", true) == true);         // not a bool
    CHECK_EQ(conf_bad_lines(c), 0);
    conf_free(c);
}

static void test_layers(void)
{
    conf_t *c = conf_from_text("[theme]\nmode = light\naccent = #ff0000\n",
                               "[theme]\nmode = dark\n");
    CHECK(same(conf_get(c, "theme", "mode", NULL), "dark"));        // the user's value wins
    CHECK(same(conf_get(c, "theme", "accent", NULL), "#ff0000"));   // the default
    CHECK(conf_set(c, "theme", "accent", "#00ff00") == 0);
    CHECK(same(conf_get(c, "theme", "accent", NULL), "#00ff00"));
    CHECK(conf_unset(c, "theme", "mode") == 0);
    CHECK(same(conf_get(c, "theme", "mode", NULL), "light")); // the default again
    // only the user's values are written
    CHECK(text_is(c, "[theme]\naccent = #00ff00\n"));
    conf_free(c);
}

static void test_writing(void)
{
    const char *start = "# my settings\n"
                        "\n"
                        "[theme]\n"
                        "mode = dark   # not a comment\n"
                        "\n"
                        "[keyboard]\n"
                        "layout = dk\n"
                        "\n"
                        "# the end\n";
    conf_t *c = conf_from_text(NULL, start);
    CHECK(text_is(c, start)); // unchanged when nothing is set
    CHECK(same(conf_get(c, "theme", "mode", NULL), "dark   # not a comment"));

    // a changed line stays where it is; a new key goes after the last line
    // of its section, before the blank line
    CHECK(conf_set(c, "theme", "mode", "light") == 0);
    CHECK(conf_set_int(c, "theme", "font_size", 14) == 0);
    CHECK(conf_set_bool(c, "keyboard", "repeat", true) == 0);
    CHECK(conf_set_color(c, "theme", "accent", 0xFF3B82F6) == 0);
    CHECK(conf_set_color(c, "theme", "glass", 0x80FFFFFF) == 0);
    CHECK(conf_set(c, "theme", "name", " spaced ") == 0);
    CHECK(conf_set(c, "", "version", "1") == 0); // before the first section
    CHECK(conf_set(c, "mouse", "speed", "5") == 0); // a new section at the end
    CHECK(text_is(c, "# my settings\n"
                     "version = 1\n"
                     "\n"
                     "[theme]\n"
                     "mode = light\n"
                     "font_size = 14\n"
                     "accent = #3b82f6\n"
                     "glass = #ffffff80\n"
                     "name = \" spaced \"\n"
                     "\n"
                     "[keyboard]\n"
                     "layout = dk\n"
                     "repeat = true\n"
                     "\n"
                     "# the end\n"
                     "\n"
                     "[mouse]\n"
                     "speed = 5\n"));
    // the values read back as they were set
    CHECK(same(conf_get(c, "theme", "name", NULL), " spaced "));
    CHECK_EQ(conf_get_color(c, "theme", "glass", 0), 0x80FFFFFF);

    // a value in quotes keeps its quotes
    CHECK(conf_set(c, "theme", "quoted", "\"x\"") == 0);
    CHECK(same(conf_get(c, "theme", "quoted", NULL), "\"x\""));

    // bad names and values
    CHECK(conf_set(c, "the]me", "a", "b") == -1);
    CHECK(conf_set(c, "theme", "a=b", "c") == -1);
    CHECK(conf_set(c, "theme", "#a", "c") == -1);
    CHECK(conf_set(c, "theme", " a", "c") == -1);
    CHECK(conf_set(c, "theme", "", "c") == -1);
    CHECK(conf_set(c, "theme", "a", "two\nlines") == -1);
    conf_free(c);

    // an empty file gets the section without a blank line before it
    c = conf_from_text(NULL, NULL);
    CHECK(conf_set(c, "theme", "mode", "dark") == 0);
    CHECK(text_is(c, "[theme]\nmode = dark\n"));
    conf_free(c);
}

static void test_bad_lines(void)
{
    const char *text = "[theme\n"
                       "just words\n"
                       "= no key\n"
                       "[ok]\n"
                       "a = 1\n";
    conf_t *c = conf_from_text(NULL, text);
    CHECK_EQ(conf_bad_lines(c), 3);
    CHECK_EQ(conf_get_int(c, "ok", "a", 0), 1);
    CHECK(text_is(c, text)); // kept as they are
    // a bad section line does not start a section: its keys are in the last one
    conf_free(c);
}

static void test_files(void)
{
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/../../../../build/host-test/conf/home", TEST_DIR);
    char system_path[300];
    char user_path[300];
    snprintf(system_path, sizeof(system_path), "%s/system.conf", dir);
    snprintf(user_path, sizeof(user_path), "%s/.config/ginnos/test.conf", dir);
    mkdir(dir, 0755);
    unlink(user_path);

    FILE *f = fopen(system_path, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    fputs("[theme]\nmode = light\n", f);
    fclose(f);

    // no user file yet: the defaults; save makes the directories
    conf_t *c = conf_load(system_path, user_path);
    CHECK(same(conf_get(c, "theme", "mode", NULL), "light"));
    CHECK(conf_set(c, "theme", "mode", "dark") == 0);
    CHECK(conf_save(c) == 0);
    conf_free(c);

    c = conf_load(system_path, user_path);
    CHECK(same(conf_get(c, "theme", "mode", NULL), "dark"));
    CHECK(conf_save(c) == 0); // over the old file
    conf_free(c);

    char temp[310];
    snprintf(temp, sizeof(temp), "%s.new", user_path);
    CHECK(access(temp, F_OK) != 0); // no temporary file is left

    // from text: there is no file to save to
    c = conf_from_text(NULL, "a = b\n");
    CHECK(conf_save(c) == -1);
    conf_free(c);

    // conf_open finds the user's file through $HOME
    setenv("HOME", dir, 1);
    c = conf_open("test");
    CHECK(c != NULL);
    CHECK(same(conf_get(c, "theme", "mode", NULL), "dark"));
    conf_free(c);
    CHECK(conf_open("../etc/passwd") == NULL);
    CHECK(conf_open("") == NULL);
}

int main(void)
{
    test_reading();
    test_layers();
    test_writing();
    test_bad_lines();
    test_files();
    return hosttest_done("conf");
}
