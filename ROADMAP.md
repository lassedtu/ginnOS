# GinnOS Roadmap

## Overview

GinnOS is a Unix-like hobby operating system written from scratch in C and Assembly.

The goal is to evolve GinnOS from a bootable kernel into a complete operating system capable of running independent user programs, providing a Unix-like environment, and eventually shipping with its own native applications.

The current focus is building the core OS infrastructure required to support a real userspace environment.

---

# Current State

GinnOS currently has:

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

## Features

### Physical Memory Manager

Implement a page-based physical memory allocator.

Requirements:

* Parse available memory map
* Track physical pages
* Bitmap allocator
* Allocate and free 4 KiB pages

Target API:

```c
void *pmm_alloc_page(void);
void pmm_free_page(void *address);
```

---

### Kernel Heap

Build dynamic kernel allocation on top of the physical memory manager.

Requirements:

* kmalloc
* kfree
* Heap metadata
* Basic fragmentation handling

Target API:

```c
void *kmalloc(size_t size);
void kfree(void *ptr);
```

---

# Virtual Memory

## Goal

Introduce memory isolation and prepare for userspace.

Features:

* Paging initialization
* Page directory management
* Kernel virtual address space
* User address spaces

Requirements:

* Page allocation
* Mapping/unmapping pages
* Page fault handling

---

# Userspace Foundation

## Goal

Separate applications from the kernel.

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

Features:

* Ring 3 user mode
* User stacks
* User memory protection
* System call interface

---

# System Calls

## Goal

Provide a controlled interface between programs and the kernel.

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

Applications should no longer directly access kernel drivers.

---

# Executable Loading

## Goal

Run programs stored on disk.

Features:

* ELF userspace loader
* Program memory mapping
* Entry point execution
* Program arguments
* Environment setup

Example:

```
/bin/hello

exec("/bin/hello")
```

---

# Process Management

## Goal

Support multiple running programs.

Features:

* Process structure
* Process IDs
* Kernel stacks
* Process states
* Context switching

Example:

```
PID 1 init

PID 2 shell

PID 3 editor
```

---

# Multitasking

## Goal

Allow multiple programs to run concurrently.

Features:

* Timer-based scheduling
* Round-robin scheduler
* Process sleeping/waking
* Process termination

Initial scheduler:

* Simple round robin
* No priorities

---

# Userspace Environment

## Goal

Create a Unix-like user environment.

Move existing commands from the kernel into userspace:

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

The shell becomes a normal program instead of a kernel component.

---

# Terminal System

## Goal

Create a proper terminal abstraction.

Features:

* Terminal input handling
* Cursor control
* Screen buffers
* ANSI-like escape sequences

Used by:

* Shell
* Text editor
* Future applications

---

# Native Text Editor

## Goal

Ship GinnOS with its own text editor.

The editor should be a native userspace application.

Features:

* Open files
* Save files
* Text buffer management
* Cursor movement
* Selection
* Search
* Editing commands
* Keyboard shortcuts

Possible future features:

* Syntax highlighting
* Multiple buffers
* Configuration files
* Plugins

---

# Long-Term Goals

After the editor milestone:

* User accounts
* Permissions
* Better filesystem support
* Networking
* Device drivers
* Graphical environment
* Package management
* Native applications

---

# Development Philosophy

GinnOS prioritizes understanding and clean architecture over speed.

Major subsystems should have clear interfaces so they can be replaced or improved later.

Early implementations should prioritize simplicity and correctness over optimization.
