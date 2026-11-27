/**
 * @file mathf.c
 * @brief the float functions of math.h: each calls its double function
 *        (math.c) and rounds the result to float once.
 */

#include <math.h>

float fabsf(float x)
{
    return (float)fabs(x);
}

float floorf(float x)
{
    return (float)floor(x);
}

float ceilf(float x)
{
    return (float)ceil(x);
}

float roundf(float x)
{
    return (float)round(x);
}

float truncf(float x)
{
    return (float)trunc(x);
}

float sqrtf(float x)
{
    return (float)sqrt(x);
}

float sinf(float x)
{
    return (float)sin(x);
}

float cosf(float x)
{
    return (float)cos(x);
}

float tanf(float x)
{
    return (float)tan(x);
}

float atanf(float x)
{
    return (float)atan(x);
}

float asinf(float x)
{
    return (float)asin(x);
}

float acosf(float x)
{
    return (float)acos(x);
}

float expf(float x)
{
    return (float)exp(x);
}

float logf(float x)
{
    return (float)log(x);
}

float log2f(float x)
{
    return (float)log2(x);
}

float log10f(float x)
{
    return (float)log10(x);
}

float sinhf(float x)
{
    return (float)sinh(x);
}

float coshf(float x)
{
    return (float)cosh(x);
}

float tanhf(float x)
{
    return (float)tanh(x);
}

float fminf(float a, float b)
{
    return (float)fmin(a, b);
}

float fmaxf(float a, float b)
{
    return (float)fmax(a, b);
}

float fmodf(float a, float b)
{
    return (float)fmod(a, b);
}

float atan2f(float a, float b)
{
    return (float)atan2(a, b);
}

float powf(float a, float b)
{
    return (float)pow(a, b);
}

float copysignf(float a, float b)
{
    return (float)copysign(a, b);
}

float ldexpf(float x, int exp)
{
    return (float)ldexp(x, exp);
}

float scalbnf(float x, int n)
{
    return (float)scalbn(x, n);
}

float frexpf(float x, int *exp)
{
    return (float)frexp(x, exp);
}
