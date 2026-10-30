#pragma once
#include "../animation_mixer.hpp"
#include "../../lib/arraylist.hpp"
#include <stddef.h>
struct DomElement;
struct Item;
struct Scene3dAnimationState;
struct Scene3dRuntime;
struct Scene3dPhysics;
struct Scene3dRootMotion {
    AnimationRootMotion translation, rotation;
    bool has_translation, has_rotation;
};
struct Scene3dAnimationReplayEvent {
    AnimationTimelineEvent timing;
    char clip[128];
};
struct Scene3dAnimationEvents {
    lam::ArrayList<Scene3dAnimationReplayEvent> values;
    char diagnostic[256] = {};
    Scene3dAnimationEvents() : values(MEM_CAT_RENDER, 0) {}
};

Scene3dAnimationState* scene3d_animation_create(DomElement* root, char* diagnostic, size_t capacity,
                                                Scene3dRuntime* runtime, bool replay = false,
                                                Scene3dPhysics* physics = nullptr);
bool scene3d_animation_adopt(Scene3dAnimationState* candidate, Scene3dAnimationState* previous,
                             Scene3dRuntime* runtime);
void scene3d_animation_destroy(Scene3dAnimationState* state);
bool scene3d_animation_matches(Scene3dAnimationState* state, DomElement* root);
bool scene3d_animation_refresh(Scene3dAnimationState* state, DomElement* root, char* diagnostic, size_t capacity,
                               Scene3dPhysics* physics = nullptr);
Item scene3d_animation_snapshot(Scene3dAnimationState* state);
Item scene3d_animation_commands(Scene3dAnimationState* state, Item batch);
// capture is native-only and can be installed solely on a quiet reconstruction owner.
bool scene3d_animation_collect(Scene3dAnimationState* state, Scene3dAnimationEvents* events);
Item scene3d_animation_replay_events(Scene3dAnimationState* state, Scene3dPhysics* physics, Item request);
uint64_t scene3d_animation_generation(Scene3dAnimationState* state);
// a template re-render rebuilt the scene's DOM: retarget playback to the corresponding nodes
void scene3d_animation_follow_rebuild(Scene3dAnimationState* state, struct DomNode* old_root, struct DomNode* new_root);
// feature presence is stable even before a declared track starts playing.
bool scene3d_animation_bound(Scene3dAnimationState* state, DomElement* node, const char* property);
bool scene3d_animation_value(Scene3dAnimationState* state, DomElement* node, const char* property,
                             AnimationValue* value);
AnimationActionState* scene3d_animation_action(Scene3dAnimationState* state, const char* clip);
bool scene3d_animation_update(Scene3dAnimationState* state, double delta);
bool scene3d_animation_seek(Scene3dAnimationState* state, double seconds);
bool scene3d_animation_automatic(Scene3dAnimationState* state, bool automatic);
bool scene3d_animation_sync_schedule(Scene3dAnimationState* state);
bool scene3d_animation_root_motion(Scene3dAnimationState* state, const char* source, DomElement** node,
                                   Scene3dRootMotion* motion);
bool scene3d_animation_root_value(Scene3dAnimationState* state, DomElement* node, Scene3dRootMotion* motion);
