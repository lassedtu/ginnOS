#pragma once

#include "common/stdint.h"

uintptr_t kernel_start_address(void);
uintptr_t kernel_end_address(void);
size_t kernel_size(void);