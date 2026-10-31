#include "scene3d.hpp"
#include "scene3d_source.hpp"
#include "../../lib/mem.h"
#include <math.h>
#include <initializer_list>

static constexpr unsigned PARTICLE_GPU_COLUMNS = 128, PARTICLE_GPU_BIRTH_TEXELS = 9;
static_assert(sizeof(Scene3dParticleGpuBirth) == PARTICLE_GPU_BIRTH_TEXELS * 4 * sizeof(float),
              "GPU birth atlas layout");
static_assert(sizeof(Scene3dParticleGpuMetadata) == 4 * sizeof(float), "GPU metadata layout");
static_assert(sizeof(Scene3dParticleSurfaceTriangle) == 16 * sizeof(float), "GPU surface table layout");
struct Scene3dParticleGpu {
    Scene3dParticleGpuOps ops;
    Scene3dParticleGpu *next;
    NativeGlContext *graphics;
    NativeGlResource triangle, program, metadata_target, birth_target, surface_target, positions[2], velocities[2];
    const Scene3dParticleSurfaces *surfaces;
    const Scene3dParticleSurface *uploaded_surfaces[SCENE3D_PARTICLE_SURFACE_VERSIONS];
    Scene3dParticleGpuMetadata *metadata[2];
    Scene3dParticleGpuBirth *births[2];
    float *read_positions, *read_velocities;
    Scene3dParticleGpuControls controls, staged_controls;
    unsigned capacity, columns, rows, records, current;
    uint64_t readbacks, passes, recoveries;
    bool ready, ever_ready, staged, births_changed, birth_uploaded, checkpoint;
};

const char *scene3d_particle_gpu_fragment() {
    return R"GLSL(#version 330 core
uniform sampler2D particle_metadata, particle_births, particle_surfaces;
uniform vec4 particle_seed;
uniform vec3 particle_gravity;
uniform float particle_drag, particle_output, particle_curve_count, particle_rotation_count;
uniform vec4 particle_curve[16], particle_rotation[16];
out vec4 fragment_color;
uvec2 add64(uvec2 a, uvec2 b) {
    uint lo = a.x + b.x;
    return uvec2(lo, a.y + b.y + uint(lo < a.x));
}
// 16-bit partial products preserve unsigned 64-bit keys on GLSL 330 devices.
uvec2 multiply64(uvec2 a, uvec2 b) {
    uint a0 = a.x & 65535u, a1 = a.x >> 16u, b0 = b.x & 65535u, b1 = b.x >> 16u;
    uint p0 = a0 * b0, p1 = a0 * b1, p2 = a1 * b0, p3 = a1 * b1;
    uint carry = (p0 >> 16u) + (p1 & 65535u) + (p2 & 65535u);
    return uvec2((carry << 16u) | (p0 & 65535u),
                 p3 + (p1 >> 16u) + (p2 >> 16u) + (carry >> 16u) + a.x * b.y + a.y * b.x);
}
uvec2 shift64(uvec2 a, uint n) { return uvec2((a.x >> n) | (a.y << (32u - n)), a.y >> n); }
uvec2 mix64(uvec2 a) {
    a = multiply64(a ^ shift64(a, 30u), uvec2(0x1ce4e5b9u, 0xbf58476du));
    a = multiply64(a ^ shift64(a, 27u), uvec2(0x133111ebu, 0x94d049bbu));
    return a ^ shift64(a, 31u);
}
uvec2 words64(vec4 a) { return uvec2(uint(a.x) | (uint(a.y) << 16u), uint(a.z) | (uint(a.w) << 16u)); }
float random_unit(uvec2 sequence, uint channel) {
    uvec2 golden = uvec2(0x7f4a7c15u, 0x9e3779b9u);
    uvec2 key = mix64(add64(words64(particle_seed) ^ sequence, golden));
    key = mix64(add64(key ^ uvec2(channel, 0u), add64(golden, golden)));
    return float(mix64(key).y >> 8u) * 0.000000059604644775390625;
}
vec4 birth(ivec2 cell, int lane) { return texelFetch(particle_births, ivec2(cell.x * 9 + lane, cell.y), 0); }
vec4 surface_point(int bank, int triangle, int lane) {
    return texelFetch(particle_surfaces, ivec2((triangle % 128) * 4 + lane, bank * 32 + triangle / 128), 0);
}
int surface_face(int bank, int count, float selector) {
    int lo = 0, hi = count;
    for (int iteration = 0; iteration < 13 && lo < hi; iteration++) {
        int mid = lo + (hi - lo) / 2;
        if (selector >= surface_point(bank, mid, 0).w) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
vec3 curve_value(vec4 keys[16], float count, float phase) {
    vec3 value = keys[0].yzw;
    for (int i = 1; i < 16; i++) {
        if (i >= int(count))
            break;
        vec4 a = keys[i - 1], b = keys[i];
        value = b.yzw;
        if (phase <= b.x)
            return mix(a.yzw, b.yzw, clamp((phase - a.x) / (b.x - a.x), 0.0, 1.0));
    }
    return value;
}
void main() {
    ivec2 cell = ivec2(gl_FragCoord.xy);
    bool pose = particle_output < 0.5;
    int lane = cell.x % 2;
    if (pose)
        cell.x /= 2;
    vec4 metadata = texelFetch(particle_metadata, cell, 0);
    if (metadata.w < 0.5) {
        fragment_color = vec4(0.0);
        return;
    }
    float phase = clamp(metadata.x / metadata.y, 0.0, 1.0);
    if (pose && lane == 1) {
        fragment_color = vec4(curve_value(particle_rotation, particle_rotation_count, phase), 0.0);
        return;
    }
    vec4 shape = birth(cell, 1), velocity_radius = birth(cell, 2), cone = birth(cell, 3), anchor = birth(cell, 4);
    vec3 origin = anchor.xyz, velocity = birth(cell, 5).xyz;
    if (cone.w < 0.5) {
        uvec2 sequence = words64(birth(cell, 0));
        origin = vec3(0.0);
        velocity = velocity_radius.xyz;
        float angle = 6.283185307179586 * random_unit(sequence, 2u);
        if (cone.z > 0.0 || shape.x == 2.0) {
            float y = 2.0 * random_unit(sequence, 1u) - 1.0;
            float radius = sqrt(max(0.0, 1.0 - y * y));
            vec3 direction = vec3(radius * cos(angle), y, radius * sin(angle));
            velocity += direction * cone.z;
            if (shape.x == 2.0)
                origin = direction * (velocity_radius.w * pow(random_unit(sequence, 3u), 1.0 / 3.0));
        }
        if (shape.x == 1.0)
            origin =
                (vec3(random_unit(sequence, 3u), random_unit(sequence, 4u), random_unit(sequence, 5u)) * 2.0 - 1.0) *
                shape.yzw * 0.5;
        if (shape.x == 4.0) {
            int bank = int(shape.y), face = surface_face(bank, int(shape.z), random_unit(sequence, 6u));
            float radius = sqrt(random_unit(sequence, 7u)), v = random_unit(sequence, 8u);
            origin = surface_point(bank, face, 0).xyz * (1.0 - radius) +
                     surface_point(bank, face, 1).xyz * (radius * (1.0 - v)) +
                     surface_point(bank, face, 2).xyz * (radius * v);
            velocity += surface_point(bank, face, 3).xyz * shape.w;
        }
        if (shape.x == 3.0) {
            float y = 1.0 - random_unit(sequence, 3u) * (1.0 - cos(cone.x));
            float radius = sqrt(max(0.0, 1.0 - y * y));
            velocity += vec3(radius * cos(angle), y, radius * sin(angle)) * cone.y;
        }
    }
    if (cone.w < 0.5) {
        vec4 point = vec4(origin, 1.0), vector = vec4(velocity, 0.0);
        vec4 a = birth(cell, 6), b = birth(cell, 7), c = birth(cell, 8);
        origin = vec3(dot(a, point), dot(b, point), dot(c, point));
        velocity = vec3(dot(a, vector), dot(b, vector), dot(c, vector));
    }
    float time = max(0.0, metadata.x - anchor.w), x = particle_drag * time;
    float decay = exp(-x), travel, acceleration_travel;
    // avoid cancellation in the drag integrals near zero.
    if (abs(x) < 0.01) {
        travel = time * (1.0 + x * (-0.5 + x * (1.0 / 6.0 + x * (-1.0 / 24.0 + x / 120.0))));
        acceleration_travel =
            time * time * (0.5 + x * (-1.0 / 6.0 + x * (1.0 / 24.0 + x * (-1.0 / 120.0 + x / 720.0))));
    } else {
        travel = (1.0 - decay) / particle_drag;
        acceleration_travel = (time - travel) / particle_drag;
    }
    vec3 position = origin + velocity * travel + particle_gravity * acceleration_travel;
    vec3 speed = velocity * decay + particle_gravity * travel;
    float size = metadata.z * curve_value(particle_curve, particle_curve_count, phase).x;
    fragment_color = particle_output < 0.5 ? vec4(position, size) : vec4(speed, metadata.x);
}
)GLSL";
}
static void particle_gpu_release(Scene3dParticleGpu *gpu) {
    if (gpu->graphics) {
        for (auto group : {gpu->positions, gpu->velocities})
            for (unsigned i = 0; i < 2; i++)
                native_gl_release(gpu->graphics, group[i]);
        native_gl_release(gpu->graphics, gpu->metadata_target);
        native_gl_release(gpu->graphics, gpu->birth_target);
        native_gl_release(gpu->graphics, gpu->surface_target);
    }
    gpu->graphics = nullptr;
    gpu->metadata_target = {};
    gpu->birth_target = {};
    gpu->surface_target = {};
    memset(gpu->uploaded_surfaces, 0, sizeof(gpu->uploaded_surfaces));
    for (auto group : {gpu->positions, gpu->velocities})
        for (unsigned i = 0; i < 2; i++)
            group[i] = {};
    gpu->ready = false;
    gpu->birth_uploaded = false;
}
static bool particle_gpu_uniform(Scene3dParticleGpu *gpu, const char *name, const float *values, unsigned components,
                                 unsigned count = 1) {
    auto uniform = native_gl_uniform(gpu->graphics, gpu->program, name);
    return native_gl_uniform_set(gpu->graphics, uniform, values, components, count);
}
void scene3d_particle_gpu_surface_changed(Scene3dParticleGpu *gpu, unsigned slot) {
    if (gpu && slot < SCENE3D_PARTICLE_SURFACE_VERSIONS)
        gpu->uploaded_surfaces[slot] = nullptr;
}
static bool particle_gpu_surfaces(Scene3dParticleGpu *gpu) {
    if (!gpu->surfaces)
        return true;
    for (unsigned slot = 0; slot < SCENE3D_PARTICLE_SURFACE_VERSIONS; slot++) {
        const auto *surface = gpu->surfaces->values[slot];
        if (!surface || gpu->uploaded_surfaces[slot] == surface)
            continue;
        if (!gpu->surface_target.id)
            gpu->surface_target =
                native_gl_float_target(gpu->graphics, SCENE3D_PARTICLE_SURFACE_COLUMNS * 4,
                                       SCENE3D_PARTICLE_SURFACE_ROWS * SCENE3D_PARTICLE_SURFACE_VERSIONS, false, true);
        // banks referenced by committed births cannot be reused by an incompatible description edit.
        if (!gpu->surface_target.id ||
            !native_gl_float_target_update_region(
                gpu->graphics, gpu->surface_target, 0, slot * SCENE3D_PARTICLE_SURFACE_ROWS,
                SCENE3D_PARTICLE_SURFACE_COLUMNS * 4, surface->records / SCENE3D_PARTICLE_SURFACE_COLUMNS,
                (const float *)surface->triangles, surface->records * (sizeof(*surface->triangles) / sizeof(float))))
            return false;
        gpu->uploaded_surfaces[slot] = surface;
    }
    return true;
}
static bool particle_gpu_compute(Scene3dParticleGpu *gpu, unsigned target, const Scene3dParticleGpuControls &controls) {
    if (!particle_gpu_surfaces(gpu) || gpu->passes > UINT64_MAX - 2)
        return false;
    float count = (float)controls.curve_count, rotation_count = (float)controls.rotation_count;
    if (!particle_gpu_uniform(gpu, "particle_seed", controls.seed, 4) ||
        !particle_gpu_uniform(gpu, "particle_gravity", controls.gravity, 3) ||
        !particle_gpu_uniform(gpu, "particle_drag", &controls.drag, 1) ||
        !particle_gpu_uniform(gpu, "particle_curve_count", &count, 1) ||
        !particle_gpu_uniform(gpu, "particle_curve[0]", controls.curve[0], 4, SCENE3D_PARTICLE_CURVE_LIMIT) ||
        !particle_gpu_uniform(gpu, "particle_rotation_count", &rotation_count, 1) ||
        !particle_gpu_uniform(gpu, "particle_rotation[0]", controls.rotation[0], 4, SCENE3D_PARTICLE_CURVE_LIMIT))
        return false;
    for (unsigned component = 0; component < 2; component++) {
        float output = (float)component;
        auto surface = component ? gpu->velocities[target] : gpu->positions[target];
        unsigned width = gpu->columns * (component ? 1 : 2);
        if (!native_gl_begin_region(gpu->graphics, surface, 0, 0, width, gpu->rows, nullptr) ||
            !particle_gpu_uniform(gpu, "particle_output", &output, 1))
            return false;
        NativeGlDraw draw = {gpu->program, gpu->triangle, 3, 1, false, false, 2, false};
        draw.bindings[draw.binding_count++] = {"particle_metadata", gpu->metadata_target, NATIVE_GL_BIND_FLOAT};
        draw.bindings[draw.binding_count++] = {"particle_births", gpu->birth_target, NATIVE_GL_BIND_FLOAT};
        draw.bindings[draw.binding_count++] = {"particle_surfaces",
                                               gpu->surface_target.id ? gpu->surface_target : gpu->metadata_target,
                                               NATIVE_GL_BIND_FLOAT};
        if (!native_gl_draw(gpu->graphics, &draw))
            return false;
        gpu->passes++;
    }
    return true;
}
static bool particle_gpu_ensure(Scene3dParticleGpu *gpu) {
    if (gpu->ready)
        return true;
    NativeGlContext *graphics = nullptr;
    NativeGlResource triangle = {}, program = {};
    if (!gpu->ops.acquire || !gpu->ops.acquire(gpu->ops.owner, &graphics, &triangle, &program))
        return false;
    gpu->graphics = graphics;
    gpu->triangle = triangle;
    gpu->program = program;
    gpu->metadata_target = native_gl_float_target(graphics, gpu->columns, gpu->rows, false, true);
    gpu->birth_target =
        native_gl_float_target(graphics, gpu->columns * PARTICLE_GPU_BIRTH_TEXELS, gpu->rows, false, true);
    for (unsigned i = 0; i < 2; i++) {
        // each pose is two adjacent RGBA texels: position/size, then local Euler rotation.
        gpu->positions[i] = native_gl_float_target(graphics, gpu->columns * 2, gpu->rows, false, true);
        gpu->velocities[i] = native_gl_float_target(graphics, gpu->columns, gpu->rows, false, true);
    }
    bool valid = gpu->metadata_target.id && gpu->birth_target.id;
    for (auto group : {gpu->positions, gpu->velocities})
        for (auto value : {group[0], group[1]})
            valid &= value.id != 0;
    if (!valid || !native_gl_float_target_update(graphics, gpu->metadata_target, (const float *)gpu->metadata[0]) ||
        !native_gl_float_target_update(graphics, gpu->birth_target, (const float *)gpu->births[0]) ||
        !particle_gpu_compute(gpu, gpu->current, gpu->controls)) {
        particle_gpu_release(gpu);
        return false;
    }
    gpu->ready = true;
    gpu->birth_uploaded = true;
    if (gpu->ever_ready) {
        if (gpu->recoveries == UINT64_MAX) {
            particle_gpu_release(gpu);
            return false;
        }
        gpu->recoveries++;
    }
    gpu->ever_ready = true;
    return true;
}
Scene3dParticleGpu *scene3d_particle_gpu_create(unsigned capacity, const Scene3dParticleGpuOps &ops,
                                                const Scene3dParticleSurfaces *surfaces) {
    if (!capacity || capacity > SCENE3D_INSTANCE_LIMIT || !ops.acquire || !ops.registry)
        return nullptr;
    auto *gpu = (Scene3dParticleGpu *)mem_calloc(1, sizeof(Scene3dParticleGpu), MEM_CAT_RENDER);
    if (!gpu)
        return nullptr;
    gpu->ops = ops;
    gpu->surfaces = surfaces;
    gpu->next = *ops.registry;
    *ops.registry = gpu;
    gpu->capacity = capacity;
    gpu->columns = capacity < PARTICLE_GPU_COLUMNS ? capacity : PARTICLE_GPU_COLUMNS;
    gpu->rows = (capacity + gpu->columns - 1) / gpu->columns;
    gpu->records = gpu->columns * gpu->rows;
    for (unsigned i = 0; i < 2; i++) {
        gpu->metadata[i] =
            (Scene3dParticleGpuMetadata *)mem_calloc(gpu->records, sizeof(Scene3dParticleGpuMetadata), MEM_CAT_RENDER);
        gpu->births[i] =
            (Scene3dParticleGpuBirth *)mem_calloc(gpu->records, sizeof(Scene3dParticleGpuBirth), MEM_CAT_RENDER);
    }
    gpu->read_positions = (float *)mem_calloc(gpu->records * 8, sizeof(float), MEM_CAT_RENDER);
    gpu->read_velocities = (float *)mem_calloc(gpu->records * 4, sizeof(float), MEM_CAT_RENDER);
    if (!gpu->metadata[0] || !gpu->metadata[1] || !gpu->births[0] || !gpu->births[1] || !gpu->read_positions ||
        !gpu->read_velocities || !particle_gpu_ensure(gpu)) {
        scene3d_particle_gpu_destroy(gpu);
        return nullptr;
    }
    return gpu;
}
void scene3d_particle_gpu_destroy(Scene3dParticleGpu *gpu) {
    if (!gpu)
        return;
    if (gpu->ops.registry) {
        auto **link = gpu->ops.registry;
        while (*link && *link != gpu)
            link = &(*link)->next;
        if (*link)
            *link = gpu->next;
    }
    particle_gpu_release(gpu);
    for (unsigned i = 0; i < 2; i++) {
        mem_free(gpu->metadata[i]);
        mem_free(gpu->births[i]);
    }
    mem_free(gpu->read_positions);
    mem_free(gpu->read_velocities);
    mem_free(gpu);
}
void scene3d_particle_gpu_owner_lost(Scene3dParticleGpu **registry, bool retire) {
    if (!registry)
        return;
    auto *gpu = *registry;
    while (gpu) {
        auto *next = gpu->next;
        // private replays borrow this context too; invalidate every borrower before freeing it.
        particle_gpu_release(gpu);
        gpu->staged = false;
        if (retire) {
            gpu->ops = {};
            gpu->next = nullptr;
        }
        gpu = next;
    }
    if (retire)
        *registry = nullptr;
}
bool scene3d_particle_gpu_upload(Scene3dParticleGpu *gpu, bool births_changed, Scene3dParticleGpuUpload *upload) {
    if (!gpu || !upload || !particle_gpu_ensure(gpu))
        return false;
    memset(gpu->metadata[1], 0, gpu->records * sizeof(Scene3dParticleGpuMetadata));
    gpu->births_changed = births_changed;
    gpu->staged = false;
    gpu->checkpoint = false;
    if (births_changed)
        memcpy(gpu->births[1], gpu->births[0], gpu->records * sizeof(Scene3dParticleGpuBirth));
    *upload = {gpu->metadata[1], births_changed ? gpu->births[1] : gpu->births[0]};
    return true;
}
bool scene3d_particle_gpu_stage(Scene3dParticleGpu *gpu, const Scene3dParticleGpuControls &controls) {
    if (!gpu || !gpu->ready || controls.curve_count < 2 || controls.curve_count > SCENE3D_PARTICLE_CURVE_LIMIT ||
        controls.rotation_count < 2 || controls.rotation_count > SCENE3D_PARTICLE_CURVE_LIMIT)
        return false;
    if (!native_gl_float_target_update(gpu->graphics, gpu->metadata_target, (const float *)gpu->metadata[1]))
        return false;
    auto *births = gpu->births[gpu->births_changed ? 1 : 0];
    bool previous_upload_valid = gpu->birth_uploaded;
    // a rejected stage may have replaced birth texels; only commit makes that upload authoritative.
    gpu->birth_uploaded = false;
    for (unsigned row = 0; row < gpu->rows; row++) {
        const auto *before = gpu->births[0] + row * gpu->columns;
        const auto *after = births + row * gpu->columns;
        if (previous_upload_valid && (!gpu->births_changed || !memcmp(before, after, gpu->columns * sizeof(*after))))
            continue;
        if (!native_gl_float_target_update_region(gpu->graphics, gpu->birth_target, 0, row,
                                                  gpu->columns * PARTICLE_GPU_BIRTH_TEXELS, 1, (const float *)after,
                                                  gpu->columns * PARTICLE_GPU_BIRTH_TEXELS * 4))
            return false;
    }
    if (!particle_gpu_compute(gpu, 1 - gpu->current, controls))
        return false;
    gpu->staged_controls = controls;
    gpu->staged = true;
    return true;
}
void scene3d_particle_gpu_commit(Scene3dParticleGpu *gpu) {
    if (!gpu || !gpu->staged)
        return;
    auto *previous = gpu->metadata[0];
    gpu->metadata[0] = gpu->metadata[1];
    gpu->metadata[1] = previous;
    if (gpu->births_changed) {
        auto *before = gpu->births[0];
        gpu->births[0] = gpu->births[1];
        gpu->births[1] = before;
    }
    gpu->current = 1 - gpu->current;
    gpu->controls = gpu->staged_controls;
    gpu->staged = false;
    gpu->birth_uploaded = true;
}
static bool particle_gpu_read_rows(Scene3dParticleGpu *gpu, const bool *selected) {
    for (unsigned row = 0; row < gpu->rows; row++) {
        if (!selected[row])
            continue;
        if (gpu->readbacks > UINT64_MAX - 2)
            return false;
        if (!native_gl_float_target_read(gpu->graphics, gpu->positions[gpu->current], 0, row, gpu->columns * 2, 1,
                                         gpu->read_positions + row * gpu->columns * 8, gpu->columns * 8) ||
            !native_gl_float_target_read(gpu->graphics, gpu->velocities[gpu->current], 0, row, gpu->columns, 1,
                                         gpu->read_velocities + row * gpu->columns * 4, gpu->columns * 4))
            return false;
        gpu->readbacks += 2;
    }
    return true;
}
bool scene3d_particle_gpu_sample(Scene3dParticleGpu *gpu, const unsigned *slots, unsigned count,
                                 Scene3dParticleSample *samples) {
    if (!gpu || count > 64 || (count && (!slots || !samples)) || !particle_gpu_ensure(gpu))
        return false;
    bool selected[PARTICLE_GPU_COLUMNS] = {};
    for (unsigned i = 0; i < count; i++) {
        if (slots[i] >= gpu->capacity || gpu->metadata[0][slots[i]].active < .5f)
            return false;
        selected[slots[i] / gpu->columns] = true;
    }
    if (!particle_gpu_read_rows(gpu, selected))
        return false;
    for (unsigned i = 0; i < count; i++) {
        const float *p = gpu->read_positions + slots[i] * 8, *v = gpu->read_velocities + slots[i] * 4;
        for (unsigned c = 0; c < 4; c++)
            if (!isfinite(p[c]) || !isfinite(v[c]))
                return false;
        for (unsigned c = 4; c < 8; c++)
            if (!isfinite(p[c]))
                return false;
        samples[i].rotation = {p[4], p[5], p[6]};
        samples[i].position = {p[0], p[1], p[2]};
        samples[i].velocity = {v[0], v[1], v[2]};
        samples[i].size = p[3];
    }
    return true;
}
bool scene3d_particle_gpu_checkpoint(Scene3dParticleGpu *gpu) {
    if (!gpu || !particle_gpu_ensure(gpu))
        return false;
    bool selected[PARTICLE_GPU_COLUMNS] = {};
    for (unsigned slot = 0; slot < gpu->capacity; slot++)
        if (gpu->metadata[0][slot].active > .5f)
            selected[slot / gpu->columns] = true;
    if (!particle_gpu_read_rows(gpu, selected))
        return false;
    memcpy(gpu->births[1], gpu->births[0], gpu->records * sizeof(Scene3dParticleGpuBirth));
    for (unsigned slot = 0; slot < gpu->capacity; slot++) {
        const auto &metadata = gpu->metadata[0][slot];
        if (metadata.active < .5f)
            continue;
        auto &birth = gpu->births[1][slot];
        const float *p = gpu->read_positions + slot * 8, *v = gpu->read_velocities + slot * 4;
        for (unsigned c = 0; c < 3; c++) {
            if (!isfinite(p[c]) || !isfinite(v[c]))
                return false;
            birth.anchor_position_age[c] = p[c];
            birth.anchor_velocity[c] = v[c];
        }
        birth.anchor_position_age[3] = metadata.age;
        birth.cone_spread_anchor[3] = 1;
    }
    gpu->checkpoint = true;
    return true;
}
void scene3d_particle_gpu_checkpoint_commit(Scene3dParticleGpu *gpu) {
    if (!gpu || !gpu->checkpoint)
        return;
    auto *before = gpu->births[0];
    gpu->births[0] = gpu->births[1];
    gpu->births[1] = before;
    gpu->birth_uploaded = false;
    gpu->checkpoint = false;
}
bool scene3d_particle_gpu_stats(Scene3dParticleGpu *gpu, Scene3dParticleGpuStats *stats) {
    if (!gpu || !stats || !particle_gpu_ensure(gpu))
        return false;
    *stats = {gpu->positions[gpu->current],
              gpu->birth_target,
              gpu->readbacks,
              gpu->passes,
              gpu->recoveries,
              (uint64_t)gpu->records * (sizeof(Scene3dParticleGpuBirth) + 7 * sizeof(Scene3dParticleGpuMetadata)) +
                  (gpu->surface_target.id
                       ? (uint64_t)SCENE3D_PARTICLE_SURFACE_COLUMNS * 4 * SCENE3D_PARTICLE_SURFACE_ROWS *
                             SCENE3D_PARTICLE_SURFACE_VERSIONS * 4 * sizeof(float)
                       : 0),
              sizeof(*gpu) + (uint64_t)gpu->records * (2 * sizeof(Scene3dParticleGpuBirth) +
                                                       2 * sizeof(Scene3dParticleGpuMetadata) + 12 * sizeof(float))};
    return true;
}
