#include "arraylist.hpp"
#include "mem.h"
#include "navigation.h"
#include <float.h>

struct NavigationLinks {
    NavigationSurfaceView surface = {};
    NavigationGraph* graph = nullptr;
    lam::ArrayList<NavigationLinkAttachment> links;
    size_t attachment_tests = 0;
    ~NavigationLinks() {
        navigation_destroy(graph);
        if (surface.owner)
            surface.release(surface.owner);
    }
};
static bool link_fail(NavigationError* error, const char* message, uint64_t id = 0) {
    if (error)
        *error = {message, id};
    return false;
}
bool navigation_link_valid(const NavigationLinkDesc* link, unsigned dimensions, NavigationError* error) {
    if (!link || (dimensions != 2 && dimensions != 3) || !link->id || !navigation_vec_finite(link->start) ||
        !navigation_vec_finite(link->end) || (dimensions == 2 && (link->start.z != 0 || link->end.z != 0)) ||
        !isfinite(link->cost) || link->cost < 0 || !isfinite(link->width) || link->width < 0 ||
        !isfinite(link->height) || link->height <= 0 || !isfinite(link->duration) || link->duration < 0 ||
        !isfinite(link->max_projection_distance) || link->max_projection_distance < 0)
        return link_fail(error, "navigation links: invalid ID, endpoint, cost, clearance or duration",
                         link ? link->id : 0);
    return true;
}
void navigation_links_destroy(NavigationLinks* links) {
    if (links) {
        links->~NavigationLinks();
        mem_free(links);
    }
}
const NavigationGraph* navigation_links_graph(const NavigationLinks* links) {
    return links ? links->graph : nullptr;
}
size_t navigation_links_count(const NavigationLinks* links) {
    return links ? links->links.size() : 0;
}
size_t navigation_links_polygon_count(const NavigationLinks* links) {
    return links ? links->surface.polygon_count : 0;
}
size_t navigation_links_attachment_tests(const NavigationLinks* links) {
    return links ? links->attachment_tests : 0;
}
bool navigation_links_attachment(const NavigationLinks* links, size_t index, NavigationLinkAttachment* result) {
    if (!links || !result || index >= links->links.size())
        return false;
    *result = links->links[index];
    return true;
}
static bool attach_endpoint(NavigationLinks* links, const NavigationLinkDesc& desc, NavigationVec source,
                            uint64_t pinned, size_t budget, NavigationVec* point, uint64_t* polygon,
                            NavigationError* error) {
    const auto& surface = links->surface;
    if (pinned) {
        if (links->attachment_tests == budget)
            return link_fail(error, "navigation links: attachment budget exhausted", desc.id);
        links->attachment_tests++;
        NavigationNodeDesc node = {};
        if (pinned > surface.polygon_count || !navigation_node(surface.graph, pinned, &node) || node.disabled ||
            !surface.closest(surface.owner, pinned, source, point))
            return link_fail(error, "navigation links: invalid pinned polygon", desc.id);
        double distance = navigation_vec_distance(source, *point);
        if (!isfinite(distance) || distance > desc.max_projection_distance)
            return link_fail(error, "navigation links: pinned endpoint exceeds attachment distance", desc.id);
        *polygon = pinned;
        return true;
    }
    NavigationMeshQuery query = {};
    query.search.filter = UINT32_MAX;
    query.max_projection_distance = desc.max_projection_distance;
    query.max_projection_tests = budget;
    NavigationMeshPath work = {};
    work.projection_tests = links->attachment_tests;
    double separation = 0;
    NavigationError failed = {};
    bool valid = navigation_surface_project(&surface, &query, source, point, polygon, &separation, &work, &failed);
    links->attachment_tests = work.projection_tests;
    if (!valid) {
        if (failed.message) {
            if (error)
                *error = failed;
            return false;
        }
        return link_fail(error, "navigation links: attachment budget exhausted", desc.id);
    }
    return *polygon != 0 ||
           link_fail(error, "navigation links: endpoint has no surface within attachment distance", desc.id);
}
bool navigation_links_create(const NavigationSurfaceView* surface, const NavigationLinksDesc* desc,
                             NavigationLinks** result, NavigationError* error) {
    if (result)
        *result = nullptr;
    if (!result || !navigation_surface_view_valid(surface) || !desc || (!desc->links && desc->count) ||
        desc->count > NAVIGATION_MAX_LINKS || desc->max_attachment_tests > NAVIGATION_MAX_ATTACHMENT_TESTS ||
        desc->count > (NAVIGATION_MAX_NODES - surface->polygon_count) / 2)
        return link_fail(error, "navigation links: invalid surface, descriptions or quota");
    void* memory = mem_alloc(sizeof(NavigationLinks), MEM_CAT_EVAL);
    if (!memory)
        return link_fail(error, "navigation links: owner allocation failed");
    auto* links = new (memory) NavigationLinks();
    links->surface = *surface;
    links->surface.owner = surface->retain(surface->owner);
    if (!links->surface.owner) {
        navigation_links_destroy(links);
        return link_fail(error, "navigation links: surface retain failed");
    }
    lam::ArrayList<NavigationNodeDesc> nodes;
    lam::ArrayList<NavigationEdgeDesc> edges;
    bool valid = true;
    for (size_t i = 0; i < surface->polygon_count && valid; i++) {
        NavigationNodeDesc node = {};
        valid = navigation_node_at(surface->graph, i, &node) && node.id == i + 1 && !node.transit && nodes.append(node);
    }
    for (size_t i = 0; i < navigation_edge_count(surface->graph) && valid; i++) {
        NavigationEdgeDesc edge = {};
        valid = navigation_edge_at(surface->graph, i, &edge) && edges.append(edge);
    }
    if (!valid)
        link_fail(error, "navigation links: surface graph copy failed");
    auto add_edge = [&](uint64_t from, uint64_t to, double cost, uint64_t guard1, uint64_t guard2) {
        if (edges.size() == NAVIGATION_MAX_EDGES || !isfinite(cost) || cost < 0)
            return link_fail(error, "navigation links: edge cost or quota exceeded");
        NavigationEdgeDesc edge = {};
        edge.from = from;
        edge.to = to;
        edge.cost = cost;
        edge.membership = UINT32_MAX;
        if (guard1) {
            edge.guards[edge.guard_count++] = guard1;
            edge.guards[edge.guard_count++] = guard2;
        }
        return edges.append(edge) || link_fail(error, "navigation links: edge allocation failed");
    };
    for (size_t i = 0; i < desc->count && valid; i++) {
        auto declaration = desc->links[i];
        NavigationLinkAttachment link = {};
        link.description = declaration;
        valid = navigation_link_valid(&declaration, surface->dimensions, error);
        if (!valid)
            break;
        for (const auto& before : links->links)
            if (before.description.id == declaration.id) {
                valid = link_fail(error, "navigation links: duplicate link ID", declaration.id);
                break;
            }
        if (!valid)
            break;
        valid = attach_endpoint(links, declaration, declaration.start, declaration.start_polygon,
                                desc->max_attachment_tests, &link.start, &link.start_polygon, error) &&
                attach_endpoint(links, declaration, declaration.end, declaration.end_polygon,
                                desc->max_attachment_tests, &link.end, &link.end_polygon, error);
        if (!valid)
            break;
        link.cost = declaration.automatic_cost ? navigation_vec_distance(link.start, link.end) : declaration.cost;
        uint64_t entry = surface->polygon_count + 2 * i + 1, exit = entry + 1;
        NavigationNodeDesc a = {entry, link.start, declaration.membership, declaration.disabled, true},
                           b = {exit, link.end, declaration.membership, declaration.disabled, true};
        if (!isfinite(link.cost) || !nodes.append(a) || !nodes.append(b) || !links->links.append(link)) {
            valid = link_fail(error, "navigation links: link allocation or numerical failure", declaration.id);
            break;
        }
        double start_cost = navigation_vec_distance(nodes[link.start_polygon - 1].position, link.start) *
                            surface->unit_cost(surface->owner, link.start_polygon),
               end_cost = navigation_vec_distance(link.end, nodes[link.end_polygon - 1].position) *
                          surface->unit_cost(surface->owner, link.end_polygon);
        valid = add_edge(link.start_polygon, entry, start_cost, 0, 0) &&
                add_edge(entry, exit, link.cost, link.start_polygon, link.end_polygon) &&
                add_edge(exit, link.end_polygon, end_cost, 0, 0);
        if (valid && declaration.bidirectional)
            valid = add_edge(link.end_polygon, exit, end_cost, 0, 0) &&
                    add_edge(exit, entry, link.cost, link.start_polygon, link.end_polygon) &&
                    add_edge(entry, link.start_polygon, start_cost, 0, 0);
    }
    if (valid && !nodes.empty()) {
        NavigationDesc graph = {surface->dimensions, nodes.data(), nodes.size(), edges.data(), edges.size()};
        valid = navigation_create(&graph, &links->graph, error);
    }
    if (valid)
        *result = links;
    else
        navigation_links_destroy(links);
    return valid;
}
static bool same_point(NavigationVec a, NavigationVec b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool navigation_link_equal(const NavigationLinkDesc* x, const NavigationLinkDesc* y) {
    if (x == y)
        return true;
    if (!x || !y)
        return false;
    const auto &d = *x, &e = *y;
    return d.id == e.id && d.type == e.type && same_point(d.start, e.start) && same_point(d.end, e.end) &&
           d.start_polygon == e.start_polygon && d.end_polygon == e.end_polygon && d.cost == e.cost &&
           d.width == e.width && d.height == e.height && d.duration == e.duration &&
           d.max_projection_distance == e.max_projection_distance && d.membership == e.membership &&
           d.disabled == e.disabled && d.bidirectional == e.bidirectional && d.automatic_cost == e.automatic_cost;
}
bool navigation_links_equal(const NavigationLinks* a, const NavigationLinks* b) {
    if (a == b)
        return true;
    if (!a || !b || a->surface.equal != b->surface.equal || a->links.size() != b->links.size() ||
        !a->surface.equal(a->surface.owner, b->surface.owner) || !navigation_equal(a->graph, b->graph))
        return false;
    for (size_t i = 0; i < a->links.size(); i++) {
        const auto &x = a->links[i], &y = b->links[i];
        if (!navigation_link_equal(&x.description, &y.description) || !same_point(x.start, y.start) ||
            !same_point(x.end, y.end) || x.start_polygon != y.start_polygon || x.end_polygon != y.end_polygon ||
            x.cost != y.cost)
            return false;
    }
    return true;
}
struct LinkAdmission {
    const NavigationLinks* links;
    const NavigationSurfaceQuery* query;
};
static bool link_admitted(void* owner, uint64_t id) {
    const auto& state = *(const LinkAdmission*)owner;
    auto* links = state.links;
    const auto& query = *state.query;
    if (id <= links->surface.polygon_count)
        return !query.path.search.admit_node || query.path.search.admit_node(query.path.search.admission_owner, id);
    size_t index = (size_t)(id - links->surface.polygon_count - 1) / 2;
    if (index >= links->links.size())
        return false;
    const auto& link = links->links[index].description;
    return query.radius <= link.width * .5 && query.height <= link.height;
}
static bool append_surface_segment(const NavigationLinks* links, const NavigationMeshQuery& query,
                                   NavigationMeshPath& path, size_t begin, size_t end, NavigationVec start,
                                   NavigationVec goal, NavigationError* error) {
    NavigationMeshPath segment = {};
    segment.corridor.nodes = path.corridor.nodes + begin;
    segment.corridor.count = end - begin + 1;
    // cell assembly reads the entry area as well as the borrowed corridor.
    segment.start_polygon = segment.corridor.nodes[0];
    segment.goal_polygon = segment.corridor.nodes[segment.corridor.count - 1];
    segment.projected_start = start;
    segment.path_end = goal;
    segment.portal_tests = path.portal_tests;
    bool valid = links->surface.assemble(links->surface.owner, &query, &segment, error);
    path.portal_tests = segment.portal_tests;
    if (valid && (!segment.points || !segment.point_count))
        valid = link_fail(error, "navigation links: empty assembled surface segment");
    if (valid) {
        double length = path.length + segment.length, cost = path.travel_cost + segment.travel_cost;
        if (!isfinite(length) || !isfinite(cost))
            valid = link_fail(error, "navigation links: grounded route outside numerical range");
        else {
            // do not simplify across an action endpoint; traversal indices remain stable.
            size_t offset = path.point_count ? 1 : 0;
            if (path.point_count &&
                (!segment.point_count || !same_point(path.points[path.point_count - 1], segment.points[0])))
                valid = link_fail(error, "navigation links: disconnected surface segment");
            else {
                for (size_t i = offset; i < segment.point_count; i++)
                    path.points[path.point_count++] = segment.points[i];
                for (size_t i = 0; i < segment.portal_count; i++)
                    path.portals[path.portal_count++] = segment.portals[i];
                path.length = length;
                path.travel_cost = cost;
            }
        }
    }
    mem_free(segment.points);
    mem_free(segment.portals);
    return valid;
}
static bool assemble_links(const NavigationLinks* links, const NavigationMeshQuery& query, NavigationMeshPath& path,
                           NavigationError* error) {
    size_t count = path.corridor.count,
           max_traversals = links->links.size() < count / 2 ? links->links.size() : count / 2;
    path.points = (NavigationVec*)mem_alloc((2 * count + 2) * sizeof(NavigationVec), MEM_CAT_EVAL);
    path.portals = (NavigationPortal*)mem_alloc(count * sizeof(NavigationPortal), MEM_CAT_EVAL);
    if (max_traversals)
        path.traversals = (NavigationTraversal*)mem_alloc(max_traversals * sizeof(NavigationTraversal), MEM_CAT_EVAL);
    if (!path.points || !path.portals || (max_traversals && !path.traversals))
        return link_fail(error, "navigation links: route allocation failed");
    size_t index = 0;
    NavigationVec start = path.projected_start;
    while (index < count) {
        if (!navigation_surface_work(&query, &path))
            return false;
        size_t end = index;
        while (end + 1 < count && path.corridor.nodes[end + 1] <= links->surface.polygon_count)
            end++;
        bool last = end + 1 == count;
        const NavigationLinkAttachment* link = nullptr;
        bool reverse = false;
        if (!last) {
            if (end + 3 >= count)
                return link_fail(error, "navigation links: incomplete traversal in corridor");
            uint64_t marker = path.corridor.nodes[end + 1];
            size_t ordinal = (size_t)(marker - links->surface.polygon_count - 1) / 2;
            if (ordinal >= links->links.size() || path.traversal_count == max_traversals)
                return link_fail(error, "navigation links: invalid traversal marker");
            link = &links->links[ordinal];
            reverse = (marker - links->surface.polygon_count - 1) % 2 != 0;
            uint64_t next = reverse ? marker - 1 : marker + 1;
            if (path.corridor.nodes[end + 2] != next || (reverse && !link->description.bidirectional) ||
                path.corridor.nodes[end] != (reverse ? link->end_polygon : link->start_polygon) ||
                path.corridor.nodes[end + 3] != (reverse ? link->start_polygon : link->end_polygon))
                return link_fail(error, "navigation links: invalid traversal connectivity");
        }
        NavigationVec departure = last ? path.path_end : reverse ? link->end : link->start;
        if (!append_surface_segment(links, query, path, index, end, start, departure, error))
            return false;
        if (last)
            return true;
        if (!navigation_surface_work(&query, &path))
            return false;
        NavigationVec arrival = reverse ? link->start : link->end;
        double length = path.length + navigation_vec_distance(departure, arrival), cost = path.travel_cost + link->cost;
        if (!isfinite(length) || !isfinite(cost))
            return link_fail(error, "navigation links: traversal outside numerical range", link->description.id);
        NavigationTraversal traversal = {link->description.id,
                                         link->description.type,
                                         reverse,
                                         departure,
                                         arrival,
                                         path.point_count - 1,
                                         path.point_count,
                                         link->cost,
                                         link->description.duration};
        path.points[path.point_count++] = arrival;
        path.traversals[path.traversal_count++] = traversal;
        path.length = length;
        path.travel_cost = cost;
        start = arrival;
        index = end + 3;
    }
    return true;
}
bool navigation_links_find(const NavigationLinks* links, const NavigationSurfaceQuery* query,
                           NavigationMeshPath* result, NavigationError* error) {
    if (!links || !navigation_surface_profile_valid(&links->surface, query, error))
        return false;
    const auto& q = query->path;
    if (!navigation_surface_query_valid(&q, result, links->surface.dimensions))
        return link_fail(error, "navigation links: invalid query output or budget");
    NavigationMeshPath path = {};
    NavigationError failed = {};
    bool valid = false;
    bool start = navigation_surface_project(&links->surface, &q, q.start, &path.projected_start, &path.start_polygon,
                                            &path.start_distance, &path, &failed),
         goal = start && navigation_surface_project(&links->surface, &q, q.goal, &path.projected_goal,
                                                    &path.goal_polygon, &path.goal_distance, &path, &failed);
    if (!start || !goal) {
        if (failed.message) {
            if (error)
                *error = failed;
            return false;
        }
        path.corridor.status = NAVIGATION_EXHAUSTED;
        *result = path;
        return true;
    }
    if (!path.start_polygon || !path.goal_polygon) {
        path.corridor.status = NAVIGATION_UNREACHABLE;
        *result = path;
        return true;
    }
    LinkAdmission admission = {links, query};
    NavigationQuery search = q.search;
    search.start = path.start_polygon;
    search.goal = path.goal_polygon;
    search.admit_node = link_admitted;
    search.admission_owner = &admission;
    if (!navigation_find(links->graph, &search, &path.corridor, error))
        return false;
    if (!path.corridor.count) {
        *result = path;
        return true;
    }
    path.path_end = path.projected_goal;
    if (path.corridor.status != NAVIGATION_COMPLETE &&
        !links->surface.closest(links->surface.owner, path.corridor.nodes[path.corridor.count - 1], q.goal,
                                &path.path_end)) {
        navigation_mesh_path_clear(&path);
        return link_fail(error, "navigation links: invalid partial endpoint");
    }
    valid = assemble_links(links, q, path, &failed);
    if (!valid) {
        if (failed.message) {
            navigation_mesh_path_clear(&path);
            if (error)
                *error = failed;
            return false;
        }
        navigation_surface_discard_route(&path);
    }
    *result = path;
    return true;
}
