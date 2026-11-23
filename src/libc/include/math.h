#pragma once

/**
 * @file math.h
 * @brief floating-point functions (double only).
 *
 * the functions use the x87 FPU instructions, which exist on i686 and on
 * x86_64. the results are as exact as those instructions give; errno is
 * not set.
 */

#define M_PI    3.14159265358979323846
#define M_PI_2  1.57079632679489661923
#define M_E     2.7182818284590452354
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)

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

double sinh(double x);
double cosh(double x);
double tanh(double x);
