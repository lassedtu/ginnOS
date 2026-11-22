/**
 * @file apps_test.c
 * @brief host tests of libapps (make host-test), with the files in data/.
 */

#include "apps.h"
#include "hosttest.h"

#include <string.h>

static bool same(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void test_list(void)
{
    app_list_t *list = apps_load(TEST_DIR "/data");
    CHECK(list != NULL);
    if (!list)
        return;
    // broken.app has no exec; notes.txt is not an .app file
    CHECK_EQ(list->count, 3);
    if (list->count != 3)
    {
        apps_free(list);
        return;
    }
    // sorted by name, without looking at the letter case
    CHECK(same(list->apps[0].name, "alpha"));
    CHECK(same(list->apps[1].name, "Secret"));
    CHECK(same(list->apps[2].name, "Zebra"));

    const app_t *zebra = apps_find(list, "zebra");
    CHECK(zebra != NULL);
    if (zebra)
    {
        CHECK(same(zebra->exec, "zebra --fast"));
        CHECK(same(zebra->icon, "zebra-icon"));
        CHECK(same(zebra->comment, "Runs"));
        CHECK(same(zebra->category, "Games")); // as in APP_CATEGORIES
        CHECK(!zebra->hidden);
    }
    const app_t *alpha = apps_find(list, "alpha");
    CHECK(alpha && same(alpha->category, "Other")); // not a known category
    CHECK(alpha && same(alpha->icon, "") && same(alpha->comment, ""));
    const app_t *secret = apps_find(list, "secret");
    CHECK(secret && secret->hidden && same(secret->category, "System"));
    CHECK(apps_find(list, "broken") == NULL);
    CHECK(apps_find(list, "notes") == NULL);
    apps_free(list);

    // no directory: an empty list
    list = apps_load(TEST_DIR "/no-such-directory");
    CHECK(list != NULL && list->count == 0);
    apps_free(list);
}

static void test_split(void)
{
    const char *exec = "\"/usr/bin/my tool\" -x  \"two words\" a\"b c\"d ";
    char buffer[64];
    char *argv[APP_ARGS_MAX + 1];
    CHECK_EQ(app_split_exec(exec, buffer, argv), 4);
    CHECK(same(argv[0], "/usr/bin/my tool"));
    CHECK(same(argv[1], "-x"));
    CHECK(same(argv[2], "two words"));
    CHECK(same(argv[3], "ab cd"));
    CHECK(argv[4] == NULL);

    CHECK_EQ(app_split_exec("heimdemo", buffer, argv), 1);
    CHECK(same(argv[0], "heimdemo") && argv[1] == NULL);
    CHECK_EQ(app_split_exec("   ", buffer, argv), -1);
    CHECK_EQ(app_split_exec("a \"not closed", buffer, argv), -1);
    CHECK_EQ(app_split_exec("1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16", buffer, argv), 16);
    CHECK_EQ(app_split_exec("1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17", buffer, argv), -1);
}

int main(void)
{
    test_list();
    test_split();
    return hosttest_done("apps");
}
