#pragma once
#include "scene3d_math.hpp"
struct DomElement;
struct Pool;

// shared bounded source readers for projection and animation adapters; no authored data is retained.
const char* scene3d_text(DomElement* node, const char* key);
bool scene3d_number(DomElement* node, const char* key, float fallback, float* result);
bool scene3d_flag(DomElement* node, const char* key, bool fallback, bool* result);
bool scene3d_numbers(DomElement* node, const char* key, Pool* pool, float** values, unsigned* count, unsigned limit);
bool scene3d_components(DomElement* node, const char* key, float* data, unsigned count, bool* present);
bool scene3d_vector(DomElement* node, const char* key, Scene3dVec fallback, Scene3dVec* result);
bool scene3d_color(DomElement* node, const char* key, const char* fallback, float color[4]);
// A shader material's numeric uniform `name` as authored in its `uniforms` map: a number, an array of
// numbers, or a "#rrggbb" color as three linear numbers (`color` set). False when absent, a texture
// reference, malformed or longer than `capacity`.
bool scene3d_uniform_numbers(DomElement* material, const char* name, float* values, unsigned capacity,
    unsigned* count, bool* color);
