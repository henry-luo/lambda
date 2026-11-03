#include "scene3d.hpp"
#include "scene3d_source.hpp"
#include "../animation_value.hpp"
#include "../../lib/mem.h"
#include "../../lib/physics/physics_math.h"
#include <math.h>
#include <limits.h>
#include <initializer_list>

static constexpr unsigned PARTICLE_GPU_COLUMNS = 128, PARTICLE_GPU_BIRTH_TEXELS = 9;
static_assert(sizeof(Scene3dParticleGpuBirth) == PARTICLE_GPU_BIRTH_TEXELS * 4 * sizeof(float),
              "GPU birth atlas layout");
static_assert(sizeof(Scene3dParticleGpuMetadata) == 4 * sizeof(float), "GPU metadata layout");
static_assert(sizeof(Scene3dParticleSurfaceTriangle) == 16 * sizeof(float), "GPU surface table layout");
struct ParticleGpuTick { double time, start, step; unsigned trail_head; };
struct Scene3dParticleGpu {
    Scene3dParticleGpuOps ops;
    Scene3dParticleGpu *next;
    NativeGlContext *graphics;
    NativeGlResource triangle, program, metadata_target, birth_target, surface_target, positions[2], velocities[2];
    NativeGlResource trails[2];
    Scene3dParticleTrailState *trail;
    double trail_anchor_time;
    const Scene3dParticleSurfaces *surfaces;
    const Scene3dParticleSurface *uploaded_surfaces[SCENE3D_PARTICLE_SURFACE_VERSIONS];
    Scene3dParticleGpuMetadata *metadata[2];
    Scene3dParticleGpuBirth *births[2];
    float *read_positions, *read_velocities;
    double *birth_times[2];
    ParticleGpuTick *history;
    double time, staged_time, staged_step;
    unsigned history_head, history_count, staged_drop;
    const char *diagnostic;
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
uniform vec4 particle_noise, particle_sheet, particle_sheet_time;
uniform float particle_drag, particle_output, particle_curve_count, particle_rotation_count, particle_color_count, particle_opacity_count, particle_velocity_count;
uniform vec4 particle_curve[16], particle_rotation[16], particle_color[16], particle_opacity[16], particle_velocity[16];
uniform sampler2D particle_previous_positions, particle_previous_velocities;
uniform sampler2D particle_previous_trails, particle_trail_pose;
uniform vec4 particle_trail_info; // samples, ring head, trail columns, metadata columns
uniform vec4 particle_collision, particle_response, particle_shapes[8];
uniform float particle_shape_kinds[8], particle_shape_count, particle_step;
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
uvec2 key_step(uvec2 key, uvec2 bits, uint position) {
    uvec2 golden = uvec2(0x7f4a7c15u, 0x9e3779b9u);
    return mix64(add64(key ^ bits, multiply64(golden, uvec2(position + 1u, 0u))));
}
float key_unit(uvec2 key) {
    return float(mix64(key).y >> 8u) * 0.000000059604644775390625;
}
float random_unit(uvec2 sequence, uint channel) {
    return key_unit(key_step(key_step(words64(particle_seed), sequence, 0u), uvec2(channel, 0u), 1u));
}
float sheet_frame(uvec2 sequence, float age, float phase) {
    uint count = uint(particle_sheet.w), flags = uint(particle_sheet_time.y);
    uint offset = (flags & 2u) != 0u ? uint(random_unit(sequence, 9u) * float(count)) : 0u;
    if (count == 1u || particle_sheet_time.x == 0.0) return particle_sheet.z + float(offset);
    float time = (flags & 4u) != 0u ? phase * (particle_sheet_time.x * float(count)) : age * particle_sheet_time.x;
    uint frame = uint(floor(time)) + offset;
    return particle_sheet.z + float((flags & 1u) != 0u ? frame % count : min(frame, count - 1u));
}
vec3 noise_acceleration(uvec2 sequence, float age) {
    float phase = max(0.0, age) * particle_noise.w, interval = floor(phase);
    float weight = phase - interval;
    weight = weight * weight * (3.0 - 2.0 * weight);
    uvec2 key = key_step(words64(particle_seed), sequence, 0u);
    uvec2 a = key_step(key, uvec2(uint(interval), 0u), 1u);
    uvec2 b = key_step(key, uvec2(uint(interval) + 1u, 0u), 1u);
    vec3 value;
    for (int c = 0; c < 3; c++) {
        uvec2 channel = uvec2(uint(c) + 9u, 0u);
        float low = key_unit(key_step(a, channel, 2u)) * 2.0 - 1.0;
        float high = key_unit(key_step(b, channel, 2u)) * 2.0 - 1.0;
        value[c] = low + (high - low) * weight;
    }
    return particle_gravity + value * particle_noise.xyz;
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
void integrate_motion(vec3 origin, vec3 velocity, vec3 gravity, float time, out vec3 position, out vec3 speed) {
    float x = particle_drag * time;
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
    position = origin + velocity * travel + gravity * acceleration_travel;
    speed = velocity * decay + gravity * travel;
}
bool analytic_hit(vec3 position, vec3 delta, out float fraction, out vec3 normal, out bool overlapping) {
    fraction = 2.0;
    normal = vec3(0.0);
    overlapping = false;
    for (int shape = 0; shape < 8; shape++) {
        if (shape >= int(particle_shape_count)) break;
        vec4 value = particle_shapes[shape];
        float hit = 2.0;
        vec3 n = value.xyz;
        if (particle_shape_kinds[shape] < 0.5) {
            float gap = dot(n, position) - value.w - particle_collision.x;
            if (gap < -particle_collision.y * 0.25) { overlapping = true; return true; }
            float travel = dot(n, delta);
            if (travel < 0.0) hit = max(0.0, gap) / -travel;
        } else {
            vec3 offset = position - value.xyz;
            float radius = value.w + particle_collision.x;
            // a common scale keeps the quadratic finite for large admitted coordinates and velocities.
            vec3 magnitude = max(abs(offset), abs(delta));
            float scale = max(radius, max(magnitude.x, max(magnitude.y, magnitude.z)));
            vec3 o = offset / scale, d = delta / scale;
            float r = radius / scale, c = dot(o, o) - r * r;
            if (length(o) < (radius - particle_collision.y * 0.25) / scale) { overlapping = true; return true; }
            float a = dot(d, d), b = dot(o, d), discriminant = b * b - a * c;
            if (a > 0.0 && b < 0.0 && discriminant >= 0.0) {
                // this root avoids cancellation when the start is close to the sphere.
                hit = max(0.0, c) / (-b + sqrt(discriminant));
                n = normalize(o + d * hit);
            }
        }
        if (hit <= 1.0 && hit < fraction) { fraction = hit; normal = n; }
    }
    return fraction <= 1.0;
}
void collide_motion(inout vec3 position, inout vec3 velocity, inout float status, float time, vec3 gravity, vec3 transport) {
    if (status < 0.0) { velocity = vec3(0.0); return; }
    float remaining = time;
    for (int impact = 0; impact <= 16; impact++) {
        vec3 destination, next_velocity;
        integrate_motion(position, velocity - transport, gravity, remaining, destination, next_velocity);
        destination += transport * remaining;
        next_velocity += transport;
        vec3 delta = destination - position, normal;
        float fraction;
        bool overlapping;
        if (!analytic_hit(position, delta, fraction, normal, overlapping)) {
            position = destination; velocity = next_velocity; return;
        }
        if (overlapping || impact >= int(particle_response.z)) {
            status = overlapping ? -2.0 : -1.0; velocity = vec3(0.0); return;
        }
        vec3 unused, incoming;
        integrate_motion(position, velocity - transport, gravity, remaining * fraction, unused, incoming);
        incoming += transport;
        position += delta * fraction + normal * particle_collision.y;
        float speed = dot(incoming, normal);
        bool settle = particle_response.x > 0.5 || -speed * particle_collision.z <= particle_response.y;
        velocity = incoming;
        if (speed < 0.0)
            velocity = (incoming - normal * speed) * (1.0 - particle_collision.w) -
                       normal * (settle ? 0.0 : speed * particle_collision.z);
        if (settle) gravity -= normal * min(0.0, dot(gravity, normal));
        if (settle) transport -= normal * min(0.0, dot(transport, normal));
        remaining *= 1.0 - fraction;
        if (remaining <= 0.0) return;
    }
}
void main() {
    ivec2 cell = ivec2(gl_FragCoord.xy);
    if (particle_output > 1.5) {
        int samples = int(particle_trail_info.x), column = cell.x / samples, lane = cell.x % samples;
        int slot = cell.y * int(particle_trail_info.z) + column, columns = int(particle_trail_info.w);
        ivec2 head = ivec2(slot % columns, slot / columns);
        fragment_color = vec4(0.0, 0.0, 0.0, -1.0);
        if (head.y >= textureSize(particle_metadata, 0).y) return;
        vec4 metadata = texelFetch(particle_metadata, head, 0);
        if (metadata.w < 0.5) return;
        if (lane == int(particle_trail_info.y)) {
            fragment_color = vec4(texelFetch(particle_trail_pose, ivec2(head.x * 3, head.y), 0).xyz, 0.0);
        } else if (metadata.w < 1.5) {
            vec4 previous = texelFetch(particle_previous_trails, cell, 0);
            if (previous.w >= 0.0 && previous.w + particle_step <= 1000000.0)
                fragment_color = vec4(previous.xyz, previous.w + particle_step);
        }
        return;
    }
    bool pose = particle_output < 0.5;
    int lane = cell.x % 3;
    if (pose)
        cell.x /= 3;
    vec4 metadata = texelFetch(particle_metadata, cell, 0);
    if (metadata.w < 0.5) {
        fragment_color = vec4(0.0);
        return;
    }
    float phase = clamp(metadata.x / metadata.y, 0.0, 1.0);
    if (pose && lane == 1) {
        fragment_color = vec4(curve_value(particle_rotation, particle_rotation_count, phase),
                              sheet_frame(words64(birth(cell, 0)), metadata.x, phase));
        return;
    }
    if (pose && lane == 2) {
        fragment_color = vec4(curve_value(particle_color, particle_color_count, phase),
                              curve_value(particle_opacity, particle_opacity_count, phase).x);
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
    vec3 position, speed;
    float status = metadata.x;
    bool noise = any(greaterThan(particle_noise.xyz, vec3(0.0)));
    if (particle_shape_count > 0.0 || noise || particle_velocity_count > 0.0) {
        position = origin; speed = velocity; status = min(0.0, birth(cell, 5).w);
        float time = 0.0;
        if (metadata.w < 1.5) {
            position = texelFetch(particle_previous_positions, ivec2(cell.x * 3, cell.y), 0).xyz;
            vec4 previous = texelFetch(particle_previous_velocities, cell, 0);
            speed = previous.xyz; status = min(0.0, previous.w); time = particle_step;
        }
        vec3 gravity = noise ? noise_acceleration(words64(birth(cell, 0)), metadata.x - time) : particle_gravity;
        vec3 transport = curve_value(particle_velocity, particle_velocity_count,
                                    max(0.0, metadata.x - time) / metadata.y);
        if (particle_shape_count > 0.0) {
            speed += transport;
            collide_motion(position, speed, status, time, gravity, transport);
            speed -= transport;
        } else {
            integrate_motion(position, speed, gravity, time, position, speed);
            position += transport * time;
        }
    } else
        integrate_motion(origin, velocity, particle_gravity, max(0.0, metadata.x - anchor.w), position, speed);
    float size = metadata.z * curve_value(particle_curve, particle_curve_count, phase).x;
    fragment_color = particle_output < 0.5 ? vec4(position, size) : vec4(speed, status);
}
)GLSL";
}
static void particle_gpu_release(Scene3dParticleGpu *gpu) {
    if (gpu->graphics) {
        for (auto group : {gpu->positions, gpu->velocities, gpu->trails})
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
    for (auto group : {gpu->positions, gpu->velocities, gpu->trails})
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
void scene3d_particle_gpu_trail_attach(Scene3dParticleGpu *gpu, Scene3dParticleTrailState *trail) {
    if (!gpu || gpu->trail == trail) return;
    for (auto &target : gpu->trails) {
        if (gpu->graphics) native_gl_release(gpu->graphics, target);
        target = {};
    }
    gpu->trail = trail; gpu->trail_anchor_time = gpu->time;
}
static bool particle_gpu_trail_ensure(Scene3dParticleGpu *gpu) {
    const auto *trail = gpu->trail;
    if (!trail || (gpu->trails[0].id && gpu->trails[1].id)) return true;
    for (auto &target : gpu->trails) {
        if (!target.id) target = native_gl_float_target(gpu->graphics, trail->columns * trail->samples, trail->rows, false, true);
        if (!target.id || !native_gl_float_target_update(gpu->graphics, target, (const float *)trail->points[0])) return false;
    }
    return true;
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
static bool particle_gpu_compute(Scene3dParticleGpu *gpu, unsigned target, const Scene3dParticleGpuControls &controls,
                                 double timestep = 0, unsigned trail_head = UINT_MAX) {
    bool trail = gpu->trail && trail_head != UINT_MAX;
    unsigned passes = trail ? 3 : 2;
    if (!particle_gpu_surfaces(gpu) || gpu->passes > UINT64_MAX - passes)
        return false;
    const char *count_names[] = {"particle_curve_count", "particle_rotation_count", "particle_color_count", "particle_opacity_count", "particle_velocity_count"};
    const char *curve_names[] = {"particle_curve[0]", "particle_rotation[0]", "particle_color[0]", "particle_opacity[0]", "particle_velocity[0]"};
    for (unsigned kind = 0; kind < SCENE3D_PARTICLE_CURVES; kind++) {
        float count = (float)controls.curve_counts[kind];
        if (kind == SCENE3D_PARTICLE_VELOCITY_CURVE && !scene3d_particle_velocity_enabled(controls)) count = 0;
        if (!particle_gpu_uniform(gpu, count_names[kind], &count, 1) ||
            !particle_gpu_uniform(gpu, curve_names[kind], controls.curves[kind][0], 4, SCENE3D_PARTICLE_CURVE_LIMIT))
            return false;
    }
    float noise[] = {controls.noise.strength[0], controls.noise.strength[1], controls.noise.strength[2],
                      controls.noise.frequency};
    float sheet[] = {(float)controls.sheet.columns, (float)controls.sheet.rows,
                      (float)controls.sheet.first, (float)controls.sheet.count};
    float sheet_time[] = {controls.sheet.rate, (float)controls.sheet.flags, 0, 0};
    const auto &collision = controls.collision;
    float shape_count = (float)collision.count, step = (float)timestep;
    float material[] = {collision.radius, collision.skin, collision.restitution, collision.friction};
    float response[] = {(float)collision.response, collision.bounce_threshold, (float)collision.impacts, 0};
    if (!particle_gpu_uniform(gpu, "particle_seed", controls.seed, 4) ||
        !particle_gpu_uniform(gpu, "particle_gravity", controls.gravity, 3) ||
        !particle_gpu_uniform(gpu, "particle_noise", noise, 4) ||
        !particle_gpu_uniform(gpu, "particle_sheet", sheet, 4) ||
        !particle_gpu_uniform(gpu, "particle_sheet_time", sheet_time, 4) ||
        !particle_gpu_uniform(gpu, "particle_drag", &controls.drag, 1) ||
        !particle_gpu_uniform(gpu, "particle_shape_count", &shape_count, 1) ||
        !particle_gpu_uniform(gpu, "particle_step", &step, 1) ||
        !particle_gpu_uniform(gpu, "particle_collision", material, 4) ||
        !particle_gpu_uniform(gpu, "particle_response", response, 4) ||
        !particle_gpu_uniform(gpu, "particle_shapes[0]", collision.shapes[0], 4, SCENE3D_PARTICLE_ANALYTIC_LIMIT) ||
        !particle_gpu_uniform(gpu, "particle_shape_kinds[0]", collision.kinds, 1, SCENE3D_PARTICLE_ANALYTIC_LIMIT))
        return false;
    float trail_info[] = {gpu->trail ? (float)gpu->trail->samples : 0, trail ? (float)trail_head : 0,
                           gpu->trail ? (float)gpu->trail->columns : 0, (float)gpu->columns};
    if (!particle_gpu_uniform(gpu, "particle_trail_info", trail_info, 4)) return false;
    for (unsigned component = 0; component < passes; component++) {
        float output = (float)component;
        bool points = component == 2;
        auto surface = points ? gpu->trails[target] : component ? gpu->velocities[target] : gpu->positions[target];
        unsigned width = points ? gpu->trail->columns * gpu->trail->samples :
                          gpu->columns * (component ? 1 : SCENE3D_PARTICLE_GPU_POSE_TEXELS);
        unsigned rows = points ? gpu->trail->rows : gpu->rows;
        if (!native_gl_begin_region(gpu->graphics, surface, 0, 0, width, rows, nullptr) ||
            !particle_gpu_uniform(gpu, "particle_output", &output, 1))
            return false;
        NativeGlDraw draw = {gpu->program, gpu->triangle, 3, 1, false, false, 2, false};
        draw.bindings[draw.binding_count++] = {"particle_metadata", gpu->metadata_target, NATIVE_GL_BIND_FLOAT};
        draw.bindings[draw.binding_count++] = {"particle_births", gpu->birth_target, NATIVE_GL_BIND_FLOAT};
        draw.bindings[draw.binding_count++] = {"particle_surfaces",
                                               gpu->surface_target.id ? gpu->surface_target : gpu->metadata_target,
                                               NATIVE_GL_BIND_FLOAT};
        draw.bindings[draw.binding_count++] = {"particle_previous_positions", gpu->positions[1 - target], NATIVE_GL_BIND_FLOAT};
        draw.bindings[draw.binding_count++] = {"particle_previous_velocities", gpu->velocities[1 - target], NATIVE_GL_BIND_FLOAT};
        // inactive sampler branches still require valid images, without sampling the active attachment.
        draw.bindings[draw.binding_count++] = {"particle_trail_pose", gpu->positions[points ? target : 1 - target], NATIVE_GL_BIND_FLOAT};
        draw.bindings[draw.binding_count++] = {"particle_previous_trails", gpu->trail ? gpu->trails[1 - target] : gpu->positions[1 - target], NATIVE_GL_BIND_FLOAT};
        if (!native_gl_draw(gpu->graphics, &draw))
            return false;
        gpu->passes++;
    }
    return true;
}
static bool particle_gpu_recover_frame(Scene3dParticleGpu *gpu, double time, double previous, double step, bool initialize,
                                       unsigned trail_head = UINT_MAX) {
    for (unsigned slot = 0; slot < gpu->records; slot++) {
        auto value = gpu->metadata[0][slot];
        if (value.active > .5f && gpu->birth_times[0][slot] <= time) {
            value.age = (float)fmax(0, (double)value.age - (gpu->time - time));
            value.active = initialize || gpu->birth_times[0][slot] > previous ? 2 : 1;
        } else
            value = {};
        gpu->metadata[1][slot] = value;
    }
    unsigned target = 1 - gpu->current;
    if (!native_gl_float_target_update(gpu->graphics, gpu->metadata_target, (const float *)gpu->metadata[1]) ||
        !particle_gpu_compute(gpu, target, gpu->controls, step, trail_head))
        return false;
    gpu->current = target;
    return true;
}
static bool particle_gpu_recover(Scene3dParticleGpu *gpu) {
    if (!scene3d_particle_gpu_stateful(gpu->controls) && !gpu->trail)
        return native_gl_float_target_update(gpu->graphics, gpu->metadata_target, (const float *)gpu->metadata[0]) &&
               particle_gpu_compute(gpu, gpu->current, gpu->controls);
    // only surviving births need replay; immutable timestamps retain the actual fixed-step partition.
    double base = gpu->history_count ? gpu->history[gpu->history_head].start
                                    : gpu->time;
    if (!particle_gpu_recover_frame(gpu, base, base, 0, true))
        return false;
    for (unsigned i = 0; i < gpu->history_count; i++) {
        const auto &tick = gpu->history[(gpu->history_head + i) % SCENE3D_PARTICLE_HISTORY_LIMIT];
        unsigned head = gpu->trail && tick.time > gpu->trail_anchor_time ? tick.trail_head : UINT_MAX;
        if (!particle_gpu_recover_frame(gpu, tick.time, tick.start, tick.step, false, head))
            return false;
    }
    return true;
}
static bool particle_gpu_ensure(Scene3dParticleGpu *gpu) {
    if (gpu->ready) {
        // a rejected peer stage may have uploaded speculative birth frames into the shared draw atlas.
        if (!gpu->birth_uploaded && !gpu->staged) {
            if (!native_gl_float_target_update(gpu->graphics, gpu->birth_target, (const float *)gpu->births[0]))
                return false;
            gpu->birth_uploaded = true;
        }
        return particle_gpu_trail_ensure(gpu);
    }
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
        // each pose stores position/size, local Euler rotation and color/opacity.
        gpu->positions[i] = native_gl_float_target(graphics, gpu->columns * SCENE3D_PARTICLE_GPU_POSE_TEXELS, gpu->rows, false, true);
        gpu->velocities[i] = native_gl_float_target(graphics, gpu->columns, gpu->rows, false, true);
    }
    bool valid = gpu->metadata_target.id && gpu->birth_target.id;
    for (auto group : {gpu->positions, gpu->velocities})
        for (auto value : {group[0], group[1]})
            valid &= value.id != 0;
    if (!valid || !native_gl_float_target_update(graphics, gpu->metadata_target, (const float *)gpu->metadata[0]) ||
        !native_gl_float_target_update(graphics, gpu->birth_target, (const float *)gpu->births[0]) ||
        !particle_gpu_trail_ensure(gpu) || !particle_gpu_recover(gpu)) {
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
        gpu->birth_times[i] = (double *)mem_calloc(gpu->records, sizeof(double), MEM_CAT_RENDER);
    }
    gpu->read_positions = (float *)mem_calloc(gpu->records * 12, sizeof(float), MEM_CAT_RENDER);
    gpu->read_velocities = (float *)mem_calloc(gpu->records * 4, sizeof(float), MEM_CAT_RENDER);
    if (!gpu->metadata[0] || !gpu->metadata[1] || !gpu->births[0] || !gpu->births[1] || !gpu->read_positions ||
        !gpu->read_velocities || !gpu->birth_times[0] || !gpu->birth_times[1] || !particle_gpu_ensure(gpu)) {
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
        mem_free(gpu->birth_times[i]);
    }
    mem_free(gpu->read_positions);
    mem_free(gpu->read_velocities);
    mem_free(gpu->history);
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
    memcpy(gpu->birth_times[1], gpu->birth_times[0], gpu->records * sizeof(double));
    gpu->births_changed = births_changed;
    gpu->staged = false;
    gpu->checkpoint = false;
    gpu->diagnostic = nullptr;
    if (births_changed)
        memcpy(gpu->births[1], gpu->births[0], gpu->records * sizeof(Scene3dParticleGpuBirth));
    *upload = {gpu->metadata[1], births_changed ? gpu->births[1] : gpu->births[0]};
    return true;
}
bool scene3d_particle_gpu_stage(Scene3dParticleGpu *gpu, const Scene3dParticleGpuControls &controls, double timestep) {
    if (!gpu || !gpu->ready || !isfinite(timestep) || timestep <= 0 ||
        !isfinite(gpu->time + timestep) || gpu->time + timestep <= gpu->time)
        return false;
    for (unsigned count : controls.curve_counts)
        if (count < 2 || count > SCENE3D_PARTICLE_CURVE_LIMIT) return false;
    gpu->staged_time = gpu->time + timestep;
    gpu->staged_step = timestep;
    gpu->staged_drop = 0;
    bool stateful = scene3d_particle_gpu_stateful(controls);
    if (stateful || gpu->trail) {
        if (!gpu->history)
            gpu->history = (ParticleGpuTick *)mem_calloc(SCENE3D_PARTICLE_HISTORY_LIMIT, sizeof(ParticleGpuTick), MEM_CAT_RENDER);
        if (!gpu->history)
            return false;
        double earliest = gpu->staged_time;
        for (unsigned slot = 0; slot < gpu->capacity; slot++) {
            const auto &metadata = gpu->metadata[1][slot];
            if (metadata.active < .5f) continue;
            if (metadata.active > 1.5f) gpu->birth_times[1][slot] = gpu->staged_time;
            earliest = fmin(earliest, gpu->birth_times[1][slot]);
        }
        while (gpu->staged_drop < gpu->history_count &&
               gpu->history[(gpu->history_head + gpu->staged_drop) % SCENE3D_PARTICLE_HISTORY_LIMIT].time < earliest)
            gpu->staged_drop++;
        // closed-form motion needs only the samples that can still occupy the bounded trail ring.
        if (!stateful && gpu->history_count - gpu->staged_drop >= gpu->trail->samples)
            gpu->staged_drop = gpu->history_count - gpu->trail->samples + 1;
        if (gpu->history_count - gpu->staged_drop == SCENE3D_PARTICLE_HISTORY_LIMIT) {
            gpu->diagnostic = "GPU particle motion: recovery history exceeds 4096 retained ticks";
            return false;
        }
    }
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
    unsigned head = gpu->trail ? (gpu->trail->head + 1) % gpu->trail->samples : UINT_MAX;
    if (!particle_gpu_compute(gpu, 1 - gpu->current, controls, timestep, head))
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
    double previous_time = gpu->time;
    gpu->time = gpu->staged_time;
    auto *birth_times = gpu->birth_times[0];
    gpu->birth_times[0] = gpu->birth_times[1];
    gpu->birth_times[1] = birth_times;
    if (scene3d_particle_gpu_stateful(gpu->controls) || gpu->trail) {
        gpu->history_head = (gpu->history_head + gpu->staged_drop) % SCENE3D_PARTICLE_HISTORY_LIMIT;
        gpu->history_count -= gpu->staged_drop;
        gpu->history[(gpu->history_head + gpu->history_count++) % SCENE3D_PARTICLE_HISTORY_LIMIT] =
            {gpu->time, previous_time, gpu->staged_step,
             gpu->trail ? (gpu->trail->head + 1) % gpu->trail->samples : UINT_MAX};
    } else
        gpu->history_count = gpu->history_head = 0;
    gpu->staged = false;
    gpu->birth_uploaded = true;
}
void scene3d_particle_gpu_discard(Scene3dParticleGpu *gpu) {
    if (gpu) gpu->staged = false;
}
static bool particle_gpu_read_rows(Scene3dParticleGpu *gpu, const bool *selected) {
    for (unsigned row = 0; row < gpu->rows; row++) {
        if (!selected[row])
            continue;
        if (gpu->readbacks > UINT64_MAX - 2)
            return false;
        if (!native_gl_float_target_read(gpu->graphics, gpu->positions[gpu->current], 0, row, gpu->columns * SCENE3D_PARTICLE_GPU_POSE_TEXELS, 1,
                                         gpu->read_positions + row * gpu->columns * 12, gpu->columns * 12) ||
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
    double times[SCENE3D_PARTICLE_CURVE_LIMIT], values[SCENE3D_PARTICLE_CURVE_LIMIT * 3];
    unsigned keys = gpu->controls.curve_counts[SCENE3D_PARTICLE_VELOCITY_CURVE];
    for (unsigned key = 0; key < keys; key++) {
        const float *value = gpu->controls.curves[SCENE3D_PARTICLE_VELOCITY_CURVE][key];
        times[key] = value[0];
        for (unsigned c = 0; c < 3; c++) values[key * 3 + c] = value[c + 1];
    }
    AnimationTrackView track = {times, values, nullptr, nullptr, nullptr, keys, 3, ANIMATION_VECTOR, ANIMATION_LINEAR};
    for (unsigned i = 0; i < count; i++) {
        const float *p = gpu->read_positions + slots[i] * 12, *v = gpu->read_velocities + slots[i] * 4;
        for (unsigned c = 0; c < 4; c++)
            if (!isfinite(p[c]) || !isfinite(v[c]))
                return false;
        for (unsigned c = 4; c < 12; c++)
            if (!isfinite(p[c]))
                return false;
        if (p[7] < 0 || p[7] > 65535 || floorf(p[7]) != p[7]) return false;
        samples[i].frame = (unsigned)p[7];
        samples[i].rotation = {p[4], p[5], p[6]};
        samples[i].position = {p[0], p[1], p[2]};
        samples[i].velocity = {v[0], v[1], v[2]};
        // explicit copies combine retained dynamic velocity with committed authored transport.
        double transport[3];
        const auto &metadata = gpu->metadata[0][slots[i]];
        if (!animation_track_sample(track, (float)(metadata.age / metadata.lifetime), transport)) return false;
        samples[i].velocity = v[3] < 0 ? Scene3dVec{} : scene3d_add(samples[i].velocity,
            {(float)transport[0], (float)transport[1], (float)transport[2]});
        if (!pfinite({samples[i].velocity.x, samples[i].velocity.y, samples[i].velocity.z})) return false;
        samples[i].size = p[3];
        memcpy(samples[i].color, p + 8, sizeof(samples[i].color));
        samples[i].collision_status = v[3] < -1.5f ? 2 : v[3] < -.5f ? 1 : 0;
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
    if (auto *trail = gpu->trail) {
        if (gpu->readbacks > UINT64_MAX - trail->rows) return false;
        for (unsigned row = 0; row < trail->rows; row++) {
            auto *points = trail->points[1] + row * trail->columns * trail->samples;
            if (!native_gl_float_target_read(gpu->graphics, gpu->trails[gpu->current], 0, row,
                                              trail->columns * trail->samples, 1, (float *)points,
                                              trail->columns * trail->samples * 4)) return false;
            gpu->readbacks++;
        }
    }
    memcpy(gpu->births[1], gpu->births[0], gpu->records * sizeof(Scene3dParticleGpuBirth));
    for (unsigned slot = 0; slot < gpu->capacity; slot++) {
        const auto &metadata = gpu->metadata[0][slot];
        if (metadata.active < .5f)
            continue;
        auto &birth = gpu->births[1][slot];
        const float *p = gpu->read_positions + slot * 12, *v = gpu->read_velocities + slot * 4;
        for (unsigned c = 0; c < 3; c++) {
            if (!isfinite(p[c]) || !isfinite(v[c]))
                return false;
            birth.anchor_position_age[c] = p[c];
            birth.anchor_velocity[c] = v[c];
        }
        birth.anchor_position_age[3] = metadata.age;
        birth.anchor_velocity[3] = fminf(0, v[3]);
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
    if (auto *trail = gpu->trail) {
        // explicit motion edits checkpoint immutable trail anchors; ordinary updates never copy device points.
        auto *before_points = trail->points[0]; trail->points[0] = trail->points[1]; trail->points[1] = before_points;
        gpu->trail_anchor_time = gpu->time;
    }
    for (unsigned slot = 0; slot < gpu->capacity; slot++)
        gpu->birth_times[0][slot] = gpu->time;
    gpu->history_count = gpu->history_head = 0;
}
bool scene3d_particle_gpu_trail_sample(Scene3dParticleGpu *gpu, unsigned slot,
                                       Scene3dParticleTrailPoint *points, unsigned *count) {
    if (!gpu || !gpu->trail || !points || !count || slot >= gpu->capacity || !particle_gpu_ensure(gpu) ||
        gpu->readbacks == UINT64_MAX) return false;
    const auto *trail = gpu->trail;
    Scene3dParticleTrailPoint copied[SCENE3D_PARTICLE_TRAIL_SAMPLE_LIMIT];
    if (!native_gl_float_target_read(gpu->graphics, gpu->trails[gpu->current], slot % trail->columns * trail->samples,
                                      slot / trail->columns, trail->samples, 1, (float *)copied, trail->samples * 4)) return false;
    gpu->readbacks++; *count = 0;
    for (unsigned i = 0; i < trail->samples; i++) {
        const auto &point = copied[(trail->head + trail->samples - i) % trail->samples];
        if (!isfinite(point.age) || (point.age >= 0 && (!isfinite(point.position.x) ||
            !isfinite(point.position.y) || !isfinite(point.position.z)))) return false;
        if (point.age >= 0) points[(*count)++] = point;
    }
    return true;
}
bool scene3d_particle_gpu_stats(Scene3dParticleGpu *gpu, Scene3dParticleGpuStats *stats) {
    if (!gpu || !stats || !particle_gpu_ensure(gpu))
        return false;
    *stats = {gpu->positions[gpu->current],
              gpu->birth_target,
              gpu->readbacks,
              gpu->passes,
              gpu->recoveries,
              (uint64_t)gpu->records * SCENE3D_PARTICLE_GPU_SLOT_BYTES +
                  (gpu->surface_target.id
                       ? (uint64_t)SCENE3D_PARTICLE_SURFACE_COLUMNS * 4 * SCENE3D_PARTICLE_SURFACE_ROWS *
                             SCENE3D_PARTICLE_SURFACE_VERSIONS * 4 * sizeof(float)
                       : 0),
              sizeof(*gpu) + (uint64_t)gpu->records * (2 * sizeof(Scene3dParticleGpuBirth) + 2 * sizeof(double) +
                                                       2 * sizeof(Scene3dParticleGpuMetadata) + 16 * sizeof(float)) +
                  (gpu->history ? SCENE3D_PARTICLE_HISTORY_LIMIT * sizeof(ParticleGpuTick) : 0),
              gpu->history_count};
    if (gpu->trail) {
        stats->trails = gpu->trails[gpu->current];
        stats->gpu_bytes += (uint64_t)gpu->trail->records * gpu->trail->samples * 8 * sizeof(float);
    }
    return true;
}
const char *scene3d_particle_gpu_diagnostic(const Scene3dParticleGpu *gpu) {
    return gpu ? gpu->diagnostic : nullptr;
}
