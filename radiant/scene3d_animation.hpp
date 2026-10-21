#pragma once
#include "animation_mixer.hpp"
#include <stddef.h>
struct DomElement;
struct Scene3dAnimationState;

Scene3dAnimationState* scene3d_animation_create(DomElement* root, char* diagnostic, size_t capacity);
void scene3d_animation_destroy(Scene3dAnimationState* state);
bool scene3d_animation_matches(Scene3dAnimationState* state, DomElement* root);
uint64_t scene3d_animation_generation(Scene3dAnimationState* state);
// a template re-render rebuilt the scene's DOM: retarget playback to the corresponding nodes
void scene3d_animation_follow_rebuild(Scene3dAnimationState* state, struct DomNode* old_root, struct DomNode* new_root);
bool scene3d_animation_value(Scene3dAnimationState* state, DomElement* node, const char* property, AnimationValue* value);
AnimationActionState* scene3d_animation_action(Scene3dAnimationState* state, const char* clip);
bool scene3d_animation_update(Scene3dAnimationState* state, double delta);
bool scene3d_animation_seek(Scene3dAnimationState* state, double seconds);
bool scene3d_animation_automatic(Scene3dAnimationState* state, bool automatic);
