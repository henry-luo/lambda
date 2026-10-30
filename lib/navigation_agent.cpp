#include "arraylist.hpp"
#include "mem.h"
#include "navigation.h"
#include <float.h>

struct NavigationFollower {
    NavigationFollowerDesc desc = {};
    NavigationFollowState state = {};
    lam::ArrayList<NavigationVec> points;
    lam::ArrayList<NavigationTraversal> traversals;
    double progress_distance = 0;
    bool progress_set = false;
};
static bool follow_fail(NavigationError* error, const char* message) {
    if (error)
        *error = {message, 0};
    return false;
}
static bool follow_point(unsigned dimensions, NavigationVec point) {
    return navigation_vec_finite(point) && (dimensions == 3 || point.z == 0);
}
static bool follow_same_point(NavigationVec a, NavigationVec b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
static NavigationVec follow_scale(NavigationVec value, double scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}
static NavigationVec follow_divide(NavigationVec value, double divisor) {
    return {value.x / divisor, value.y / divisor, value.z / divisor};
}
static NavigationVec follow_difference(NavigationVec a, NavigationVec b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
static NavigationVec follow_offset(const NavigationFollower* follower, NavigationVec a, NavigationVec b) {
    auto offset = follow_difference(a, b);
    if (follower->desc.horizontal)
        offset.y = 0;
    return offset;
}
static double follow_length(NavigationVec value) {
    return hypot(hypot(value.x, value.y), value.z);
}
static NavigationVec follow_add(NavigationVec a, NavigationVec b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
static bool follow_desc_valid(const NavigationFollowerDesc* d) {
    return d && (d->dimensions == 2 || d->dimensions == 3) && (!d->horizontal || d->dimensions == 3) &&
           isfinite(d->max_speed) && d->max_speed >= 0 && isfinite(d->max_acceleration) && d->max_acceleration > 0 &&
           isfinite(d->waypoint_distance) && d->waypoint_distance >= 0 && isfinite(d->target_distance) &&
           d->target_distance >= 0 && isfinite(d->path_distance) &&
           d->path_distance >= fmax(d->waypoint_distance, d->target_distance) && isfinite(d->stuck_time) &&
           d->stuck_time >= 0 && isfinite(d->min_progress) && d->min_progress > 0 && d->max_waypoints &&
           d->max_waypoints <= NAVIGATION_MAX_ROUTE_POINTS;
}
bool navigation_follower_create(const NavigationFollowerDesc* desc, NavigationFollower** result,
                                NavigationError* error) {
    if (result)
        *result = nullptr;
    if (!result || !follow_desc_valid(desc)) {
        return follow_fail(error, "navigation follower: invalid description or work budget");
    }
    void* memory = mem_alloc(sizeof(NavigationFollower), MEM_CAT_EVAL);
    if (!memory)
        return follow_fail(error, "navigation follower: allocation failed");
    auto* follower = new (memory) NavigationFollower();
    follower->desc = *desc;
    *result = follower;
    return true;
}
void navigation_follower_destroy(NavigationFollower* follower) {
    if (follower) {
        follower->~NavigationFollower();
        mem_free(follower);
    }
}
void navigation_follower_cancel(NavigationFollower* follower) {
    if (!follower)
        return;
    follower->points.clear();
    follower->traversals.clear();
    follower->state = {};
    follower->progress_set = false;
}
bool navigation_follower_read(const NavigationFollower* follower, NavigationFollowState* result) {
    if (!follower || !result)
        return false;
    *result = follower->state;
    return true;
}
bool navigation_follower_action(const NavigationFollower* follower, NavigationTraversal* action) {
    if (!follower || !action || follower->state.status != NAVIGATION_FOLLOW_ACTION ||
        follower->state.traversal >= follower->traversals.size())
        return false;
    *action = follower->traversals[follower->state.traversal];
    return true;
}
bool navigation_follower_assign(NavigationFollower* follower, const NavigationMeshPath* path, uint64_t generation,
                                NavigationError* error) {
    if (!follower || !path || !generation || !path->points || !path->point_count ||
        path->point_count > NAVIGATION_MAX_ROUTE_POINTS || (unsigned)path->corridor.status > NAVIGATION_EXHAUSTED ||
        path->corridor.status == NAVIGATION_UNREACHABLE || path->traversal_count > NAVIGATION_MAX_LINKS ||
        (!path->traversals && path->traversal_count))
        return follow_fail(error, "navigation follower: requires a verified route and generation");
    lam::ArrayList<NavigationVec> points;
    lam::ArrayList<NavigationTraversal> actions;
    for (size_t i = 0; i < path->point_count; i++) {
        if (!follow_point(follower->desc.dimensions, path->points[i]))
            return follow_fail(error, "navigation follower: invalid route coordinates");
        if (i && !isfinite(navigation_vec_distance(path->points[i - 1], path->points[i])))
            return follow_fail(error, "navigation follower: route distance outside numerical range");
        if (!points.append(path->points[i]))
            return follow_fail(error, "navigation follower: route allocation failed");
    }
    for (size_t i = 0; i < path->traversal_count; i++) {
        const auto& action = path->traversals[i];
        if (!action.id || action.start_point >= points.size() || action.end_point != action.start_point + 1 ||
            action.end_point >= points.size() || (i && action.start_point < path->traversals[i - 1].end_point) ||
            !follow_same_point(action.start, points[action.start_point]) ||
            !follow_same_point(action.end, points[action.end_point]) || !isfinite(action.duration) ||
            action.duration < 0 || !isfinite(action.cost) || action.cost < 0)
            return follow_fail(error, "navigation follower: invalid action endpoints or ordering");
        if (!actions.append(action))
            return follow_fail(error, "navigation follower: action allocation failed");
    }
    follower->points = static_cast<lam::ArrayList<NavigationVec>&&>(points);
    follower->traversals = static_cast<lam::ArrayList<NavigationTraversal>&&>(actions);
    follower->state = {};
    follower->state.status = NAVIGATION_FOLLOW_MOVING;
    follower->state.generation = generation;
    follower->state.complete = path->corridor.status == NAVIGATION_COMPLETE;
    follower->progress_set = false;
    return true;
}
static double follow_segment_distance(const NavigationFollower* follower, NavigationVec point, NavigationVec a,
                                      NavigationVec b) {
    if (follower->desc.horizontal)
        point.y = a.y = b.y = 0;
    NavigationVec edge = follow_difference(b, a);
    double length = follow_length(edge);
    if (length == 0)
        return navigation_vec_distance(point, a);
    if (!isfinite(length))
        return INFINITY;
    NavigationVec direction = follow_divide(edge, length), offset = follow_difference(point, a);
    double along = offset.x * direction.x + offset.y * direction.y + offset.z * direction.z;
    if (!isfinite(along))
        return INFINITY;
    double t = fmax(0, fmin(1, along / length));
    // convex interpolation avoids overflowing a+(b-a)*t for opposite extreme endpoints.
    NavigationVec nearest = follow_add(follow_scale(a, 1 - t), follow_scale(b, t));
    return navigation_vec_distance(point, nearest);
}
static void follow_stop(NavigationFollowState* state, NavigationFollowStatus status) {
    state->status = status;
    state->velocity = {};
    state->displacement = {};
    if (status != NAVIGATION_FOLLOW_ACTION)
        state->action = 0;
}
static bool follow_update(const NavigationFollower* follower, NavigationVec position, uint64_t generation, double delta,
                          NavigationFollowState* state, double* progress_distance, bool* progress_set,
                          NavigationError* error) {
    const auto& desc = follower->desc;
    state->position = position;
    state->waypoint_tests = 0;
    state->budget_exhausted = false;
    state->displacement = {};
    if (state->status == NAVIGATION_FOLLOW_IDLE)
        return true;
    if (generation != state->generation) {
        follow_stop(state, NAVIGATION_FOLLOW_STALE);
        return true;
    }
    if (state->status != NAVIGATION_FOLLOW_MOVING)
        return true;
    const auto& points = follower->points;
    const auto& actions = follower->traversals;
    while (state->waypoint < points.size()) {
        if (state->waypoint_tests == desc.max_waypoints) {
            state->budget_exhausted = true;
            follow_stop(state, NAVIGATION_FOLLOW_MOVING);
            return true;
        }
        state->waypoint_tests++;
        size_t cursor = state->waypoint;
        state->distance = follow_length(follow_offset(follower, position, points[cursor]));
        if (!isfinite(state->distance))
            return follow_fail(error, "navigation follower: observed distance outside numerical range");
        bool last = cursor + 1 == points.size();
        double tolerance = last ? desc.target_distance : desc.waypoint_distance;
        if (state->distance > tolerance)
            break;
        if (state->traversal < actions.size() && actions[state->traversal].start_point == cursor) {
            state->action = actions[state->traversal].id;
            follow_stop(state, NAVIGATION_FOLLOW_ACTION);
            return true;
        }
        if (last) {
            follow_stop(state, state->complete ? NAVIGATION_FOLLOW_ARRIVED : NAVIGATION_FOLLOW_PATH_END);
            return true;
        }
        state->waypoint++;
        state->stalled_time = 0;
        *progress_set = false;
    }
    size_t cursor = state->waypoint;
    double off_path =
        cursor ? follow_segment_distance(follower, position, points[cursor - 1], points[cursor]) : state->distance;
    if (off_path > desc.path_distance) {
        follow_stop(state, NAVIGATION_FOLLOW_OFF_PATH);
        return true;
    }
    if (!*progress_set || *progress_distance - state->distance >= desc.min_progress) {
        *progress_distance = state->distance;
        *progress_set = true;
        state->stalled_time = 0;
    } else {
        state->stalled_time += delta;
        if (!isfinite(state->stalled_time))
            return follow_fail(error, "navigation follower: stalled time outside numerical range");
        if (desc.stuck_time > 0 && desc.max_speed > 0 && state->stalled_time >= desc.stuck_time) {
            follow_stop(state, NAVIGATION_FOLLOW_STUCK);
            return true;
        }
    }
    NavigationVec offset = follow_offset(follower, points[cursor], position);
    double speed = fmin(desc.max_speed, state->distance / delta);
    // braking limits approach speed; infinity here simply leaves the finite speed cap authoritative.
    double braking = sqrt(2.0) * sqrt(desc.max_acceleration) * sqrt(state->distance);
    speed = fmin(speed, braking);
    NavigationVec desired = follow_scale(follow_divide(offset, state->distance), speed),
                  change = follow_difference(desired, state->velocity);
    double change_length = follow_length(change), limit = desc.max_acceleration * delta;
    if (!isfinite(change_length))
        return follow_fail(error, "navigation follower: steering outside numerical range");
    state->velocity =
        change_length > limit ? follow_add(state->velocity, follow_scale(change, limit / change_length)) : desired;
    state->displacement = follow_scale(state->velocity, delta);
    double travel = follow_length(state->displacement);
    // never issue a step longer than the next route segment; safety stops can brake immediately.
    if (travel > state->distance) {
        state->displacement = follow_scale(state->displacement, state->distance / travel);
        state->velocity = follow_divide(state->displacement, delta);
    }
    return (navigation_vec_finite(state->velocity) && navigation_vec_finite(state->displacement)) ||
           follow_fail(error, "navigation follower: movement outside numerical range");
}
bool navigation_follower_step(NavigationFollower* follower, NavigationVec position, uint64_t generation, double delta,
                              NavigationFollowState* result, NavigationError* error) {
    if (!follower || !result || !follow_point(follower->desc.dimensions, position) || !isfinite(delta) || delta <= 0)
        return follow_fail(error, "navigation follower: invalid observation or timestep");
    NavigationFollowState next = follower->state;
    double progress = follower->progress_distance;
    bool set = follower->progress_set;
    if (!follow_update(follower, position, generation, delta, &next, &progress, &set, error))
        return false;
    follower->state = next;
    follower->progress_distance = progress;
    follower->progress_set = set;
    *result = next;
    return true;
}
bool navigation_follower_complete_link(NavigationFollower* follower, uint64_t generation, uint64_t action,
                                       NavigationVec position, NavigationError* error) {
    if (!follower || !follow_point(follower->desc.dimensions, position) ||
        follower->state.status != NAVIGATION_FOLLOW_ACTION || generation != follower->state.generation ||
        action != follower->state.action || follower->state.traversal >= follower->traversals.size())
        return follow_fail(error, "navigation follower: stale or inactive action acknowledgement");
    const auto& traversal = follower->traversals[follower->state.traversal];
    if (navigation_vec_distance(position, traversal.end) > follower->desc.waypoint_distance)
        return follow_fail(error, "navigation follower: action has not achieved its arrival endpoint");
    auto& state = follower->state;
    state.waypoint = traversal.end_point;
    state.traversal++;
    state.action = 0;
    state.position = position;
    state.stalled_time = 0;
    follow_stop(&state, NAVIGATION_FOLLOW_MOVING);
    follower->progress_set = false;
    return true;
}
bool navigation_follower_feedback(NavigationFollower* follower, NavigationVec velocity, NavigationError* error) {
    if (!follower || !follow_point(follower->desc.dimensions, velocity) ||
        (follower->desc.horizontal && velocity.y != 0)) {
        return follow_fail(error, "navigation follower: invalid achieved velocity");
    }
    double speed = follow_length(velocity);
    if (!isfinite(speed))
        return follow_fail(error, "navigation follower: achieved speed outside numerical range");
    if (follower->state.status == NAVIGATION_FOLLOW_MOVING) {
        follower->state.velocity =
            speed > follower->desc.max_speed ? follow_scale(velocity, follower->desc.max_speed / speed) : velocity;
    }
    return true;
}

// build reciprocal velocity constraints from a copied crowd snapshot.
struct AvoidPlane {
    NavigationVec normal;
    double minimum;
};

static double avoid_dot(NavigationVec a, NavigationVec b) {
    return a.x * b.x + a.z * b.z;
}

static double avoid_determinant(NavigationVec a, NavigationVec b) {
    return navigation_cross2(a.x, a.z, b.x, b.z);
}

static NavigationVec avoid_horizontal(NavigationVec value) {
    value.y = 0;
    return value;
}

static double avoid_tolerance(double scale) {
    return 128 * DBL_EPSILON * scale;
}

static bool avoid_valid(const NavigationAvoidanceAgent* agents, size_t count, double delta, NavigationError* error) {
    if (count > NAVIGATION_AVOIDANCE_MAX_AGENTS || (!agents && count) || !isfinite(delta) || delta <= 0) {
        return follow_fail(error, "navigation avoidance: invalid snapshot or timestep");
    }
    for (size_t i = 0; i < count; i++) {
        const auto& agent = agents[i];
        double speed = follow_length(agent.preferred);
        if (!agent.id || !navigation_vec_finite(agent.position) || !navigation_vec_finite(agent.preferred) ||
            agent.preferred.y != 0 || !isfinite(agent.radius) || agent.radius <= 0 || !isfinite(agent.height) ||
            agent.height <= 0 || !isfinite(agent.position.y + agent.height) || !isfinite(agent.max_speed) ||
            agent.max_speed < 0 || !isfinite(speed) || speed > agent.max_speed + avoid_tolerance(agent.max_speed) ||
            !isfinite(agent.priority) || agent.priority < 0 || agent.priority > 1 || !isfinite(agent.time_horizon) ||
            agent.time_horizon <= 0 || agent.max_neighbors >= NAVIGATION_AVOIDANCE_MAX_AGENTS ||
            agent.max_neighbor_tests >= NAVIGATION_AVOIDANCE_MAX_AGENTS ||
            agent.max_solver_tests > NAVIGATION_AVOIDANCE_MAX_SOLVER_TESTS) {
            return follow_fail(error, "navigation avoidance: invalid agent or work budget");
        }
        for (size_t j = 0; j < i; j++) {
            if (agent.id == agents[j].id) {
                return follow_fail(error, "navigation avoidance: duplicate agent identity");
            }
        }
    }
    return true;
}

static bool avoid_test(NavigationAvoidanceResult& result, size_t maximum) {
    if (result.solver_tests == maximum) {
        result.status = NAVIGATION_AVOIDANCE_EXHAUSTED;
        result.velocity = {};
        return false;
    }
    result.solver_tests++;
    return true;
}

static bool avoid_plane(const NavigationAvoidanceAgent& agent, const NavigationAvoidanceAgent& other, double delta,
                        AvoidPlane* result) {
    NavigationVec offset = avoid_horizontal(follow_difference(other.position, agent.position));
    NavigationVec relative = follow_difference(agent.preferred, other.preferred);
    double distance = follow_length(offset);
    double radius = agent.radius + other.radius;
    double horizon = fmax(delta, fmax(agent.time_horizon, other.time_horizon));
    if (!isfinite(distance) || !isfinite(radius))
        return false;

    double inverse = 1 / (distance > radius ? horizon : delta);
    NavigationVec w = follow_difference(relative, follow_scale(offset, inverse));
    double w_length = follow_length(w);
    if (!navigation_vec_finite(w) || !isfinite(w_length) || !isfinite(radius * inverse))
        return false;

    NavigationVec axis =
        distance ? follow_divide(offset, distance) : NavigationVec{agent.id < other.id ? 1.0 : -1.0, 0, 0};
    bool use_cap = distance <= radius;
    if (!use_cap)
        use_cap = avoid_dot(w, axis) < -(radius / distance) * w_length;

    NavigationVec normal;
    NavigationVec correction;
    if (use_cap) {
        normal = w_length ? follow_divide(w, w_length) : follow_scale(axis, -1);
        correction = follow_scale(normal, radius * inverse - w_length);
    } else {
        double sine = radius / distance;
        double cosine = sqrt(fmax(0, 1 - sine * sine));
        NavigationVec tangent;
        if (avoid_determinant(offset, relative) > 0) {
            tangent = {axis.x * cosine - axis.z * sine, 0, axis.x * sine + axis.z * cosine};
        } else {
            tangent = {-axis.x * cosine - axis.z * sine, 0, axis.x * sine - axis.z * cosine};
        }
        normal = {-tangent.z, 0, tangent.x};
        correction = follow_difference(follow_scale(tangent, avoid_dot(relative, tangent)), relative);
    }

    double total_priority = agent.priority + other.priority;
    double responsibility = .5;
    if (!other.responsive)
        responsibility = 1;
    else if (total_priority > 0)
        responsibility = other.priority / total_priority;
    double minimum = avoid_dot(normal, follow_add(agent.preferred, follow_scale(correction, responsibility)));
    if (!navigation_vec_finite(normal) || !isfinite(minimum))
        return false;
    *result = {normal, minimum};
    return true;
}

static bool avoid_project(const NavigationAvoidanceAgent& agent, const AvoidPlane* planes, size_t count,
                          NavigationAvoidanceResult& result) {
    NavigationVec velocity = agent.preferred;
    double speed = agent.max_speed;
    for (size_t i = 0; i < count; i++) {
        if (!avoid_test(result, agent.max_solver_tests))
            return true;
        const auto& plane = planes[i];
        double tolerance = avoid_tolerance(fmax(speed, fabs(plane.minimum)));
        if (avoid_dot(plane.normal, velocity) >= plane.minimum - tolerance)
            continue;
        if (fabs(plane.minimum) > speed + tolerance) {
            result.status = NAVIGATION_AVOIDANCE_BLOCKED;
            result.velocity = {};
            return true;
        }

        NavigationVec origin = follow_scale(plane.normal, plane.minimum);
        NavigationVec tangent = {-plane.normal.z, 0, plane.normal.x};
        double ratio = speed ? plane.minimum / speed : 0;
        double span = speed * sqrt(fmax(0, 1 - ratio * ratio));
        double low = -span;
        double high = span;
        for (size_t j = 0; j < i; j++) {
            if (!avoid_test(result, agent.max_solver_tests))
                return true;
            double coefficient = avoid_dot(planes[j].normal, tangent);
            double minimum = planes[j].minimum - avoid_dot(planes[j].normal, origin);
            if (!isfinite(minimum))
                return false;
            if (fabs(coefficient) <= 16 * DBL_EPSILON) {
                if (minimum > tolerance) {
                    result.status = NAVIGATION_AVOIDANCE_BLOCKED;
                    result.velocity = {};
                    return true;
                }
            } else {
                double bound = minimum / coefficient;
                if (coefficient > 0)
                    low = fmax(low, bound);
                else
                    high = fmin(high, bound);
                if (low > high + tolerance) {
                    result.status = NAVIGATION_AVOIDANCE_BLOCKED;
                    result.velocity = {};
                    return true;
                }
            }
        }
        double preferred = avoid_dot(tangent, follow_difference(agent.preferred, origin));
        velocity = follow_add(origin, follow_scale(tangent, fmax(low, fmin(high, preferred))));
        if (!navigation_vec_finite(velocity))
            return false;
    }

    result.velocity = velocity;
    result.status = follow_length(follow_difference(velocity, agent.preferred)) > avoid_tolerance(speed)
                        ? NAVIGATION_AVOIDANCE_ADJUSTED
                        : NAVIGATION_AVOIDANCE_CLEAR;
    return true;
}

bool navigation_avoidance_step(const NavigationAvoidanceAgent* agents, size_t count, double delta,
                               NavigationAvoidanceResult* output, NavigationError* error) {
    if (!output && count)
        return follow_fail(error, "navigation avoidance: missing result storage");
    if (!avoid_valid(agents, count, delta, error))
        return false;

    NavigationAvoidanceResult results[NAVIGATION_AVOIDANCE_MAX_AGENTS] = {};
    size_t order[NAVIGATION_AVOIDANCE_MAX_AGENTS] = {};
    double maximum_radius = 0;
    double maximum_speed = 0;
    double maximum_horizon = delta;
    for (size_t i = 0; i < count; i++) {
        maximum_radius = fmax(maximum_radius, agents[i].radius);
        maximum_speed = fmax(maximum_speed, agents[i].max_speed);
        maximum_horizon = fmax(maximum_horizon, agents[i].time_horizon);

        size_t at = i;
        while (at && (agents[order[at - 1]].position.x > agents[i].position.x ||
                      (agents[order[at - 1]].position.x == agents[i].position.x &&
                       agents[order[at - 1]].id > agents[i].id))) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = i;
    }

    for (size_t i = 0; i < count; i++) {
        const auto& agent = agents[i];
        auto& result = results[i];
        if (!agent.responsive) {
            result.velocity = agent.preferred;
            result.status = NAVIGATION_AVOIDANCE_DISABLED;
            continue;
        }

        double reach = agent.radius + maximum_radius + (agent.max_speed + maximum_speed) * maximum_horizon;
        if (!isfinite(reach))
            return follow_fail(error, "navigation avoidance: neighbor reach outside numerical range");
        size_t low = 0;
        size_t high = count;
        size_t neighbors[NAVIGATION_AVOIDANCE_MAX_AGENTS] = {};
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            if (agents[order[middle]].position.x < agent.position.x - reach)
                low = middle + 1;
            else
                high = middle;
        }

        bool exhausted = false;
        for (size_t cursor = low; cursor < count && agents[order[cursor]].position.x <= agent.position.x + reach;
             cursor++) {
            size_t other_index = order[cursor];
            if (other_index == i)
                continue;
            if (result.neighbor_tests == agent.max_neighbor_tests) {
                exhausted = true;
                break;
            }
            result.neighbor_tests++;

            const auto& other = agents[other_index];
            bool overlaps_height = agent.position.y < other.position.y + other.height &&
                                   other.position.y < agent.position.y + agent.height;
            if (!overlaps_height)
                continue;
            double distance = hypot(agent.position.x - other.position.x, agent.position.z - other.position.z);
            double horizon = fmax(delta, fmax(agent.time_horizon, other.time_horizon));
            double limit = agent.radius + other.radius + (agent.max_speed + other.max_speed) * horizon;
            if (!isfinite(distance) || !isfinite(limit)) {
                return follow_fail(error, "navigation avoidance: pair distance outside numerical range");
            }
            if (distance > limit)
                continue;
            if (result.neighbors == agent.max_neighbors) {
                exhausted = true;
                break;
            }

            size_t at = result.neighbors++;
            while (at && agents[neighbors[at - 1]].id > other.id) {
                neighbors[at] = neighbors[at - 1];
                at--;
            }
            neighbors[at] = other_index;
        }
        if (exhausted) {
            result.status = NAVIGATION_AVOIDANCE_EXHAUSTED;
            continue;
        }

        AvoidPlane planes[NAVIGATION_AVOIDANCE_MAX_AGENTS] = {};
        bool ready = true;
        for (size_t j = 0; j < result.neighbors; j++) {
            if (!avoid_test(result, agent.max_solver_tests)) {
                ready = false;
                break;
            }
            if (!avoid_plane(agent, agents[neighbors[j]], delta, &planes[j])) {
                return follow_fail(error, "navigation avoidance: constraint outside numerical range");
            }
        }
        if (ready && !avoid_project(agent, planes, result.neighbors, result)) {
            return follow_fail(error, "navigation avoidance: projection outside numerical range");
        }
    }

    if (count)
        memcpy(output, results, count * sizeof(*output));
    return true;
}

static bool avoid_interval(double value, double rate, double* low, double* high) {
    if (rate > 0)
        *high = fmin(*high, -value / rate);
    else if (rate < 0)
        *low = fmax(*low, -value / rate);
    else if (value > 0)
        return false;
    return *low <= *high;
}

bool navigation_avoidance_verify(const NavigationAvoidanceAgent* agents, const NavigationVec* velocities, size_t count,
                                 double delta, bool* clear, NavigationError* error) {
    if (!clear || (!velocities && count)) {
        return follow_fail(error, "navigation avoidance: missing sweep storage");
    }
    if (!avoid_valid(agents, count, delta, error))
        return false;
    for (size_t i = 0; i < count; i++) {
        if (!navigation_vec_finite(velocities[i])) {
            return follow_fail(error, "navigation avoidance: nonfinite target velocity");
        }
    }

    bool safe = true;
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < i; j++) {
            const auto& agent = agents[i];
            const auto& other = agents[j];
            NavigationVec offset = follow_difference(other.position, agent.position);
            NavigationVec relative = follow_difference(velocities[j], velocities[i]);
            if (!navigation_vec_finite(offset) || !navigation_vec_finite(relative)) {
                return follow_fail(error, "navigation avoidance: sweep outside numerical range");
            }

            double low = 0;
            double high = delta;
            if (!avoid_interval(offset.y - agent.height, relative.y, &low, &high) ||
                !avoid_interval(-offset.y - other.height, -relative.y, &low, &high))
                continue;
            offset.y = 0;
            relative.y = 0;
            double speed = follow_length(relative);
            double initial = follow_length(offset);
            double radius = agent.radius + other.radius;
            if (!isfinite(speed) || !isfinite(initial) || !isfinite(radius)) {
                return follow_fail(error, "navigation avoidance: sweep distance outside numerical range");
            }

            NavigationVec direction = speed ? follow_divide(relative, speed) : NavigationVec{};
            double along = avoid_dot(offset, direction);
            double time = speed ? fmax(low, fmin(high, -along / speed)) : low;
            double closest = follow_length(follow_add(offset, follow_scale(relative, time)));
            double tolerance = avoid_tolerance(fmax(initial, radius));
            if (!isfinite(along) || !isfinite(closest)) {
                return follow_fail(error, "navigation avoidance: closest approach outside numerical range");
            }
            bool separating_existing_overlap = low == 0 && initial < radius - tolerance && along >= 0;
            if (closest < radius - tolerance && !separating_existing_overlap)
                safe = false;
        }
    }
    *clear = safe;
    return true;
}
