#include "predicates.h"
#include <math.h>
#pragma STDC FP_CONTRACT OFF

// Shewchuk's stage-A filter ("Adaptive Precision Floating-Point Arithmetic and
// Fast Robust Geometric Predicates", 1997): the binary64 determinant's sign is
// exact whenever |det| exceeds errbound * permanent. Only undecided cases pay
// for the exact decimal path, which costs tens of microseconds per call.
// The bounds assume no underflow or overflow, so the filter declines unless
// every nonzero coordinate difference lies in [1e-60, 1e60] (products of four
// stay normal). Each product is its own statement so a contracted FMA cannot
// replace the separately rounded operations the bounds were derived for.
bool procgen_predicate_filter(const double* p, int count, int dimension, bool lifted, int* sign) {
    const double eps = 1.1102230246251565e-16;  // 2^-53
    int rank = count - 1;
    double d[9];
    for (int row = 0; row < rank; row++) {
        for (int col = 0; col < dimension; col++) {
            double v = p[row * dimension + col] - p[rank * dimension + col];
            double m = fabs(v);
            if (v != 0 && (m < 1e-60 || m > 1e60)) return false;
            d[row * 3 + col] = v;
        }
    }
    double det, permanent, bound;
    if (count == 3) {
        double left = d[0] * d[4];
        double right = d[1] * d[3];
        det = left - right;
        permanent = fabs(left) + fabs(right);
        bound = (3.0 + 16.0 * eps) * eps;
    } else {
        double t0 = d[3] * d[7];
        double t1 = d[6] * d[4];
        double t2 = d[6] * d[1];
        double t3 = d[0] * d[7];
        double t4 = d[0] * d[4];
        double t5 = d[3] * d[1];
        double a2, b2, c2;
        if (lifted) {
            double ax2 = d[0] * d[0];
            double ay2 = d[1] * d[1];
            double bx2 = d[3] * d[3];
            double by2 = d[4] * d[4];
            double cx2 = d[6] * d[6];
            double cy2 = d[7] * d[7];
            a2 = ax2 + ay2;
            b2 = bx2 + by2;
            c2 = cx2 + cy2;
            bound = (10.0 + 96.0 * eps) * eps;
        } else {
            a2 = d[2];
            b2 = d[5];
            c2 = d[8];
            bound = (7.0 + 56.0 * eps) * eps;
        }
        double ta = a2 * (t0 - t1);
        double tb = b2 * (t2 - t3);
        double tc = c2 * (t4 - t5);
        det = ta + tb + tc;
        double pa = (fabs(t0) + fabs(t1)) * fabs(a2);
        double pb = (fabs(t2) + fabs(t3)) * fabs(b2);
        double pc = (fabs(t4) + fabs(t5)) * fabs(c2);
        permanent = pa + pb + pc;
    }
    double errbound = bound * permanent;
    if (det > errbound) { *sign = 1; return true; }
    if (-det > errbound) { *sign = -1; return true; }
    return false;
}

