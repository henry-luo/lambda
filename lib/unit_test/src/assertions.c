#include "../include/assertions.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

bool _test_float_eq(double a, double b, double epsilon) {
    if (isnan(a) && isnan(b)) return true;
    if (isinf(a) && isinf(b)) return (a > 0) == (b > 0);
    return fabs(a - b) <= epsilon;
}

bool _test_str_eq(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}
