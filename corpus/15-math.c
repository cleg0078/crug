#include <math.h>

double norm(double x, double y) { return sqrt(x * x + y * y); }
double rounding(double x) { return floor(x) + ceil(x); }
float single(float x) { return fabsf(x); }
