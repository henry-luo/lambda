#include "scene3d_source.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lib/physics/physics_math.h"
#include "../../lib/hashmap.h"
#include "../../lib/mem.h"
#include "../../lib/str.h"
#include <float.h>

static PhysicsVec surface_vertex(const Scene3dGeometrySource &source, unsigned at) {
    unsigned index = source.index_count ? source.indices[at] : at;
    const float *p = source.positions + index * 3;
    return {p[0], p[1], p[2]};
}
bool scene3d_particle_surface_retain(Scene3dParticleSurface *surface) {
    if (!surface || surface->references == UINT32_MAX)
        return false;
    surface->references++;
    return true;
}
void scene3d_particle_surface_release(Scene3dParticleSurface *surface) {
    if (!surface || --surface->references)
        return;
    mem_free(surface->triangles);
    mem_free(surface);
}
bool scene3d_particle_surface_same(const Scene3dParticleSurface *a, const Scene3dParticleSurface *b) {
    return a == b || (a && b && a->count == b->count && a->hash == b->hash &&
                      !memcmp(a->triangles, b->triangles, a->count * sizeof(*a->triangles)));
}
bool scene3d_particle_surface_read(DomElement *root, DomElement *emitter, Pool *pool, Scene3dParticleSurface **result,
                                   char *diagnostic, size_t capacity) {
    *result = nullptr;
    auto fail = [&](const char *message) {
        str_copy(diagnostic, capacity, message, strlen(message));
        return false;
    };
    const char *id = scene3d_text(emitter, "surface");
    bool valid = false;
    auto *definition = id && *id && strlen(id) < 128 ? scene3d_find_source(root, id, &valid) : nullptr;
    if (!definition || !valid || strcmp(definition->local_name(), "geometry"))
        return fail("particle surface: missing, ambiguous or non-geometry surface reference");
    Scene3dGeometrySource source = {};
    if (!scene3d_read_geometry(definition, pool, &source, diagnostic, capacity, SCENE3D_PARTICLE_SURFACE_LIMIT * 3,
                               SCENE3D_PARTICLE_SURFACE_LIMIT * 3))
        return false;
    if (source.bone_index_count || source.morph_count)
        return fail("particle surface: deformation requires an explicit static geometry snapshot");
    unsigned indices = source.index_count ? source.index_count : source.vertex_count;
    if (!indices || indices % 3 || indices / 3 > SCENE3D_PARTICLE_SURFACE_LIMIT)
        return fail("particle surface: requires 1..4096 complete triangle faces");
    for (unsigned i = 0; i < source.vertex_count * 3; i++)
        if (fabs(source.positions[i]) > 1e6)
            return fail("particle surface: coordinates exceed the finite million-unit profile");
    unsigned count = indices / 3, kept = 0;
    auto *areas = (double *)pool_calloc(pool, count * sizeof(double));
    if (!areas)
        return fail("particle surface: area scratch allocation failed");
    double total = 0;
    for (unsigned i = 0; i < count; i++) {
        PhysicsVec a = surface_vertex(source, i * 3), b = surface_vertex(source, i * 3 + 1),
                   c = surface_vertex(source, i * 3 + 2);
        areas[i] = plen(pcross(psub(b, a), psub(c, a)));
        if (!isfinite(areas[i]) || !isfinite(total + areas[i]))
            return fail("particle surface: triangle area exceeds finite coordinates");
        if (areas[i] > 0) {
            total += areas[i];
            kept++;
        }
    }
    if (!kept || !(total > 0))
        return fail("particle surface: geometry has no positive-area faces");
    struct Owner {
        Scene3dParticleSurface *value;
        ~Owner() { scene3d_particle_surface_release(value); }
    } owner{(Scene3dParticleSurface *)mem_calloc(1, sizeof(Scene3dParticleSurface), MEM_CAT_RENDER)};
    if (!owner.value)
        return fail("particle surface: table allocation failed");
    auto *surface = owner.value;
    surface->references = 1;
    surface->count = kept;
    surface->records = (kept + SCENE3D_PARTICLE_SURFACE_COLUMNS - 1) / SCENE3D_PARTICLE_SURFACE_COLUMNS *
                       SCENE3D_PARTICLE_SURFACE_COLUMNS;
    surface->triangles = (Scene3dParticleSurfaceTriangle *)mem_calloc(
        surface->records, sizeof(Scene3dParticleSurfaceTriangle), MEM_CAT_RENDER);
    if (!surface->triangles)
        return fail("particle surface: triangle allocation failed");
    double prefix = 0;
    float before = 0;
    unsigned output = 0;
    for (unsigned i = 0; i < count; i++) {
        if (!areas[i])
            continue;
        auto &triangle = surface->triangles[output++];
        prefix += areas[i];
        float cdf = output == kept ? 1 : (float)(prefix / total);
        if (!(cdf > before) || cdf > 1)
            return fail("particle surface: positive face weights are not representable in the float area table");
        before = triangle.vertices[0][3] = cdf;
        for (unsigned vertex = 0; vertex < 3; vertex++) {
            auto point = surface_vertex(source, i * 3 + vertex);
            float *p = triangle.vertices[vertex];
            p[0] = (float)point.x;
            p[1] = (float)point.y;
            p[2] = (float)point.z;
            Scene3dVec value = {p[0], p[1], p[2]};
            if (output == 1 && vertex == 0)
                surface->lo = surface->hi = value;
            else
                scene3d_include(value, &surface->lo, &surface->hi);
        }
        auto a = surface_vertex(source, i * 3), b = surface_vertex(source, i * 3 + 1),
             c = surface_vertex(source, i * 3 + 2);
        auto normal = pmul(pcross(psub(b, a), psub(c, a)), 1 / areas[i]);
        triangle.normal[0] = (float)normal.x;
        triangle.normal[1] = (float)normal.y;
        triangle.normal[2] = (float)normal.z;
    }
    // the shared float table defines both backends' admitted geometry and area distribution.
    surface->hash = hashmap_xxhash3(surface->triangles, kept * sizeof(*surface->triangles), 0, 0);
    *result = surface;
    owner.value = nullptr;
    return true;
}
void scene3d_particle_surface_sample(const Scene3dParticleSurface *surface, double face, double u, double v,
                                     Scene3dVec *position, Scene3dVec *normal) {
    unsigned lo = 0, hi = surface->count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (face >= surface->triangles[mid].vertices[0][3])
            lo = mid + 1;
        else
            hi = mid;
    }
    const auto &triangle = surface->triangles[lo];
    double radius = sqrt(u), weights[] = {1 - radius, radius * (1 - v), radius * v};
    *position = {};
    float *out[] = {&position->x, &position->y, &position->z};
    for (unsigned axis = 0; axis < 3; axis++) {
        double value = 0;
        for (unsigned vertex = 0; vertex < 3; vertex++)
            value += weights[vertex] * triangle.vertices[vertex][axis];
        *out[axis] = (float)value;
    }
    *normal = {triangle.normal[0], triangle.normal[1], triangle.normal[2]};
}
