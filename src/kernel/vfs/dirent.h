#pragma once

/**
 * the directory records getdents writes. the layout and the DT_ values are
 * ABI: the libc's <dirent.h> reads the same ones.
 *
 * records follow each other in the buffer; d_reclen is the size of one
 * record, its name and NUL included, rounded up to 4 bytes.
 */

#include "common/stdint.h"

// d_type values
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8

struct kdirent
{
    uint32_t d_ino;    // inode number
    uint16_t d_reclen; // bytes to the next record
    uint8_t d_type;    // DT_*
    char d_name[];     // NUL-terminated
} __attribute__((packed));

// bytes before d_name
#define KDIRENT_HEADER 7
