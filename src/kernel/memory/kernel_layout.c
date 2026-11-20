#include "kernel_layout.h"

extern uint8_t kernel_start;
extern uint8_t kernel_end;

uintptr_t kernel_start_address(void)
{
    return (uintptr_t)&kernel_start;
}

uintptr_t kernel_end_address(void)
{
    return (uintptr_t)&kernel_end;
}

size_t kernel_size(void)
{
    return kernel_end_address() - kernel_start_address();
}