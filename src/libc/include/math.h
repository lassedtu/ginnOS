#pragma once

/**
 * @file math.h
 * @brief floating-point functions (double only).
 *
 * the functions use the x87 FPU instructions, which exist on i686 and on
 * x86_64. the results are as exact as those instructions give; errno is
 * not set.
 */

// the constants of POSIX (XSI)
#define M_E        2.7182818284590452354  // e
#define M_LOG2E    1.4426950408889634074  // log2(e)
#define M_LOG10E   0.43429448190325182765 // log10(e)
#define M_LN2      0.69314718055994530942 // ln(2)
#define M_LN10     2.30258509299404568402 // ln(10)
#define M_PI       3.14159265358979323846 // pi
#define M_PI_2     1.57079632679489661923 // pi/2
#define M_PI_4     0.78539816339744830962 // pi/4
#define M_1_PI     0.31830988618379067154 // 1/pi
#define M_2_PI     0.63661977236758134308 // 2/pi
#define M_2_SQRTPI 1.12837916709551257390 // 2/sqrt(pi)
#define M_SQRT2    1.41421356237309504880 // sqrt(2)
#define M_SQRT1_2  0.70710678118654752440 // 1/sqrt(2)
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))

#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())

#define isnan(x)    __builtin_isnan(x)
#define isinf(x)    __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x)  __builtin_signbit(x)

double fabs(double x);
/** the smaller (larger) of two; a NaN gives the other one */
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

/** x * 2^exponent */
double ldexp(double x, int exponent);
double scalbn(double x, int exponent);

/**
 * split x into a fraction in [0.5, 1) (or 0) and a power of 2:
 * x = fraction * 2^*exponent.
 */
double frexp(double x, int *exponent);

// long double (the x87's 80-bit numbers)
long double fabsl(long double x);
long double fmodl(long double x, long double y);
long double frexpl(long double x, int *exponent);
long double ldexpl(long double x, int exponent);
long double scalbnl(long double x, int exponent);
/** x with the sign of y */
long double copysignl(long double x, long double y);
double copysign(double x, double y);

double sinh(double x);
double cosh(double x);
double tanh(double x);
