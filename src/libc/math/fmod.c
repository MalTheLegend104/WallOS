#include <math.h>

double fmod(double x, double y) {
    return x - y * floor(x / y);
}