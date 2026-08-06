#include "../shell/command.h"

void echo_initialize(void);
void man_initialize(void);

void builtin_initialize(void)
{
    echo_initialize();
    man_initialize();
}