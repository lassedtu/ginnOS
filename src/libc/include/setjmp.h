#pragma once

/**
 * @file setjmp.h
 * @brief non-local jumps.
 *
 * a jmp_buf keeps the registers a function call must keep (ebx, esi, edi,
 * ebp), the stack pointer and the return address.
 */

typedef unsigned int jmp_buf[6];

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
