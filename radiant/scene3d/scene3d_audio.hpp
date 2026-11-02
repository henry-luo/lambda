#pragma once
#include "scene3d.hpp"
struct Scene3dAnimationState;
struct Scene3dPhysics;
struct Scene3dAudio;
bool scene3d_audio_tag(DomElement* node);
Scene3dAudio* scene3d_audio_create(DomElement* root, Scene3dRuntime* runtime);
void scene3d_audio_destroy(Scene3dAudio* state);
bool scene3d_audio_prepare(Scene3dAudio* state, DomElement* root, Scene3dAnimationState* animation,
    Scene3dPhysics* physics, char* diagnostic, size_t capacity);
const char* scene3d_audio_sample(Scene3dAudio* state, double time);
void scene3d_audio_follow_rebuild(Scene3dAudio* state, struct DomNode* before, struct DomNode* after);
void scene3d_audio_reset(Scene3dAudio* state);
Item scene3d_audio_operate(Scene3dAudio* state, Item request);
const char* scene3d_audio_diagnostic(Scene3dAudio* state);
