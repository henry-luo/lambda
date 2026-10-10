#include "animation_value.hpp"
#include <math.h>
#include <string.h>

unsigned animation_keyframe_segment(const double* times, unsigned count, double time) {
    if (!times || !count || time < times[0]) return 0;
    unsigned low = 0, high = count;
    // upper-bound search selects the last equal-time key, including discrete boundaries.
    while (low < high) {
        unsigned middle = low + (high - low) / 2;
        if (time < times[middle]) high = middle;
        else low = middle + 1;
    }
    return low ? low - 1 : 0;
}

bool animation_value_combine(double* left, const double* right, unsigned count,
        double left_weight, double right_weight) {
    if (!left || !right || !isfinite(left_weight) || !isfinite(right_weight)) return false;
    for (unsigned i = 0; i < count; i++) {
        left[i] = left[i] * left_weight + right[i] * right_weight;
        if (!isfinite(left[i])) return false;
    }
    return true;
}

bool animation_quaternion_normalize(double value[4]) {
    double length = hypot(hypot(value[0], value[1]), hypot(value[2], value[3]));
    if (!isfinite(length) || length == 0) return false;
    for (unsigned i = 0; i < 4; i++) value[i] /= length;
    return true;
}

bool animation_quaternion_slerp(const double left[4], const double right[4],
        double weight, double result[4]) {
    double a[4], b[4]; memcpy(a, left, sizeof(a)); memcpy(b, right, sizeof(b));
    if (!isfinite(weight) || !animation_quaternion_normalize(a) || !animation_quaternion_normalize(b)) return false;
    double product = 0;
    for (unsigned i = 0; i < 4; i++) product += a[i] * b[i];
    double orientation = product < 0 ? -1 : 1;
    product = fmin(fabs(product), 1);
    double aw = 1 - weight, bw = weight;
    // equivalent signs use the shortest arc; near-equal rotations avoid sin(0).
    if (product < 1 - 1e-12) {
        double angle = acos(product), denominator = sin(angle);
        aw = sin((1 - weight) * angle) / denominator;
        bw = sin(weight * angle) / denominator;
    }
    for (unsigned i = 0; i < 4; i++) result[i] = aw * a[i] + bw * orientation * b[i];
    return animation_quaternion_normalize(result);
}
bool animation_quaternion_matrix(const double value[4], double matrix[16]) {
    double q[4];memcpy(q,value,sizeof(q));if(!animation_quaternion_normalize(q)) return false;
    memset(matrix,0,16*sizeof(double));matrix[15]=1;
    for(unsigned axis=0;axis<3;axis++) {
        unsigned next=(axis+1)%3,last=(axis+2)%3;
        matrix[axis*4+axis]=1-2*(q[next]*q[next]+q[last]*q[last]);
        matrix[axis*4+next]=2*(q[axis]*q[next]-q[last]*q[3]);
        matrix[axis*4+last]=2*(q[axis]*q[last]+q[next]*q[3]);
    }
    return true;
}

bool animation_value_mix(AnimationValueType type, double* result, const double* value,
        unsigned count, double weight, bool additive) {
    if (!result || !value || !isfinite(weight)) return false;
    if (type == ANIMATION_QUATERNION) {
        if (count != 4) return false;
        if (!additive) return animation_quaternion_slerp(result, value, weight, result);
        const double identity[4] = {0, 0, 0, 1}; double delta[4], original[4];
        memcpy(original, result, sizeof(original));
        if (!animation_quaternion_slerp(identity, value, weight, delta)) return false;
        for (unsigned i = 0; i < 3; i++) {
            unsigned next = (i + 1) % 3, last = (i + 2) % 3;
            result[i] = original[3] * delta[i] + original[i] * delta[3] +
                original[next] * delta[last] - original[last] * delta[next];
        }
        result[3] = original[3] * delta[3] - original[0] * delta[0] - original[1] * delta[1] - original[2] * delta[2];
        return animation_quaternion_normalize(result);
    }
    if (type == ANIMATION_BOOLEAN) {
        if (weight >= .5) memcpy(result, value, count * sizeof(double));
        return true;
    }
    return animation_value_combine(result, value, count, additive ? 1 : 1 - weight, weight);
}

bool animation_track_validate(const AnimationTrackView& track) {
    if (!track.times || !track.keys || track.keys > 1048576 || !track.components || track.components > 2048 ||
        (unsigned)track.type > ANIMATION_STRING || (unsigned)track.interpolation > ANIMATION_HERMITE) return false;
    if (track.type == ANIMATION_STRING ? !track.strings : !track.values) return false;
    if (track.type == ANIMATION_QUATERNION && (track.components != 4 || (track.interpolation > ANIMATION_LINEAR && track.interpolation != ANIMATION_HERMITE))) return false;
    if ((track.type == ANIMATION_BOOLEAN || track.type == ANIMATION_STRING) &&
        (track.components != 1 || track.interpolation != ANIMATION_DISCRETE)) return false;
    if (track.interpolation == ANIMATION_HERMITE && (!track.in_tangents || !track.out_tangents)) return false;
    for (unsigned i = 0; i < track.keys; i++) {
        if (!isfinite(track.times[i]) || (i && track.times[i] < track.times[i - 1])) return false;
        for (unsigned c = 0; c < track.components; c++) {
            size_t offset = (size_t)i * track.components + c;
            if (track.type == ANIMATION_STRING) { if (!track.strings[offset]) return false; }
            else if (!isfinite(track.values[offset])) return false;
            else if(track.type==ANIMATION_BOOLEAN&&track.values[offset]!=0&&track.values[offset]!=1) return false;
            if (track.interpolation == ANIMATION_HERMITE &&
                (!isfinite(track.in_tangents[offset]) || !isfinite(track.out_tangents[offset]))) return false;
            if (track.interpolation == ANIMATION_BEZIER && track.in_tangents && track.out_tangents) for (unsigned pair = 0; pair < 2; pair++)
                if (!isfinite(track.in_tangents[offset * 2 + pair]) || !isfinite(track.out_tangents[offset * 2 + pair])) return false;
        }
        if (track.type == ANIMATION_QUATERNION) {
            double q[4]; memcpy(q, track.values + i * 4, sizeof(q));
            if (!animation_quaternion_normalize(q)) return false;
        }
    }
    return true;
}

static double animation_cubic(double a, double b, double c, double d, double t) {
    double s = 1 - t;
    return s * s * s * a + 3 * s * s * t * b + 3 * s * t * t * c + t * t * t * d;
}

bool animation_track_sample(const AnimationTrackView& track, double time, double* result, const char** string_result) {
    if (!track.times || !track.keys || !isfinite(time)) return false;
    unsigned key = animation_keyframe_segment(track.times, track.keys, time);
    bool discrete = track.interpolation == ANIMATION_DISCRETE || key + 1 == track.keys || time <= track.times[0];
    if (track.type == ANIMATION_STRING) {
        if (!string_result || !track.strings) return false;
        *string_result = track.strings[key]; return true;
    }
    if (!result || !track.values) return false;
    const double* a = track.values + (size_t)key * track.components;
    if (discrete) { memcpy(result, a, track.components * sizeof(double));return track.type!=ANIMATION_QUATERNION||animation_quaternion_normalize(result); }
    double start = track.times[key], end = track.times[key + 1], duration = end - start;
    if (!(duration > 0)) return false;
    double t = (time - start) / duration;
    const double* b = a + track.components;
    if (track.interpolation == ANIMATION_HERMITE) {
        if (!track.in_tangents || !track.out_tangents) return false;
        double t2=t*t,t3=t2*t;size_t offset=(size_t)key*track.components;
        // glTF tangents are derivatives: scale them by this segment's duration.
        for(unsigned c=0;c<track.components;c++) {
            result[c]=(2*t3-3*t2+1)*a[c]+(t3-2*t2+t)*duration*track.out_tangents[offset+c]+
                (-2*t3+3*t2)*b[c]+(t3-t2)*duration*track.in_tangents[offset+track.components+c];
            if(!isfinite(result[c])) return false;
        }
        return track.type!=ANIMATION_QUATERNION||animation_quaternion_normalize(result);
    }
    if (track.type == ANIMATION_QUATERNION) return animation_quaternion_slerp(a, b, t, result);
    memcpy(result, a, track.components * sizeof(double));
    if (track.interpolation == ANIMATION_LINEAR || (track.interpolation == ANIMATION_BEZIER &&
        (!track.in_tangents || !track.out_tangents)))
        return animation_value_combine(result, b, track.components, 1 - t, t);
    for (unsigned c = 0; c < track.components; c++) {
        if (track.interpolation == ANIMATION_BEZIER) {
            if (!track.in_tangents || !track.out_tangents) return false;
            size_t offset = ((size_t)key * track.components + c) * 2;
            const double* out = track.out_tangents + offset;
            const double* in = track.in_tangents + offset + track.components * 2;
            double parameter = t;
            // match the pinned solver, including clamping and nonmonotonic control points.
            for (unsigned iteration = 0; iteration < 8; iteration++) {
                double error = animation_cubic(start, out[0], in[0], end, parameter) - time;
                if (fabs(error) < 1e-10) break;
                double complement = 1 - parameter;
                double slope = 3 * complement * complement * (out[0] - start) +
                    6 * complement * parameter * (in[0] - out[0]) + 3 * parameter * parameter * (end - in[0]);
                if (fabs(slope) < 1e-10) break;
                parameter = fmin(1, fmax(0, parameter - error / slope));
            }
            result[c] = animation_cubic(a[c], out[1], in[1], b[c], parameter);
        } else {
            unsigned previous = key ? key - 1 : key + 1;
            unsigned next = key + 2 < track.keys ? key + 2 : key;
            double before = track.times[previous], after = track.times[next];
            if (!key) {
                if (track.ending_start == ANIMATION_ZERO_SLOPE) before = 2 * start - end;
                else if (track.ending_start == ANIMATION_WRAP) {
                    previous = track.keys - 2;
                    before = start + track.times[previous] - track.times[previous + 1];
                }
            }
            if (key + 2 == track.keys) {
                if (track.ending_end == ANIMATION_ZERO_SLOPE) { next = key + 1; after = 2 * end - start; }
                else if (track.ending_end == ANIMATION_WRAP) { next = 1; after = end + track.times[1] - track.times[0]; }
            }
            double wp = duration / (2 * (start - before)), wn = duration / (2 * (after - end));
            double t2 = t * t, t3 = t2 * t;
            // nonuniform Hermite coefficients and endpoint policies follow pinned Three.js (MIT).
            double p = track.values[(size_t)previous * track.components + c];
            double n = track.values[(size_t)next * track.components + c];
            result[c] = (-wp * t3 + 2 * wp * t2 - wp * t) * p +
                ((1 + wp) * t3 + (-1.5 - 2 * wp) * t2 + (-.5 + wp) * t + 1) * a[c] +
                ((-1 - wn) * t3 + (1.5 + wn) * t2 + .5 * t) * b[c] +
                (wn * t3 - wn * t2) * n;
        }
        if (!isfinite(result[c])) return false;
    }
    return true;
}

float css_interpolate_float(float a, float b, float t) {
    return a + (b - a) * t;
}
