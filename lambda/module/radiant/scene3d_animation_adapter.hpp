#pragma once

#include "../../core/data.hpp"

struct Scene3dAnimationState;
struct Scene3dPhysics;
Item scene3d_animation_snapshot(Scene3dAnimationState* state);
Item scene3d_animation_commands(Scene3dAnimationState* state, Item batch);
Item scene3d_animation_replay_events(Scene3dAnimationState* state, Scene3dPhysics* physics, Item request);
