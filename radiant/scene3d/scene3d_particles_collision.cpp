#include "scene3d.hpp"
#include "scene3d_source.hpp"
#include "scene3d_physics.hpp"
#include "../../lib/physics/physics_math.h"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/core/mark_reader.hpp"
#include <float.h>

static constexpr size_t PARTICLE_QUERY_WORK_LIMIT = 65536;

static bool collision_material_read(DomElement *node, Scene3dParticleCollisionDesc *description) {
    auto &d = *description;
    const char *response = scene3d_text(node, "response");
    const char *responses[] = {"bounce", "slide", "kill"};
    bool known = !response;
    for (unsigned i = 0; i < 3; i++) {
        if (response && !strcmp(response, responses[i])) {
            d.response = i;
            known = true;
        }
    }
    if (!known || !scene3d_real(node, "radius", .01, &d.radius) || !(d.radius > 0) || d.radius > 1e6 ||
        !scene3d_real(node, "skin", fmin(.001, d.radius * .1), &d.skin) || !(d.skin > 0) || d.skin > d.radius ||
        !scene3d_real(node, "restitution", .5, &d.restitution) || d.restitution < 0 || d.restitution > 1 ||
        !scene3d_real(node, "friction", 0, &d.friction) || d.friction < 0 || d.friction > 1 ||
        !scene3d_real(node, "bounce-threshold", .1, &d.bounce_threshold) || d.bounce_threshold < 0 ||
        d.bounce_threshold > 1e6 || !scene3d_unsigned(node, "max-impacts", 4, 16, &d.impacts) || !d.impacts)
        return false;
    return true;
}
bool scene3d_particle_collision_read(DomElement *node, Scene3dParticleCollisionDesc *description) {
    auto &d = *description;
    d.enabled = true;
    const char *profile = scene3d_text(node, "profile");
    const char *overlap = scene3d_text(node, "initial-overlap");
    if ((profile && strcmp(profile, "scene")) || (overlap && strcmp(overlap, "kill") && strcmp(overlap, "error")))
        return false;
    d.overlap_error = overlap && !strcmp(overlap, "error");
    if (!collision_material_read(node, &d) ||
        !scene3d_unsigned(node, "cast-iterations", PHYSICS_CAST_MAX_ITERATIONS, PHYSICS_CAST_MAX_ITERATIONS,
                          &d.iterations) ||
        !d.iterations || !scene3d_unsigned(node, "event-capacity", 256, 4096, &d.event_capacity) || !d.event_capacity ||
        !scene3d_unsigned(node, "membership", UINT32_MAX, UINT32_MAX, &d.membership) ||
        !scene3d_unsigned(node, "filter", UINT32_MAX, UINT32_MAX, &d.filter) ||
        !scene3d_flag(node, "sensors", false, &d.sensors) ||
        !scene3d_flag(node, "ignore-owner", true, &d.ignore_owner) || !scene3d_flag(node, "events", true, &d.events))
        return false;
    return true;
}
bool scene3d_particle_analytic_read(DomElement *node, Scene3dParticleAnalytic *description) {
    Scene3dParticleCollisionDesc material = {};
    const char *profile = scene3d_text(node, "profile"), *overlap = scene3d_text(node, "initial-overlap");
    bool events;
    if (!profile || strcmp(profile, "analytic") || (overlap && strcmp(overlap, "stop")) ||
        !scene3d_flag(node, "events", false, &events) || events ||
        !collision_material_read(node, &material) || material.response == SCENE3D_PARTICLE_KILL)
        return false;
    // analytic volumes have no scene filters, query solver or gameplay contact queue.
    const char *unsupported[] = {"membership", "filter", "sensors", "ignore-owner", "cast-iterations", "event-capacity"};
    for (const char *name : unsupported)
        if (node->has_attribute(name) || !scene3d_value(node, name).isNull())
            return false;
    auto &d = *description;
    d.radius = (float)material.radius;
    d.skin = (float)material.skin;
    d.restitution = (float)material.restitution;
    d.friction = (float)material.friction;
    d.bounce_threshold = (float)material.bounce_threshold;
    d.impacts = material.impacts;
    d.response = material.response;
    if (!(d.radius > 0) || !(d.skin > 0))
        return false;
    for (DomNode *child = node->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        auto *shape = child->as_element();
        if (shape->first_child || d.count == SCENE3D_PARTICLE_ANALYTIC_LIMIT)
            return false;
        Scene3dVec vector;
        double scalar;
        bool plane = !strcmp(shape->local_name(), "particle-plane");
        if (!plane && strcmp(shape->local_name(), "particle-sphere"))
            return false;
        if (!scene3d_vector(shape, plane ? "normal" : "center", plane ? Scene3dVec{0, 1, 0} : Scene3dVec{}, &vector) ||
            !scene3d_real(shape, plane ? "offset" : "radius", plane ? 0 : 1, &scalar) || fabs(scalar) > 1e6 ||
            (!plane && !(scalar > 0)))
            return false;
        if (plane) {
            double length = sqrt((double)vector.x * vector.x + (double)vector.y * vector.y + (double)vector.z * vector.z);
            if (!(length > 1e-12) || !isfinite(length))
                return false;
            vector = {(float)(vector.x / length), (float)(vector.y / length), (float)(vector.z / length)};
        }
        float *value = d.shapes[d.count];
        value[0] = vector.x;
        value[1] = vector.y;
        value[2] = vector.z;
        value[3] = (float)scalar;
        if (!plane && !(value[3] > 0))
            return false;
        d.kinds[d.count++] = plane ? 0 : 1;
    }
    return d.count != 0;
}
Scene3dParticleMotion scene3d_particle_motion(double drag, double timestep) {
    double x = drag * timestep;
    double travel = drag ? -expm1(-x) / drag : timestep;
    // share the cancellation-safe integral between free motion and post-impact motion.
    double acceleration_travel =
        fabs(x) < .001 ? timestep * timestep * (.5 + x * (-1.0 / 6 + x * (1.0 / 24 + x * (-1.0 / 120 + x / 720))))
                       : (timestep - travel) / drag;
    return {exp(-x), travel, acceleration_travel, drag * acceleration_travel, -expm1(-x)};
}
bool scene3d_particle_integrate(PhysicsVec position, PhysicsVec velocity, PhysicsVec gravity,
                                const Scene3dParticleMotion &motion, PhysicsVec *next_position,
                                PhysicsVec *next_velocity, PhysicsVec transport) {
    *next_position = padd(padd(position, pmul(velocity, motion.travel)), pmul(gravity, motion.acceleration_travel));
    *next_velocity = padd(pmul(velocity, motion.decay), pmul(gravity, motion.travel));
    // only dynamic velocity decays; authored transport stays constant through the substep.
    *next_position = padd(*next_position, pmul(transport, motion.transport_travel));
    *next_velocity = padd(*next_velocity, pmul(transport, motion.transport_gain));
    return pfinite(*next_position) && pfinite(*next_velocity);
}
static bool collision_fail(PhysicsError *error, const char *message) {
    if (error)
        error->message = message;
    return false;
}
static bool collision_cast(const Scene3dParticleCollisionDesc &d, const Scene3dParticleCollisionFrame &frame,
                           PhysicsVec origin, PhysicsVec delta, Scene3dParticleCollisionWork *work, PhysicsHit *hit,
                           PhysicsError *error) {
    size_t count = scene3d_physics_query_colliders(frame.physics), cost = count ? count : 1;
    if (cost > PARTICLE_QUERY_WORK_LIMIT - work->geometry)
        return collision_fail(error, "particle collision: scene query work exceeds 65536 collider checks per tick");
    work->geometry += cost;
    work->queries++;
    *hit = {};
    if (!count)
        return true;
    PhysicsQuery query = {};
    query.kind = PHYSICS_SHAPE_CAST;
    query.radius = d.radius;
    query.origin = origin;
    query.direction = delta;
    query.distance = plen(delta);
    query.membership = d.membership;
    query.filter = d.filter;
    query.include_sensors = d.sensors;
    query.cast_iterations = d.iterations;
    query.exclude_body = d.ignore_owner ? frame.excluded_body : 0;
    if (!scene3d_physics_query(frame.physics, &query, 1, hit, error))
        return false;
    if (hit->triangle_tests > PARTICLE_QUERY_WORK_LIMIT - work->triangles)
        return collision_fail(error, "particle collision: scene triangle work exceeds 65536 tests per tick");
    work->triangles += hit->triangle_tests;
    return true;
}
static bool collision_store(const Scene3dParticleCollisionFrame &frame, PhysicsVec position, PhysicsVec velocity,
                            Scene3dParticleSample *sample, PhysicsError *error) {
    for (unsigned axis = 0; axis < 3; axis++) {
        if (!isfinite(paxis(position, axis)) || fabs(paxis(position, axis)) > FLT_MAX ||
            !isfinite(paxis(velocity, axis)) || fabs(paxis(velocity, axis)) > FLT_MAX)
            return collision_fail(error, "particle collision: world motion exceeds finite coordinates");
    }
    auto p = scene3d_point(frame.inverse, {(float)position.x, (float)position.y, (float)position.z});
    auto v = scene3d_point(frame.inverse, {(float)velocity.x, (float)velocity.y, (float)velocity.z}, 0);
    if (!pfinite({p.x, p.y, p.z}) || !pfinite({v.x, v.y, v.z}))
        return collision_fail(error, "particle collision: local motion exceeds finite coordinates");
    sample->position = p;
    sample->velocity = v;
    return true;
}
bool scene3d_particle_collide(const Scene3dParticleCollisionDesc &d, const Scene3dParticleCollisionFrame &frame,
                              Scene3dParticleSample *sample, Scene3dVec gravity, double drag, double timestep,
                              Scene3dParticleCollisionWork *work, Scene3dParticleContactFn contact, void *owner,
                              bool *alive, PhysicsError *error, Scene3dVec transport) {
    *alive = true;
    auto p = scene3d_point(frame.world, sample->position);
    auto v = scene3d_point(frame.world, sample->velocity, 0);
    auto g = scene3d_point(frame.world, gravity, 0);
    auto t = scene3d_point(frame.world, transport, 0);
    PhysicsVec drift = {t.x, t.y, t.z};
    PhysicsVec position = {p.x, p.y, p.z}, velocity = {v.x, v.y, v.z}, acceleration = {g.x, g.y, g.z};
    if (!pfinite(position) || !pfinite(velocity) || !pfinite(acceleration) || !pfinite(drift))
        return collision_fail(error, "particle collision: invalid transformed motion");
    double remaining = timestep;
    unsigned impacts = 0;
    for (;;) {
        PhysicsVec destination, next_velocity;
        if (!scene3d_particle_integrate(position, velocity, acceleration, scene3d_particle_motion(drag, remaining),
                                        &destination, &next_velocity, drift))
            return collision_fail(error, "particle collision: motion integration exceeds finite coordinates");
        PhysicsVec delta = psub(destination, position);
        PhysicsHit hit;
        if (!collision_cast(d, frame, position, delta, work, &hit, error))
            return false;
        if (!hit.found)
            return collision_store(frame, destination, next_velocity, sample, error);
        if (impacts++ == d.impacts)
            return collision_fail(error, "particle collision: per-particle impact budget exceeded");
        if (hit.overlapping && d.overlap_error)
            return collision_fail(error, "particle collision: initial sphere overlap");
        double fraction = pclamp(hit.fraction, 0, 1);
        PhysicsVec unused, incoming;
        if (!scene3d_particle_integrate(position, velocity, acceleration,
                                        scene3d_particle_motion(drag, remaining * fraction), &unused, &incoming, drift))
            return collision_fail(error, "particle collision: impact velocity exceeds finite coordinates");
        PhysicsVec center = padd(position, pmul(delta, fraction));
        bool killed = d.response == SCENE3D_PARTICLE_KILL || hit.overlapping;
        PhysicsVec outgoing = {};
        if (!killed) {
            PhysicsVec surface;
            if (!scene3d_physics_velocity_at(frame.physics, hit.body, hit.point_b, &surface, error))
                return false;
            PhysicsVec normal = punit(hit.normal), relative = psub(incoming, surface);
            if (!pfinite(normal) || plen(normal) < .5)
                return collision_fail(error, "particle collision: missing impact normal");
            double speed = pdot(relative, normal);
            bool settle = d.response == SCENE3D_PARTICLE_SLIDE || -speed * d.restitution <= d.bounce_threshold;
            if (speed < 0) {
                PhysicsVec tangent = psub(relative, pmul(normal, speed));
                relative = psub(pmul(tangent, 1 - d.friction), pmul(normal, settle ? 0 : speed * d.restitution));
            }
            outgoing = padd(surface, relative);
            // a resting/sliding contact removes inward acceleration for the rest of this tick.
            if (settle && pdot(acceleration, normal) < 0)
                acceleration = psub(acceleration, pmul(normal, pdot(acceleration, normal)));
            if (settle && pdot(drift, normal) < 0)
                drift = psub(drift, pmul(normal, pdot(drift, normal)));
            position = padd(center, pmul(normal, d.skin));
        }
        double tick_fraction = timestep ? 1 - remaining * (1 - fraction) / timestep : 0;
        Scene3dParticleContact event = {hit, center, incoming, outgoing, pclamp(tick_fraction, 0, 1), killed};
        if (!contact(owner, event, error))
            return false;
        if (killed) {
            *alive = false;
            return true;
        }
        velocity = outgoing;
        remaining *= 1 - fraction;
        if (!remaining)
            return collision_store(frame, position, velocity, sample, error);
    }
}
