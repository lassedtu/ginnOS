#include "kernel_layout.h"

extern uint8_t kernel_start;
extern uint8_t kernel_end;

uint32_t kernel_start_address(void)
{
    return (uint32_t)&kernel_start;
}

uint32_t kernel_end_address(void)
{
    return (uint32_t)&kernel_end;
}

uint32_t kernel_size(void)
{
    return kernel_end_address() - kernel_start_address();
}