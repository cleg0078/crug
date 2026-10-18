#include <complex.h>

double complex add(double complex a, double complex b) { return a + b; }
double real_part(double complex z) { return creal(z); }
