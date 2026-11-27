<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;setjmp.h&gt;

Source: [`src/libc/include/setjmp.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/setjmp.h)

non-local jumps.

a jmp\_buf keeps the registers a function call must keep (i686: ebx, esi,
edi, ebp; x86\_64: rbx, rbp, r12 to r15), the stack pointer and the return
address.

## setjmp

```c
int setjmp(jmp_buf env) __attribute__((returns_twice));
```

save the calling context.

Returns: 0 when called; the value given to longjmp when it jumps here.

## longjmp

```c
void longjmp(jmp_buf env, int value) __attribute__((noreturn));
```

go back to where setjmp saved env; setjmp then returns value (1 if
value is 0).
