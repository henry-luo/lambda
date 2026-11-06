#pragma once

#include "../../core/data.hpp"

struct Scene3dParticles;
Item scene3d_particles_trail_read(Scene3dParticles* state, Item request);
Item scene3d_particles_snapshot(Scene3dParticles* state);
Item scene3d_particles_commands(Scene3dParticles* state, Item batch);
Item scene3d_particles_events(Scene3dParticles* state, Item request);
