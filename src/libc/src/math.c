/**
 * floating-point functions with the x87 FPU (math.h).
 *
 * the "t" constraint puts a value in st(0), "u" in st(1). fsin, fcos and
 * fptan take arguments up to 2^63 in size; larger ones are returned as
 * they are (the C library does not reduce them).
 */

#include <math.h>
#include <stdbool.h>

double fabs(double x)
{
    return x < 0 ? -x : x;
}

double fmin(double a, double b)
{
    if (isnan(a))
        return b;
    if (isnan(b))
        return a;
    return a < b ? a : b;
}

double fmax(double a, double b)
{
    if (isnan(a))
        return b;
    if (isnan(b))
        return a;
    return a > b ? a : b;
}

double sqrt(double x)
{
    double result;
    __asm__("fsqrt" : "=t"(result) : "0"(x));
    return result;
}

double sin(double x)
{
    double result;
    __asm__("fsin" : "=t"(result) : "0"(x));
    return result;
}

double cos(double x)
{
    double result;
    __asm__("fcos" : "=t"(result) : "0"(x));
    return result;
}

double tan(double x)
{
    // fptan pushes 1.0 after the result: pop it
    double result;
    __asm__("fptan\n\tfstp %%st(0)" : "=t"(result) : "0"(x));
    return result;
}

double atan2(double y, double x)
{
    // fpatan: st(0) = atan(st(1) / st(0)), with the quadrant from the signs
    double result;
    __asm__("fpatan" : "=t"(result) : "0"(x), "u"(y) : "st(1)");
    return result;
}

double atan(double x)
{
    return atan2(x, 1.0);
}

double asin(double x)
{
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x)
{
    return atan2(sqrt(1.0 - x * x), x);
}

/**
 * y * log2(x) in the x87's 80-bit precision (fyl2x: st(0) = st(1) *
 * log2(st(0))). log, log10 and pow round only the end result to a double,
 * so a result that is a whole number (as log10(1000)) comes out exact.
 */
static long double times_log2(long double y, long double x)
{
    long double result;
    __asm__("fyl2x" : "=t"(result) : "0"(x), "u"(y) : "st(1)");
    return result;
}

/** 2^t in the x87's 80-bit precision; t is finite and not too large */
static long double exp2_extended(long double t)
{
    long double whole;
    __asm__("frndint" : "=t"(whole) : "0"(t));
    long double fraction = t - whole; // at most 0.5 in size
    long double result;
    // f2xm1: 2^f - 1 for |f| <= 1; fscale: multiply by 2^whole
    __asm__("f2xm1\n\t"
            "fld1\n\t"
            "faddp\n\t"
            "fscale"
            : "=t"(result)
            : "0"(fraction), "u"(whole));
    return result;
}

/**
 * 2^t as a double. past +-1100 the result is past the largest double or
 * below the smallest one, so it is infinity or 0.
 */
static double exp2_double(long double t)
{
    if (t != t)
    {
        return (double)t; // NaN
    }
    if (t > 1100.0L)
    {
        return HUGE_VAL;
    }
    if (t < -1100.0L)
    {
        return 0.0;
    }
    return (double)exp2_extended(t);
}

double log2(double x)
{
    return (double)times_log2(1.0L, x);
}

double log(double x)
{
    return (double)times_log2(0.693147180559945309417232121458176568L, x); // ln(2)
}

double log10(double x)
{
    return (double)times_log2(0.301029995663981195213738894724493027L, x); // log10(2)
}

/**
 * the round-down of x to an integer, done without the FPU control word: a
 * value of 2^52 or more has no fraction left.
 */
double floor(double x)
{
    if (x != x || fabs(x) >= 4503599627370496.0)
    {
        return x;
    }
    double t = (double)(long long)x;
    return t > x ? t - 1.0 : t;
}

double ceil(double x)
{
    return -floor(-x);
}

double trunc(double x)
{
    return x < 0 ? ceil(x) : floor(x);
}

double round(double x)
{
    return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5);
}

double fmod(double x, double y)
{
    // fprem reduces in steps; repeat until C2 (bit 10 of the status) is 0
    double result;
    __asm__("1: fprem\n\t"
            "fnstsw %%ax\n\t"
            "testw $0x400, %%ax\n\t"
            "jnz 1b"
            : "=t"(result)
            : "0"(x), "u"(y)
            : "ax", "cc");
    return result;
}

double exp(double x)
{
    // e^x = 2^(x * log2(e))
    return exp2_double(x * 1.442695040888963407359924681001892137L);
}

/**
 * x^y = 2^(y * log2(x)), all in 80-bit precision: with doubles between
 * the steps, pow(2, 10) was 1023.9999999999998.
 */
double pow(double x, double y)
{
    if (y == 0.0 || x == 1.0)
    {
        return 1.0;
    }
    if (x == 0.0)
    {
        return y < 0.0 ? HUGE_VAL : 0.0;
    }
    if (x < 0.0)
    {
        // only whole exponents have a real result
        double whole = floor(y);
        if (whole != y)
        {
            return NAN;
        }
        double r = exp2_double(times_log2(y, -x));
        // an odd whole number below 2^53 gives a negative result
        bool odd = fabs(y) < 9007199254740992.0 && ((long long)whole & 1);
        return odd ? -r : r;
    }
    return exp2_double(times_log2(y, x));
}

double ldexp(double x, int exponent)
{
    // fscale: x * 2^exponent, exactly
    double result;
    __asm__("fscale" : "=t"(result) : "0"(x), "u"((double)exponent));
    return result;
}

double scalbn(double x, int exponent)
{
    return ldexp(x, exponent);
}

double frexp(double x, int *exponent)
{
    union
    {
        double d;
        unsigned long long bits;
    } v = {x};
    int raw = (int)((v.bits >> 52) & 0x7FF);
    *exponent = 0;
    if (raw == 0x7FF || x == 0.0)
        return x; // infinity, NaN and zero stay as they are
    if (raw == 0)
    {
        // a subnormal number: make it normal first
        v.d = x * 18014398509481984.0; // 2^54
        raw = (int)((v.bits >> 52) & 0x7FF);
        *exponent = -54;
    }
    // the fraction in [0.5, 1): the exponent field of 0.5 is 0x3FE
    *exponent += raw - 0x3FE;
    v.bits = (v.bits & ~(0x7FFULL << 52)) | (0x3FEULL << 52);
    return v.d;
}

// below this size, sinh(x) and tanh(x) are x to the last bit
#define TINY 3.7252902984e-09 // 2^-28

double sinh(double x)
{
    if (fabs(x) < TINY)
        return x;
    return (exp(x) - exp(-x)) / 2.0;
}

double cosh(double x)
{
    return (exp(x) + exp(-x)) / 2.0;
}

double tanh(double x)
{
    if (fabs(x) < TINY)
        return x;
    if (x > 22.0)
        return 1.0; // e^(2x) is too large to add 1 to
    if (x < -22.0)
        return -1.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}

/** an 80-bit x87 number: 64 bits of mantissa, then the sign and exponent */
typedef union
{
    long double value;
    struct
    {
        unsigned long long mantissa;
        unsigned short sign_exponent;
    } parts;
} extended_t;

long double fabsl(long double x)
{
    extended_t v = {x};
    v.parts.sign_exponent &= 0x7FFF;
    return v.value;
}

long double copysignl(long double x, long double y)
{
    extended_t v = {x};
    extended_t s = {y};
    v.parts.sign_exponent = (unsigned short)((v.parts.sign_exponent & 0x7FFF) |
                                             (s.parts.sign_exponent & 0x8000));
    return v.value;
}

double copysign(double x, double y)
{
    return (double)copysignl(x, y);
}

long double fmodl(long double x, long double y)
{
    // as fmod: repeat fprem until C2 (bit 10 of the status) is 0
    long double result;
    __asm__("1: fprem\n\t"
            "fnstsw %%ax\n\t"
            "testw $0x400, %%ax\n\t"
            "jnz 1b"
            : "=t"(result)
            : "0"(x), "u"(y)
            : "ax", "cc");
    return result;
}

long double ldexpl(long double x, int exponent)
{
    // fscale: x * 2^exponent, rounded once
    long double result;
    __asm__("fscale" : "=t"(result) : "0"(x), "u"((long double)exponent));
    return result;
}

long double scalbnl(long double x, int exponent)
{
    return ldexpl(x, exponent);
}

long double frexpl(long double x, int *exponent)
{
    extended_t v = {x};
    int raw = v.parts.sign_exponent & 0x7FFF;
    *exponent = 0;
    if (raw == 0x7FFF || x == 0)
    {
        return x; // infinity, NaN and zero stay as they are
    }
    if (raw == 0)
    {
        // a subnormal number: make it normal first
        v.value = x * 0x1p120L;
        raw = v.parts.sign_exponent & 0x7FFF;
        *exponent = -120;
    }
    // the fraction in [0.5, 1): the exponent field of 0.5 is 0x3FFE
    *exponent += raw - 0x3FFE;
    v.parts.sign_exponent = (unsigned short)((v.parts.sign_exponent & 0x8000) | 0x3FFE);
    return v.value;
}
