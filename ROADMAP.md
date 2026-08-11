---
title: Roadmap
tags:
---
## Overview

The goal is to evolve ginnOS from a bootable kernel into a complete operating system capable of running independent user programs, providing a Unix-like environment, and eventually shipping with its own native applications.

The current focus is building the core OS infrastructure required to support a real userspace environment.

---

# Current State

ginnOS currently has:

* Custom boot process
* x86 protected mode kernel
* Interrupt Descriptor Table (IDT)
* Programmable Interrupt Controller (PIC)
* IRQ handling
* Keyboard interrupt driver
* VGA text mode driver
* Console subsystem
* Shell with lexer/parser/executor architecture
* Automatic command registration
* VFS abstraction layer
* ext2 filesystem support
* File and directory operations
* Kernel commands:
  * ls
  * cat
  * echo
  * clear
  * pwd
  * cd
  * touch
  * mkdir
  * rm
  * man
  * etc.

The next stage is moving from a kernel with integrated applications into a real operating system with isolated user programs.

---
# Memory Management

## Goal

Give the kernel the ability to dynamically manage memory.

Current progress:

- [x] Pass boot information from stage2 to kernel
- [x] Collect BIOS E820 memory map
- [x] Detect usable and reserved memory regions
- [x] Add kernel_start/kernel_end linker symbols
- [x] Add kernel layout helper API
- [x] Add initial boot memory reservation

---

## Memory Region Management

Before implementing the allocator, create a system for tracking reserved memory.

Requirements:

- Generic memory region reservation API
- Reserve kernel memory
- Reserve bootloader/stage2 memory
- Reserve allocator metadata memory

Example:

```

reserve_region(start, end)

```

Future reserved regions:

```

- bootloader
    
- kernel
    
- page tables
    
- PMM bitmap
    
- kernel heap
    

```

---

# Physical Memory Manager (PMM)

## Goal

Manage physical RAM using page frames.

Requirements:

- Parse E820 memory map
- Convert usable memory into page frames
- Track pages using bitmap allocator
- Allocate and free 4 KiB pages
- Prevent allocation of reserved regions

Target API:

```

void *pmm_alloc_page(void);  
void pmm_free_page(void *address);

```

Testing:

- Allocate pages
- Write/read data
- Free pages
- Verify reuse

---

# Kernel Heap

## Goal

Provide dynamic memory allocation inside the kernel.

Build on top of PMM.

Requirements:

- kmalloc
- kfree
- Heap metadata
- Free block tracking
- Basic fragmentation handling

Target API:

```

void *kmalloc(size_t size);  
void kfree(void *ptr);

```

---

# Virtual Memory

## Goal

Introduce paging and memory isolation.

Features:

- Paging initialization
- Page directory/table management
- Identity mapping
- Kernel virtual address space
- User address spaces

Requirements:

- Map/unmap pages
- Page fault handler
- Page protection flags
- User/kernel memory separation

---

# Kernel/User Boundary

## Goal

Transition from a kernel with built-in applications into a real operating system.

Current:

```

Kernel  
├── Shell  
├── Commands  
└── Filesystem

```

Target:

```

Kernel  
|  
Userspace  
├── Shell  
├── Programs  
└── Applications

```

Requirements:

- Ring 3 transition
- User stack setup
- User memory protection
- System call mechanism

---

# System Calls

## Goal

Create a stable interface between userspace and kernel.

Initial syscall set:

```

write()  
read()  
open()  
close()  
stat()  
create()  
mkdir()  
exec()  
exit()

```

Applications should no longer directly access:

- filesystem drivers
- keyboard drivers
- display hardware

---

# Executable Loading

## Goal

Load and execute programs from disk.

Features:

- ELF userspace loader
- Program segments
- Memory mapping
- Entry point execution
- User stack creation
- Argument passing

Example:

```

/bin/hello

exec("/bin/hello")

```

---

# Process Management

## Goal

Represent and manage running programs.

Features:

- Process structure
- PID system
- Kernel stacks
- Process states
- Parent/child relationships
- Process termination

Example:

```

PID 1 init

PID 2 shell

PID 3 editor

```

---

# Multitasking

## Goal

Run multiple processes.

Features:

- PIT/APIC timer
- Context switching
- Scheduler
- Sleeping/waking
- Process cleanup

Initial scheduler:

- Round robin
- No priorities

---

# Userspace Environment

## Goal

Create a Unix-like environment.

Move kernel commands into userspace:

```

/bin

sh  
ls  
cat  
mkdir  
rm  
mv  
touch

```

The shell becomes a normal program.

---

# Terminal System

## Goal

Create a proper terminal abstraction.

Features:

- Terminal input
- Output buffering
- Cursor control
- ANSI escape sequences
- Multiple terminals

Used by:

- Shell
- Text editor
- Applications

---

# Native Text Editor

## Goal

Ship ginnOS with a native editor.

The editor becomes the first major userspace application.

Features:

- File opening
- Saving
- Text buffer
- Cursor movement
- Selection
- Search
- Keyboard shortcuts

Future:

- Syntax highlighting
- Multiple buffers
- Configuration
- Plugins
