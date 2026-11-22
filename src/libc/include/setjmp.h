#pragma once

/**
 * @file setjmp.h
 * @brief non-local jumps.
 *
 * a jmp_buf keeps the registers a function call must keep (i686: ebx, esi,
 * edi, ebp; x86_64: rbx, rbp, r12 to r15), the stack pointer and the return
 * address.
 */

typedef unsigned long jmp_buf[8]; // the registers setjmp saves (arch/*/setjmp.asm)

/**
 * save the calling context.
 * @return 0 when called; the value given to longjmp when it jumps here.
 */
int setjmp(jmp_buf env) __attribute__((returns_twice));

/**
 * go back to where setjmp saved env; setjmp then returns value (1 if
 * value is 0).
 */
void longjmp(jmp_buf env, int value) __attribute__((noreturn));

// the BSD names: setjmp here never saves the signal mask, so they are the
// same calls
#define _setjmp(env)          setjmp(env)
#define _longjmp(env, value) longjmp(env, value)
