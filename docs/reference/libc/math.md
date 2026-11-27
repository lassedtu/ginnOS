<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;math.h&gt;

Source: [`src/libc/include/math.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/math.h)

floating-point functions (double, and float with an f).

the functions use the x87 FPU instructions, which exist on i686 and on
x86\_64. the results are as exact as those instructions give; errno is
not set.

## fmin, fmax, floor, ceil, round, trunc, fmod, sqrt, sin, cos, tan, atan, atan2, asin, acos, exp, log, log2, log10, pow

```c
double fmin(double a, double b);
double fmax(double a, double b);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
double fmod(double x, double y);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double asin(double x);
double acos(double x);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double pow(double x, double y);
```

the smaller (larger) of two; a NaN gives the other one

## ldexp, scalbn

```c
double ldexp(double x, int exponent);
double scalbn(double x, int exponent);
```

x \* 2^exponent

## frexp

```c
double frexp(double x, int *exponent);
```

split x into a fraction in \[0.5, 1) (or 0) and a power of 2:
x = fraction \* 2^\*exponent.

## copysignl, copysign

```c
long double copysignl(long double x, long double y);
double copysign(double x, double y);
```

x with the sign of y
