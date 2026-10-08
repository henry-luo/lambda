#pragma once
#include <stddef.h>

// SVG and 3D adapters supply typed values; sampling has no DOM or graphics dependency.
enum AnimationValueType { ANIMATION_NUMBER, ANIMATION_VECTOR, ANIMATION_COLOR,
    ANIMATION_QUATERNION, ANIMATION_BOOLEAN, ANIMATION_STRING };
enum AnimationInterpolation { ANIMATION_DISCRETE, ANIMATION_LINEAR,
    ANIMATION_SMOOTH, ANIMATION_BEZIER };
enum AnimationEnding { ANIMATION_ZERO_CURVATURE, ANIMATION_ZERO_SLOPE, ANIMATION_WRAP };
struct AnimationTrackView {
    const double* times;
    const double* values;
    const char* const* strings;
    const double* in_tangents; // absolute time/value control-point pairs per component
    const double* out_tangents;
    unsigned keys, components;
    AnimationValueType type;
    AnimationInterpolation interpolation;
    AnimationEnding ending_start = ANIMATION_ZERO_CURVATURE;
    AnimationEnding ending_end = ANIMATION_ZERO_CURVATURE;
};

unsigned animation_keyframe_segment(const double* times, unsigned count, double time);
bool animation_value_combine(double* left, const double* right, unsigned count,
    double left_weight, double right_weight);
bool animation_quaternion_normalize(double value[4]);
bool animation_quaternion_matrix(const double value[4], double matrix[16]); // row-major rotation
bool animation_quaternion_slerp(const double left[4], const double right[4], double weight, double result[4]);
bool animation_value_mix(AnimationValueType type, double* result, const double* value,
    unsigned count, double weight, bool additive = false);
bool animation_track_validate(const AnimationTrackView& track);
bool animation_track_sample(const AnimationTrackView& track, double time,
    double* result, const char** string_result = nullptr);
