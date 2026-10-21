#pragma once
// The common <character> schema's converters (vibe/Lambda_Pkg_Motion.md §6-§7):
// each format converts once into joints with rest poses, baked clips whose
// channels name joints, and an optional skinned body.
#include "input-scene3d.hpp"

namespace lambda {

// [x, y, z, w] quaternions and column-major affine matrices in double precision
struct CharacterQuat { double x, y, z, w; };

inline CharacterQuat character_quat_mul(const CharacterQuat& a, const CharacterQuat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
// a rotation of `degrees` about axis 0, 1 or 2
inline CharacterQuat character_quat_axis(unsigned axis, double degrees) {
    double half = degrees * M_PI / 360.0, s = sin(half);
    return {axis == 0 ? s : 0.0, axis == 1 ? s : 0.0, axis == 2 ? s : 0.0, cos(half)};
}
inline CharacterQuat character_quat_normalized(CharacterQuat q) {
    double length = sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(length > 0)) return {0, 0, 0, 1};
    return {q.x / length, q.y / length, q.z / length, q.w / length};
}
// keep consecutive keys on one hemisphere, so linear interpolation and SLERP
// take the short way between them
inline CharacterQuat character_quat_continue(const CharacterQuat& previous, CharacterQuat q) {
    if (previous.x * q.x + previous.y * q.y + previous.z * q.z + previous.w * q.w < 0) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    return q;
}

bool character_from_bvh(SceneAsset& a, Item raw, const char* clip_name);

}  // namespace lambda
