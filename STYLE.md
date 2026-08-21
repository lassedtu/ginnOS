# Coding Style

This document describes the naming conventions and coding style used
throughout ginnOS. All new code should follow these rules, and existing
code will be migrated incrementally.

---

## Naming

| Element               | Convention            | Example                       |
|-----------------------|-----------------------|-------------------------------|
| Functions             | `snake_case`          | `ata_initialize`              |
| Variables             | `snake_case`          | `sector_count`                |
| Types (typedef)       | `snake_case_t`        | `ata_device_t`, `kerr_t`      |
| Macros / constants    | `UPPER_SNAKE_CASE`    | `ATA_REG_DATA`, `PAGE_SIZE`   |
| Struct members        | `snake_case`          | `bytes_per_block`             |
| Enum members          | `UPPER_SNAKE_CASE`    | `ATA_CHANNEL_PRIMARY`         |
| Public API functions  | subsystem prefix      | `vfs_open`, `pmm_alloc_page`  |
| Static functions      | no prefix required    | `map_ext2_file_type`          |

On-disk structures (packed structs that mirror hardware or filesystem
layout) keep the `_t` suffix like everything else: `ext2_superblock_t`,
`ext2_inode_t`.

---

## Types

- Always use `<stdint.h>` fixed-width types for sized data: `uint8_t`,
  `uint16_t`, `uint32_t`, `int32_t`, etc.
- Never use bare `int` or `unsigned` for data that has a defined width.
- Use `bool` (from `<stdbool.h>`) for boolean values.
- Use `kerr_t` for fallible kernel operations.
- Use `NULL` (from `common/stddef.h`) for null pointers, never just `0`.

---

## Formatting

- 4-space indentation, no tabs.
- Allman brace style (opening brace on its own line).
- Column limit: 100 characters.
- Pointer binds right: `int *ptr`, not `int* ptr`.
- No single-line `if`/`for`/`while` bodies without braces (except trivial returns).

See `.clang-format` for the canonical formatter configuration.

---

## Comments

- Function documentation uses `/** */` Doxygen blocks above the declaration.
- Every public function is documented in its header file.
- Every static function is documented at its definition.
- Inline explanations use `//` single-line comments.
- Struct field descriptions use `//` on the same line.
- Comments start lowercase and describe what/why, not how.

---

## Headers

- Use `#pragma once` as the include guard.
- Include the minimum set of headers needed.
- Group includes: same-module first, then `common/`, then other subsystems.
- No `../` relative paths in includes (use `-I src` flat paths).

---

## File structure

- One module per directory where practical.
- Public API in a single `.h` file per module.
- Internal shared helpers in a `*_internal.h` if multiple `.c` files need them.
- Every `.c` file includes its own header first.
