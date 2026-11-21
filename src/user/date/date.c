/**
 * @file date.c
 * @brief Print the date and time (UTC).
 *
 * usage: date [+format]   (format as for strftime)
 */

#include <stdio.h>
#include <time.h>

int main(int argc, char **argv)
{
    const char *format = "%a %b %e %H:%M:%S %Z %Y";
    if (argc > 1 && argv[1][0] == '+')
        format = argv[1] + 1;

    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);

    char text[128];
    if (strftime(text, sizeof(text), format, &tm) == 0 && format[0] != '\0')
    {
        printf("date: format too long\n");
        return 1;
    }
    printf("%s\n", text);
    return 0;
}
