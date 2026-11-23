/**
 * floating-point functions with the x87 FPU (math.h).
 *
 * the "t" constraint puts a value in st(0), "u" in st(1). fsin, fcos and
 * fptan take arguments up to 2^63 in size; larger ones are returned as
 * they are (the C library does not reduce them).
 */

#include <math.h>

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

double log2(double x)
{
    // fyl2x: st(0) = st(1) * log2(st(0))
    double result;
    __asm__("fyl2x" : "=t"(result) : "0"(x), "u"(1.0) : "st(1)");
    return result;
}

double log(double x)
{
    return log2(x) * 0.69314718055994530942; // ln(2)
}

double log10(double x)
{
    return log2(x) * 0.30102999566398119521; // log10(2)
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
    // e^x = 2^(x * log2(e)) = 2^i * 2^f with i whole and |f| < 1
    double t = x * 1.44269504088896340736; // log2(e)
    double i = round(t);
    double f = t - i;
    double result;
    // f2xm1: 2^f - 1 for |f| <= 1; fscale: multiply by 2^i
    __asm__("f2xm1\n\t"
            "fld1\n\t"
            "faddp\n\t"
            "fscale"
            : "=t"(result)
            : "0"(f), "u"(i));
    return result;
}

double pow(double x, double y)
{
    if (y == 0.0)
    {
        return 1.0;
    }
    if (x == 0.0)
    {
        return 0.0;
    }
    if (x < 0.0)
    {
        // only whole exponents have a real result
        double whole = floor(y);
        if (whole != y)
        {
            return NAN;
        }
        double r = exp(y * log(-x));
        long long n = (long long)whole;
        return (n & 1) ? -r : r;
    }
    return exp(y * log(x));
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
