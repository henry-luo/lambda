#include "scene3d.hpp"
#include "scene3d_source.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lib/mem.h"

static_assert(sizeof(Scene3dParticleTrailPoint) == 4 * sizeof(float), "particle trail RGBA32F point layout");

bool scene3d_particle_trail_read(DomElement *node, Scene3dParticleTrailDesc *d) {
    unsigned segments;
    if (!node || !d || node->first_child ||
        !scene3d_unsigned(node, "segments", 16, SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT - 1, &segments) || !segments ||
        !scene3d_number(node, "lifetime", .5f, &d->lifetime) || !(d->lifetime > 0) || d->lifetime > 1e6f ||
        !scene3d_number(node, "width", .05f, &d->width) || !(d->width > 0) || d->width > 1e6f ||
        !scene3d_flag(node, "fade", true, &d->fade)) return false;
    d->samples = segments + 1;
    return true;
}
bool scene3d_particle_trail_same(const Scene3dParticleTrailDesc &a, const Scene3dParticleTrailDesc &b) {
    return a.samples == b.samples && a.lifetime == b.lifetime && a.width == b.width && a.fade == b.fade;
}
unsigned scene3d_particle_trail_records(unsigned capacity) {
    unsigned columns = capacity < SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT ? capacity : SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT;
    return columns ? (capacity + columns - 1) / columns * columns : 0;
}
Scene3dParticleTrailState *scene3d_particle_trail_create(unsigned capacity, unsigned samples, bool gpu) {
    if (!capacity || capacity > SCENE3D_INSTANCE_LIMIT || samples < 2 || samples > SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT)
        return nullptr;
    auto *trail = (Scene3dParticleTrailState *)mem_calloc(1, sizeof(Scene3dParticleTrailState), MEM_CAT_RENDER);
    if (!trail) return nullptr;
    trail->capacity = capacity; trail->samples = samples;
    trail->gpu = gpu;
    trail->columns = capacity < SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT ? capacity : SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT;
    trail->records = scene3d_particle_trail_records(capacity); trail->rows = trail->records / trail->columns;
    size_t points = (size_t)trail->records * samples;
    for (auto &buffer : trail->points) {
        buffer = (Scene3dParticleTrailPoint *)mem_calloc(points, sizeof(Scene3dParticleTrailPoint), MEM_CAT_RENDER);
        if (buffer) for (size_t i = 0; i < points; i++) buffer[i].age = -1;
    }
    trail->slots = (float *)mem_calloc(capacity, sizeof(float), MEM_CAT_RENDER);
    if (!gpu) trail->styles = (float *)mem_calloc(trail->records * 12, sizeof(float), MEM_CAT_RENDER);
    if (!trail->points[0] || !trail->points[1] || !trail->slots || (!gpu && !trail->styles)) {
        scene3d_particle_trail_destroy(trail); return nullptr;
    }
    trail->bytes = sizeof(*trail) + points * 2 * sizeof(Scene3dParticleTrailPoint) +
                   (capacity + (gpu ? 0 : trail->records * 12)) * sizeof(float);
    return trail;
}
void scene3d_particle_trail_destroy(Scene3dParticleTrailState *trail) {
    if (!trail) return;
    for (auto *buffer : trail->points) mem_free(buffer);
    mem_free(trail->slots); mem_free(trail->styles); mem_free(trail);
}
void scene3d_particle_trail_begin(Scene3dParticleTrailState *trail) {
    if (!trail) return;
    memcpy(trail->bounds[1], trail->bounds[0], sizeof(trail->bounds[0]));
    if (trail->gpu) return;
    // invalidate staging first: dead or recycled slots cannot retain another head's ribbon.
    for (size_t i = 0; i < (size_t)trail->records * trail->samples; i++) trail->points[1][i].age = -1;
}
void scene3d_particle_trail_append(Scene3dParticleTrailState *trail, unsigned slot, Scene3dVec position,
                                   bool newborn, double timestep) {
    if (!trail || slot >= trail->capacity) return;
    auto *next = trail->points[1] + slot * trail->samples;
    const auto *previous = trail->points[0] + slot * trail->samples;
    if (!newborn) for (unsigned i = 0; i < trail->samples; i++) {
        if (previous[i].age < 0) continue;
        double age = previous[i].age + timestep;
        // use relative ages, retaining all representable authored windows across presentation edits.
        if (age <= 1e6) { next[i] = previous[i]; next[i].age = (float)age; }
    }
    next[(trail->head + 1) % trail->samples] = {position, 0};
}
void scene3d_particle_trail_commit(Scene3dParticleTrailState *trail) {
    if (!trail) return;
    if (!trail->gpu) {
        auto *retired = trail->points[0]; trail->points[0] = trail->points[1]; trail->points[1] = retired;
    }
    memcpy(trail->bounds[0], trail->bounds[1], sizeof(trail->bounds[0]));
    trail->head = (trail->head + 1) % trail->samples;
}
unsigned scene3d_particle_trail_samples(const Scene3dParticleTrailState *trail, unsigned slot,
                                        Scene3dParticleTrailPoint *points) {
    if (!trail || slot >= trail->capacity || !points) return 0;
    unsigned count = 0;
    for (unsigned i = 0; i < trail->samples; i++) {
        const auto &point = trail->points[0][slot * trail->samples + (trail->head + trail->samples - i) % trail->samples];
        if (point.age >= 0) points[count++] = point;
    }
    return count;
}
