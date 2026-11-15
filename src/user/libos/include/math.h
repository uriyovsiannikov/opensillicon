#ifndef LIBOS_MATH_H
#define LIBOS_MATH_H

double acos(double);
double asin(double);
double atan2(double, double);
double atan(double);
double cbrt(double);
double ceil(double);
float ceilf(float);
double cos(double);
double floor(double);
double fabs(double);
float fabsf(float);
double hypot(double, double);
double frexp(double, int *);
double fmod(double, double);
double modf(double, double *);
double pow(double, double);
double log(double);
double log10(double);
double exp(double);
double ldexp(double, int);
double rint(double);
double round(double);
long lround(double);
long lroundf(float);
double sin(double);
double sqrt(double);
double tan(double);
void sincos(double, double *, double *);

#define M_PI 3.14159265358979323846264338327950288
#define M_PI_2 1.57079632679489661923132169163975144
#define M_PI_4 0.78539816339744830961566084581987572
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define isnan(value) __builtin_isnan(value)
#define isinf(value) __builtin_isinf(value)
#define isfinite(value) __builtin_isfinite(value)
#define signbit(value) __builtin_signbit(value)

#endif
