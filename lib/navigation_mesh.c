#include "math_utils.h"
#include "mem.h"
#include "navigation.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

typedef struct NMeshTriangle {
    NavigationTriangleDesc d;
    uint64_t neighbors[3];
} NMeshTriangle;
struct NavigationMesh {
    unsigned references;
    unsigned dimensions;
    NavigationVec* vertices;
    size_t vertex_count, triangle_count;
    NMeshTriangle* triangles;
    NavigationGraph* graph;
};
typedef struct NMeshVertex {
    NavigationVec p;
    uint32_t source;
} NMeshVertex;
typedef struct NMeshEdge {
    uint32_t low, high;
    size_t face;
    unsigned side;
    bool forward;
} NMeshEdge;
typedef struct NMeshFace {
    uint32_t vertices[3];
} NMeshFace;
typedef struct NMeshCorner {
    NavigationVec p;
    size_t portal;
} NMeshCorner;
static bool nm_fail(NavigationError* error, const char* message, uint64_t id) {
    if (error) {
        error->message = message;
        error->id = id;
    }
    return false;
}
static bool nm_same(NavigationVec a, NavigationVec b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
double navigation_cross2(double ax, double ay, double bx, double by) {
    double first = ax * by, second = ay * bx;
    return (first - second) + (fma(ax, by, -first) - fma(ay, bx, -second));
}
// independent vector scales preserve orientation when the two rays have very different lengths.
double navigation_surface_turn(NavigationVec origin, NavigationVec a, NavigationVec b, unsigned dimensions) {
    double ax = a.x - origin.x,
           ay = navigation_surface_axis(a, dimensions) - navigation_surface_axis(origin, dimensions),
           bx = b.x - origin.x,
           by = navigation_surface_axis(b, dimensions) - navigation_surface_axis(origin, dimensions);
    double sa = fmax(fabs(ax), fabs(ay)), sb = fmax(fabs(bx), fabs(by));
    if (!isfinite(sa) || !isfinite(sb))
        return NAN;
    if (sa == 0 || sb == 0)
        return 0;
    return navigation_cross2(ax / sa, ay / sa, bx / sb, by / sb);
}
static bool nm_same_plane(NavigationVec a, NavigationVec b, unsigned dimensions) {
    return a.x == b.x && navigation_surface_axis(a, dimensions) == navigation_surface_axis(b, dimensions);
}
static int nm_vertex_compare(const void* left, const void* right) {
    const NMeshVertex *a = left, *b = right;
    if (a->p.x != b->p.x)
        return a->p.x > b->p.x ? 1 : -1;
    if (a->p.y != b->p.y)
        return a->p.y > b->p.y ? 1 : -1;
    if (a->p.z != b->p.z)
        return a->p.z > b->p.z ? 1 : -1;
    return (a->source > b->source) - (a->source < b->source);
}
static int nm_edge_compare(const void* left, const void* right) {
    const NMeshEdge *a = left, *b = right;
    if (a->low != b->low)
        return a->low > b->low ? 1 : -1;
    if (a->high != b->high)
        return a->high > b->high ? 1 : -1;
    return (a->face > b->face) - (a->face < b->face);
}
static int nm_face_compare(const void* left, const void* right) {
    const NMeshFace *a = left, *b = right;
    for (unsigned i = 0; i < 3; i++)
        if (a->vertices[i] != b->vertices[i])
            return a->vertices[i] > b->vertices[i] ? 1 : -1;
    return 0;
}
bool navigation_triangle_normal(NavigationVec a, NavigationVec b, NavigationVec c, double normal[3]) {
    double ab[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, ac[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
    double scale = fmax(hypot(hypot(ab[0], ab[1]), ab[2]), hypot(hypot(ac[0], ac[1]), ac[2]));
    if (!isfinite(scale) || scale == 0)
        return false;
    for (unsigned i = 0; i < 3; i++) {
        ab[i] /= scale;
        ac[i] /= scale;
    }
    for (unsigned i = 0; i < 3; i++)
        normal[i] = navigation_cross2(ab[(i + 1) % 3], ab[(i + 2) % 3], ac[(i + 1) % 3], ac[(i + 2) % 3]);
    double length = hypot(hypot(normal[0], normal[1]), normal[2]);
    if (!isfinite(length) || length == 0)
        return false;
    for (unsigned i = 0; i < 3; i++)
        normal[i] /= length;
    return true;
}
void navigation_mesh_destroy(NavigationMesh* mesh) {
    if (!mesh || __atomic_sub_fetch(&mesh->references, 1, __ATOMIC_ACQ_REL) != 0)
        return;
    navigation_destroy(mesh->graph);
    mem_free(mesh->vertices);
    mem_free(mesh->triangles);
    mem_free(mesh);
}
const NavigationGraph* navigation_mesh_graph(const NavigationMesh* mesh) {
    return mesh ? mesh->graph : NULL;
}
size_t navigation_mesh_vertex_count(const NavigationMesh* mesh) {
    return mesh ? mesh->vertex_count : 0;
}
bool navigation_mesh_create(const NavigationMeshDesc* desc, NavigationMesh** result, NavigationError* error) {
    if (result)
        *result = NULL;
    if (!desc || !result || (desc->dimensions != 2 && desc->dimensions != 3) ||
        ((desc->vertex_count || desc->triangle_count) &&
         (!desc->vertices || !desc->triangles || desc->vertex_count < 3 || !desc->triangle_count)) ||
        desc->vertex_count > NAVIGATION_MAX_VERTICES || desc->triangle_count > NAVIGATION_MAX_NODES ||
        !isfinite(desc->max_slope) || desc->max_slope < 0 || desc->max_slope >= acos(0))
        return nm_fail(error, "navigation: invalid surface buffers, dimensions, slope or quota", 0);
    NavigationMesh* mesh = mem_calloc(1, sizeof(*mesh), MEM_CAT_EVAL);
    NMeshVertex* ordered = NULL;
    uint32_t* remap = NULL;
    NMeshFace* faces = NULL;
    NMeshEdge* topology = NULL;
    NavigationNodeDesc* nodes = NULL;
    NavigationEdgeDesc* edges = NULL;
    bool valid = false;
    if (!mesh)
        goto allocation;
    mesh->references = 1;
    mesh->dimensions = desc->dimensions;
    mesh->triangle_count = desc->triangle_count;
    // a valid empty bake is an owned empty domain, with no fabricated polygon.
    if (!desc->triangle_count) {
        *result = mesh;
        return true;
    }
    mesh->vertices = mem_alloc(desc->vertex_count * sizeof(NavigationVec), MEM_CAT_EVAL);
    mesh->triangles = mem_calloc(desc->triangle_count, sizeof(NMeshTriangle), MEM_CAT_EVAL);
    ordered = mem_alloc(desc->vertex_count * sizeof(NMeshVertex), MEM_CAT_EVAL);
    remap = mem_alloc(desc->vertex_count * sizeof(uint32_t), MEM_CAT_EVAL);
    faces = mem_alloc(desc->triangle_count * sizeof(NMeshFace), MEM_CAT_EVAL);
    topology = mem_alloc(desc->triangle_count * 3 * sizeof(NMeshEdge), MEM_CAT_EVAL);
    nodes = mem_calloc(desc->triangle_count, sizeof(NavigationNodeDesc), MEM_CAT_EVAL);
    edges = mem_calloc(desc->triangle_count * 3, sizeof(NavigationEdgeDesc), MEM_CAT_EVAL);
    if (!mesh->vertices || !mesh->triangles || !ordered || !remap || !faces || !topology || !nodes || !edges)
        goto allocation;
    for (size_t i = 0; i < desc->vertex_count; i++) {
        NavigationVec p = desc->vertices[i];
        if (!navigation_vec_finite(p) || (desc->dimensions == 2 && p.z != 0)) {
            nm_fail(error, "navigation: invalid surface vertex", i + 1);
            goto done;
        }
        ordered[i] = (NMeshVertex){p, (uint32_t)i};
    }
    qsort(ordered, desc->vertex_count, sizeof(NMeshVertex), nm_vertex_compare);
    for (size_t i = 0; i < desc->vertex_count; i++) {
        if (!i || !nm_same(ordered[i].p, ordered[i - 1].p))
            mesh->vertices[mesh->vertex_count++] = ordered[i].p;
        remap[ordered[i].source] = (uint32_t)(mesh->vertex_count - 1);
    }
    for (size_t i = 0; i < desc->triangle_count; i++) {
        NMeshTriangle* triangle = &mesh->triangles[i];
        triangle->d = desc->triangles[i];
        if (!isfinite(triangle->d.cost) || triangle->d.cost < 0) {
            nm_fail(error, "navigation: surface area costs must be finite and nonnegative", i + 1);
            goto done;
        }
        for (unsigned j = 0; j < 3; j++) {
            if (triangle->d.vertices[j] >= desc->vertex_count) {
                nm_fail(error, "navigation: invalid surface vertex index", i + 1);
                goto done;
            }
            triangle->d.vertices[j] = remap[triangle->d.vertices[j]];
        }
        uint32_t* v = triangle->d.vertices;
        NavigationVec a = mesh->vertices[v[0]], b = mesh->vertices[v[1]], c = mesh->vertices[v[2]];
        double normal[3];
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2] || !navigation_triangle_normal(a, b, c, normal)) {
            nm_fail(error, "navigation: degenerate or numerically unsupported surface triangle", i + 1);
            goto done;
        }
        if (desc->dimensions == 3 && (desc->two_sided ? fabs(normal[1]) : normal[1]) < cos(desc->max_slope))
            triangle->d.disabled = true;
        double orientation = navigation_surface_turn(a, b, c, desc->dimensions);
        if (!isfinite(orientation) || (desc->dimensions == 2 && orientation == 0)) {
            nm_fail(error, "navigation: unsupported surface projection", i + 1);
            goto done;
        }
        if (orientation < 0) {
            uint32_t swap = v[1];
            v[1] = v[2];
            v[2] = swap;
        }
        faces[i] = (NMeshFace){{v[0], v[1], v[2]}};
        for (unsigned j = 0; j < 2; j++)
            for (unsigned k = j + 1; k < 3; k++)
                if (faces[i].vertices[j] > faces[i].vertices[k]) {
                    uint32_t swap = faces[i].vertices[j];
                    faces[i].vertices[j] = faces[i].vertices[k];
                    faces[i].vertices[k] = swap;
                }
        nodes[i] = (NavigationNodeDesc){
            i + 1,
            {a.x / 3 + b.x / 3 + c.x / 3, a.y / 3 + b.y / 3 + c.y / 3, a.z / 3 + b.z / 3 + c.z / 3},
            triangle->d.membership,
            triangle->d.disabled};
        for (unsigned j = 0; j < 3; j++) {
            uint32_t from = v[j], to = v[(j + 1) % 3];
            topology[3 * i + j] = (NMeshEdge){from < to ? from : to, from < to ? to : from, i, j, from < to};
        }
    }
    qsort(faces, desc->triangle_count, sizeof(NMeshFace), nm_face_compare);
    for (size_t i = 1; i < desc->triangle_count; i++)
        if (!nm_face_compare(&faces[i - 1], &faces[i])) {
            nm_fail(error, "navigation: duplicate surface triangle", 0);
            goto done;
        }
    qsort(topology, desc->triangle_count * 3, sizeof(NMeshEdge), nm_edge_compare);
    size_t edge_count = 0;
    for (size_t i = 0; i < desc->triangle_count * 3;) {
        size_t end = i + 1;
        while (end < desc->triangle_count * 3 && topology[end].low == topology[i].low &&
               topology[end].high == topology[i].high)
            end++;
        if (end - i > 2) {
            nm_fail(error, "navigation: nonmanifold surface edge", topology[i].face + 1);
            goto done;
        }
        if (end - i == 2) {
            NMeshEdge *a = &topology[i], *b = &topology[i + 1];
            if (a->forward == b->forward && !nodes[a->face].disabled && !nodes[b->face].disabled) {
                nm_fail(error, "navigation: overlapping surface neighbors", a->face + 1);
                goto done;
            }
            double distance = navigation_vec_distance(nodes[a->face].position, nodes[b->face].position);
            for (unsigned direction = 0; direction < 2; direction++) {
                NMeshEdge *from = direction ? b : a, *to = direction ? a : b;
                double cost = distance * mesh->triangles[to->face].d.cost;
                if (!isfinite(cost)) {
                    nm_fail(error, "navigation: surface corridor cost outside numerical range", from->face + 1);
                    goto done;
                }
                edges[edge_count++] = (NavigationEdgeDesc){
                    .from = from->face + 1, .to = to->face + 1, .cost = cost, .membership = UINT32_MAX};
                mesh->triangles[from->face].neighbors[from->side] = to->face + 1;
            }
        }
        i = end;
    }
    NavigationDesc graph = {desc->dimensions, nodes, desc->triangle_count, edges, edge_count};
    valid = navigation_create(&graph, &mesh->graph, error);
    goto done;
allocation:
    nm_fail(error, "navigation: surface allocation failed", 0);
done:
    mem_free(ordered);
    mem_free(remap);
    mem_free(faces);
    mem_free(topology);
    mem_free(nodes);
    mem_free(edges);
    if (valid)
        *result = mesh;
    else
        navigation_mesh_destroy(mesh);
    return valid;
}
bool navigation_mesh_equal(const NavigationMesh* a, const NavigationMesh* b) {
    if (a == b)
        return true;
    if (!a || !b || a->dimensions != b->dimensions || a->vertex_count != b->vertex_count ||
        a->triangle_count != b->triangle_count || !navigation_equal(a->graph, b->graph))
        return false;
    for (size_t i = 0; i < a->vertex_count; i++)
        if (!nm_same(a->vertices[i], b->vertices[i]))
            return false;
    for (size_t i = 0; i < a->triangle_count; i++) {
        const NMeshTriangle *x = &a->triangles[i], *y = &b->triangles[i];
        if (x->d.cost != y->d.cost)
            return false;
        for (unsigned j = 0; j < 3; j++)
            if (x->d.vertices[j] != y->d.vertices[j] || x->neighbors[j] != y->neighbors[j])
                return false;
    }
    return true;
}
static bool nm_closest(const NavigationMesh* mesh, uint64_t polygon, NavigationVec p, NavigationVec* result) {
    const NMeshTriangle* triangle = &mesh->triangles[polygon - 1];
    double vertices[3][3], point[3] = {p.x, p.y, p.z}, out[3];
    for (unsigned i = 0; i < 3; i++) {
        NavigationVec v = mesh->vertices[triangle->d.vertices[i]];
        vertices[i][0] = v.x;
        vertices[i][1] = v.y;
        vertices[i][2] = v.z;
    }
    NavigationVec corners[3];
    for (unsigned i = 0; i < 3; i++)
        corners[i] = mesh->vertices[triangle->d.vertices[i]];
    bool inside = navigation_surface_turn(corners[0], corners[1], corners[2], mesh->dimensions) > 0;
    for (unsigned i = 0; i < 3 && inside; i++)
        inside = navigation_surface_turn(corners[i], corners[(i + 1) % 3], p, mesh->dimensions) >= 0;
    bool coplanar = mesh->dimensions == 2;
    if (inside && !coplanar) {
        double normal[3], delta[3] = {p.x - corners[0].x, p.y - corners[0].y, p.z - corners[0].z},
                          scale = fmax(fmax(fabs(delta[0]), fabs(delta[1])), fabs(delta[2]));
        coplanar = isfinite(scale) && navigation_triangle_normal(corners[0], corners[1], corners[2], normal);
        for (unsigned i = 0; i < 3 && coplanar; i++)
            if (scale > 0) {
                double value = delta[i] / scale;
                coplanar = value != 0 || delta[i] == 0;
                delta[i] = value;
            }
        if (coplanar) {
            // retain cancellation errors so a contracted multiply-add cannot invent an off-plane offset.
            double products[3], errors[3];
            for (unsigned i = 0; i < 3; i++) {
                products[i] = normal[i] * delta[i];
                errors[i] = fma(normal[i], delta[i], -products[i]);
            }
            coplanar = ((products[0] + products[1]) + products[2]) + ((errors[0] + errors[1]) + errors[2]) == 0;
        }
    }
    // a point already on the domain needs no barycentric reconstruction or artificial snap distance.
    if (inside && coplanar) {
        *result = p;
        return true;
    }
    if (!math_triangle_closest(vertices[0], vertices[1], vertices[2], point, out))
        return false;
    *result = (NavigationVec){out[0], out[1], out[2]};
    return true;
}
bool navigation_surface_view_valid(const NavigationSurfaceView* view) {
    return view && (view->dimensions == 2 || view->dimensions == 3) && view->polygon_count <= NAVIGATION_MAX_NODES &&
           navigation_node_count(view->graph) == view->polygon_count && view->owner && view->retain && view->release &&
           view->closest && view->unit_cost && view->assemble && view->equal && view->polygon;
}
bool navigation_surface_profile_valid(const NavigationSurfaceView* view, const NavigationSurfaceQuery* query,
                                      NavigationError* error) {
    if (!navigation_surface_view_valid(view) || !query || !isfinite(query->radius) || query->radius < 0 ||
        query->radius > view->radius || !isfinite(query->height) || query->height < 0 || !isfinite(query->max_step) ||
        query->max_step < 0 ||
        (view->clearance && (query->height <= 0 || query->height > view->height || query->max_step < view->max_step)))
        return nm_fail(error, "navigation: invalid surface query or incompatible clearance/step profile", 0);
    return true;
}
bool navigation_surface_project(const NavigationSurfaceView* view, const NavigationMeshQuery* query,
                                NavigationVec source, NavigationVec* point, uint64_t* polygon, double* separation,
                                NavigationMeshPath* path, NavigationError* error) {
    if (!navigation_surface_view_valid(view) || !query || !point || !polygon || !separation || !path ||
        !navigation_vec_finite(source) || (view->dimensions == 2 && source.z != 0))
        return nm_fail(error, "navigation: invalid surface projection", 0);
    double best = INFINITY;
    *polygon = 0;
    uint64_t selected = 0;
    NavigationVec candidate = {0};
    for (size_t i = 0; i < view->polygon_count; i++) {
        if (path->projection_tests == query->max_projection_tests)
            return false;
        path->projection_tests++;
        NavigationNodeDesc node = {0};
        navigation_node_at(view->graph, i, &node);
        if (node.disabled || !(node.membership & query->search.filter) ||
            (query->search.admit_node && !query->search.admit_node(query->search.admission_owner, node.id)))
            continue;
        NavigationVec projected;
        if (!view->closest(view->owner, node.id, source, &projected))
            return nm_fail(error, "navigation: surface projection outside numerical range", node.id);
        double distance = navigation_vec_distance(projected, source);
        if (!isfinite(distance))
            return nm_fail(error, "navigation: surface distance outside numerical range", node.id);
        if (distance <= query->max_projection_distance && distance < best) {
            selected = node.id;
            candidate = projected;
            best = distance;
        }
    }
    *polygon = selected;
    *point = candidate;
    *separation = selected ? best : 0;
    return true;
}
static NavigationPortal nm_portal(const NavigationMeshPath* path, size_t index) {
    if (!index)
        return (NavigationPortal){path->projected_start, path->projected_start};
    if (index == path->corridor.count)
        return (NavigationPortal){path->path_end, path->path_end};
    return path->portals[index - 1];
}
bool navigation_surface_work(const NavigationMeshQuery* query, NavigationMeshPath* path) {
    if (path->portal_tests >= query->max_portal_tests)
        return false;
    path->portal_tests++;
    return true;
}
static bool nm_intersect(NavigationVec a, NavigationVec b, NavigationPortal portal, unsigned dimensions,
                         double previous, double* parameter, double* edge_parameter) {
    double dx = b.x - a.x, dy = navigation_surface_axis(b, dimensions) - navigation_surface_axis(a, dimensions),
           lx = portal.left.x - a.x,
           ly = navigation_surface_axis(portal.left, dimensions) - navigation_surface_axis(a, dimensions),
           rx = portal.right.x - a.x,
           ry = navigation_surface_axis(portal.right, dimensions) - navigation_surface_axis(a, dimensions);
    double scale = fmax(fmax(fabs(dx), fabs(dy)), fmax(fmax(fabs(lx), fabs(ly)), fmax(fabs(rx), fabs(ry))));
    if (!isfinite(scale))
        return false;
    if (scale == 0) {
        *parameter = previous;
        *edge_parameter = 0;
        return true;
    }
    dx /= scale;
    dy /= scale;
    lx /= scale;
    ly /= scale;
    rx /= scale;
    ry /= scale;
    double ex = rx - lx, ey = ry - ly, denominator = navigation_cross2(dx, dy, ex, ey), t, u;
    double tolerance = 32 * DBL_EPSILON, parallel_error = 8 * DBL_EPSILON * (fabs(dx * ey) + fabs(dy * ex));
    // projected endpoints can move by an ulp; near-parallel determinant division amplifies that error.
    if (fabs(denominator) > parallel_error) {
        t = navigation_cross2(lx, ly, ex, ey) / denominator;
        u = navigation_cross2(lx, ly, dx, dy) / denominator;
    } else {
        double collinear_error = 8 * DBL_EPSILON * (fabs(lx * dy) + fabs(ly * dx));
        if (fabs(navigation_cross2(lx, ly, dx, dy)) > collinear_error)
            return false;
        double length = dx * dx + dy * dy;
        if (length == 0) {
            double edge = ex * ex + ey * ey;
            if (edge == 0)
                return false;
            u = -(lx * ex + ly * ey) / edge;
            t = previous;
        } else {
            double tl = (lx * dx + ly * dy) / length, tr = (rx * dx + ry * dy) / length;
            t = fmax(previous, fmin(tl, tr));
            if (t > fmax(tl, tr) + tolerance)
                return false;
            t = fmin(t, fmax(tl, tr));
            u = tl == tr ? 0 : (t - tl) / (tr - tl);
        }
    }
    // division roundoff at a shared endpoint must not turn a valid corridor into a gap.
    if (!isfinite(t) || !isfinite(u) || t < previous - tolerance || t > 1 + tolerance || u < -tolerance ||
        u > 1 + tolerance)
        return false;
    *parameter = fmax(previous, fmin(1, fmax(0, t)));
    *edge_parameter = fmin(1, fmax(0, u));
    return true;
}
static void nm_append(NavigationMeshPath* path, NavigationVec p) {
    if (path->point_count && nm_same(path->points[path->point_count - 1], p))
        return;
    if (path->point_count > 1) {
        NavigationVec a = path->points[path->point_count - 2], b = path->points[path->point_count - 1];
        double u[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, v[3] = {p.x - b.x, p.y - b.y, p.z - b.z};
        double su = fmax(fmax(fabs(u[0]), fabs(u[1])), fabs(u[2])), sv = fmax(fmax(fabs(v[0]), fabs(v[1])), fabs(v[2]));
        if (su > 0 && sv > 0 && isfinite(su) && isfinite(sv)) {
            for (unsigned i = 0; i < 3; i++) {
                u[i] /= su;
                v[i] /= sv;
            }
            bool collinear = true;
            for (unsigned i = 0; i < 3; i++)
                collinear &= navigation_cross2(u[(i + 1) % 3], u[(i + 2) % 3], v[(i + 1) % 3], v[(i + 2) % 3]) == 0;
            if (collinear && math_dot3(u, v) >= 0) {
                path->points[path->point_count - 1] = p;
                return;
            }
        }
    }
    path->points[path->point_count++] = p;
}
bool navigation_waypoint_append(NavigationMeshPath* path, NavigationVec p, double unit_cost, NavigationError* error) {
    double distance = path->point_count ? navigation_vec_distance(path->points[path->point_count - 1], p) : 0,
           cost = distance * unit_cost;
    if (!navigation_vec_finite(p) || !isfinite(distance) || !isfinite(cost) || !isfinite(path->length + distance) ||
        !isfinite(path->travel_cost + cost))
        return nm_fail(error, "navigation: grounded waypoint distance/cost outside numerical range", 0);
    path->length += distance;
    path->travel_cost += cost;
    nm_append(path, p);
    return true;
}
bool navigation_surface_assemble(unsigned dimensions, const NavigationMeshQuery* query, NavigationMeshPath* path,
                                 const double* costs, const NavigationPortal* arrivals, NavigationError* error) {
    if (!query || !path || !costs || (dimensions != 2 && dimensions != 3) || !path->corridor.count ||
        path->corridor.count > NAVIGATION_MAX_NODES || path->points || path->portal_count != path->corridor.count - 1 ||
        (path->portal_count && !path->portals))
        return nm_fail(error, "navigation: invalid convex corridor assembly", 0);
    size_t count = path->corridor.count;
    NMeshCorner* corners = mem_alloc((count + 1) * sizeof(NMeshCorner), MEM_CAT_EVAL);
    // a discontinuous portal needs both its departure and its landing point.
    path->points = mem_alloc((count + 1 + (arrivals ? count - 1 : 0)) * sizeof(NavigationVec), MEM_CAT_EVAL);
    if (!corners || !path->points) {
        mem_free(corners);
        return nm_fail(error, "navigation: surface path allocation failed", 0);
    }
    bool valid = false;
    size_t corner_count = 1, left_index = 0, right_index = 0;
    NavigationVec apex = path->projected_start, left = apex, right = apex;
    corners[0] = (NMeshCorner){apex, 0};
    for (size_t i = 1; i <= count; i++) {
        if (!navigation_surface_work(query, path))
            goto done;
        NavigationPortal portal = nm_portal(path, i);
        double right_turn = navigation_surface_turn(apex, right, portal.right, dimensions),
               left_turn = navigation_surface_turn(apex, left, portal.left, dimensions);
        if (!isfinite(right_turn) || !isfinite(left_turn)) {
            nm_fail(error, "navigation: funnel orientation outside numerical range", i);
            goto done;
        }
        if (right_turn >= 0) {
            if (nm_same_plane(apex, right, dimensions) ||
                navigation_surface_turn(apex, left, portal.right, dimensions) <= 0) {
                right = portal.right;
                right_index = i;
            } else {
                if (corner_count > count || left_index <= corners[corner_count - 1].portal) {
                    nm_fail(error, "navigation: funnel failed to advance", i);
                    goto done;
                }
                corners[corner_count++] = (NMeshCorner){left, left_index};
                apex = left;
                right = left = apex;
                i = left_index;
                right_index = left_index;
                continue;
            }
        }
        if (left_turn <= 0) {
            if (nm_same_plane(apex, left, dimensions) ||
                navigation_surface_turn(apex, right, portal.left, dimensions) >= 0) {
                left = portal.left;
                left_index = i;
            } else {
                if (corner_count > count || right_index <= corners[corner_count - 1].portal) {
                    nm_fail(error, "navigation: funnel failed to advance", i);
                    goto done;
                }
                corners[corner_count++] = (NMeshCorner){right, right_index};
                apex = right;
                left = right = apex;
                i = right_index;
                left_index = right_index;
                continue;
            }
        }
    }
    if (corners[corner_count - 1].portal != count)
        corners[corner_count++] = (NMeshCorner){path->path_end, count};
    nm_append(path, path->projected_start);
    for (size_t k = 1; k < corner_count; k++) {
        NMeshCorner a = corners[k - 1], b = corners[k];
        double previous_t = 0;
        for (size_t i = a.portal + 1; i <= b.portal && i < count; i++) {
            if (!navigation_surface_work(query, path))
                goto done;
            double t, u;
            NavigationPortal portal = path->portals[i - 1];
            if (!nm_intersect(a.p, b.p, portal, dimensions, previous_t, &t, &u)) {
                nm_fail(error, "navigation: straight path does not cross its corridor in order", i);
                goto done;
            }
            NavigationVec p = i == b.portal ? b.p
                                            : (NavigationVec){portal.left.x + (portal.right.x - portal.left.x) * u,
                                                              portal.left.y + (portal.right.y - portal.left.y) * u,
                                                              portal.left.z + (portal.right.z - portal.left.z) * u};
            if (!navigation_waypoint_append(path, p, costs[i - 1], error))
                goto done;
            if (arrivals) {
                NavigationPortal arrival = arrivals[i - 1];
                NavigationVec landing = {arrival.left.x + (arrival.right.x - arrival.left.x) * u,
                                         arrival.left.y + (arrival.right.y - arrival.left.y) * u,
                                         arrival.left.z + (arrival.right.z - arrival.left.z) * u};
                if (!navigation_waypoint_append(path, landing, costs[i], error))
                    goto done;
            }
            previous_t = t;
        }
        if (b.portal == count && !navigation_waypoint_append(path, b.p, costs[count - 1], error))
            goto done;
    }
    valid = true;
done:
    mem_free(corners);
    return valid;
}
static bool nm_assemble(const NavigationMesh* mesh, const NavigationMeshQuery* query, NavigationMeshPath* path,
                        NavigationError* error) {
    size_t count = path->corridor.count;
    double* costs = mem_alloc(count * sizeof(double), MEM_CAT_EVAL);
    path->portal_count = count - 1;
    if (path->portal_count)
        path->portals = mem_alloc(path->portal_count * sizeof(NavigationPortal), MEM_CAT_EVAL);
    bool valid = costs && (!path->portal_count || path->portals);
    if (!valid)
        nm_fail(error, "navigation: surface portal allocation failed", 0);
    for (size_t i = 0; i < count && valid; i++) {
        const NMeshTriangle* from = &mesh->triangles[path->corridor.nodes[i] - 1];
        costs[i] = from->d.cost;
        if (i + 1 == count)
            continue;
        unsigned side = 0;
        while (side < 3 && from->neighbors[side] != path->corridor.nodes[i + 1])
            side++;
        if (side == 3) {
            valid = nm_fail(error, "navigation: corridor has no shared portal", path->corridor.nodes[i]);
            break;
        }
        path->portals[i] = (NavigationPortal){mesh->vertices[from->d.vertices[(side + 1) % 3]],
                                              mesh->vertices[from->d.vertices[side]]};
    }
    if (valid)
        valid = navigation_surface_assemble(mesh->dimensions, query, path, costs, NULL, error);
    mem_free(costs);
    return valid;
}

void navigation_mesh_path_clear(NavigationMeshPath* path) {
    if (!path)
        return;
    navigation_path_clear(&path->corridor);
    mem_free(path->points);
    mem_free(path->portals);
    mem_free(path->traversals);
    memset(path, 0, sizeof(*path));
}
void navigation_surface_discard_route(NavigationMeshPath* path) {
    mem_free(path->corridor.nodes);
    path->corridor.nodes = NULL;
    path->corridor.count = 0;
    path->corridor.cost = 0;
    mem_free(path->points);
    path->points = NULL;
    path->point_count = 0;
    mem_free(path->portals);
    path->portals = NULL;
    path->portal_count = 0;
    mem_free(path->traversals);
    path->traversals = NULL;
    path->traversal_count = 0;
    path->length = path->travel_cost = 0;
    path->corridor.status = NAVIGATION_EXHAUSTED;
}
bool navigation_surface_query_valid(const NavigationMeshQuery* query, const NavigationMeshPath* result,
                                    unsigned dimensions) {
    return query && result && !result->corridor.nodes && !result->points && !result->portals && !result->traversals &&
           navigation_vec_finite(query->start) && navigation_vec_finite(query->goal) &&
           (dimensions != 2 || (query->start.z == 0 && query->goal.z == 0)) &&
           isfinite(query->max_projection_distance) && query->max_projection_distance >= 0 &&
           query->max_projection_tests <= 2 * NAVIGATION_MAX_NODES &&
           query->max_portal_tests <= 16 * NAVIGATION_MAX_EDGES;
}
NavigationMesh* navigation_mesh_retain(NavigationMesh* mesh) {
    if (mesh)
        __atomic_add_fetch(&mesh->references, 1, __ATOMIC_RELAXED);
    return mesh;
}
static void* nm_retain_owner(void* owner) {
    return navigation_mesh_retain(owner);
}
static void nm_release_owner(void* owner) {
    navigation_mesh_destroy(owner);
}
static bool nm_view_closest(const void* owner, uint64_t polygon, NavigationVec point, NavigationVec* result) {
    return nm_closest(owner, polygon, point, result);
}
static double nm_view_cost(const void* owner, uint64_t polygon) {
    return ((const NavigationMesh*)owner)->triangles[polygon - 1].d.cost;
}
static bool nm_view_assemble(const void* owner, const NavigationMeshQuery* query, NavigationMeshPath* path,
                             NavigationError* error) {
    return nm_assemble(owner, query, path, error);
}
static bool nm_view_equal(const void* left, const void* right) {
    return navigation_mesh_equal(left, right);
}
static bool nm_view_polygon(const void* owner, uint64_t polygon, NavigationVec* vertices, size_t capacity,
                            size_t* count) {
    const NavigationMesh* mesh = owner;
    if (!polygon || polygon > mesh->triangle_count || !vertices || capacity < 3 || !count)
        return false;
    const uint32_t* indices = mesh->triangles[polygon - 1].d.vertices;
    for (unsigned i = 0; i < 3; i++)
        vertices[i] = mesh->vertices[indices[i]];
    *count = 3;
    return true;
}
bool navigation_mesh_surface(NavigationMesh* mesh, NavigationSurfaceView* view) {
    if (!mesh || !view)
        return false;
    *view = (NavigationSurfaceView){.dimensions = mesh->dimensions,
                                    .polygon_count = mesh->triangle_count,
                                    .graph = mesh->graph,
                                    .owner = mesh,
                                    .retain = nm_retain_owner,
                                    .release = nm_release_owner,
                                    .closest = nm_view_closest,
                                    .unit_cost = nm_view_cost,
                                    .assemble = nm_view_assemble,
                                    .equal = nm_view_equal,
                                    .polygon = nm_view_polygon};
    return true;
}
bool navigation_surface_find(const NavigationSurfaceView* view, const NavigationSurfaceQuery* request,
                             NavigationMeshPath* result, NavigationError* error) {
    if (!navigation_surface_profile_valid(view, request, error))
        return false;
    const NavigationMeshQuery* query = &request->path;
    if (!navigation_surface_query_valid(query, result, view->dimensions))
        return nm_fail(error, "navigation: invalid surface query, output or work budget", 0);
    NavigationMeshPath path = {0};
    NavigationError failed = {0};
    bool valid = false;
    bool start = navigation_surface_project(view, query, query->start, &path.projected_start, &path.start_polygon,
                                            &path.start_distance, &path, &failed);
    bool goal = start && navigation_surface_project(view, query, query->goal, &path.projected_goal, &path.goal_polygon,
                                                    &path.goal_distance, &path, &failed);
    if (!start || !goal) {
        if (failed.message)
            goto done;
        path.corridor.status = NAVIGATION_EXHAUSTED;
        valid = true;
        goto done;
    }
    if (!path.start_polygon || !path.goal_polygon) {
        path.corridor.status = NAVIGATION_UNREACHABLE;
        valid = true;
        goto done;
    }
    NavigationQuery search = query->search;
    search.start = path.start_polygon;
    search.goal = path.goal_polygon;
    if (!navigation_find(view->graph, &search, &path.corridor, &failed))
        goto done;
    if (!path.corridor.count) {
        valid = true;
        goto done;
    }
    path.path_end = path.projected_goal;
    if (path.corridor.status != NAVIGATION_COMPLETE &&
        !view->closest(view->owner, path.corridor.nodes[path.corridor.count - 1], query->goal, &path.path_end)) {
        nm_fail(&failed, "navigation: partial surface endpoint outside numerical range", 0);
        goto done;
    }
    if (!view->assemble(view->owner, query, &path, &failed)) {
        if (failed.message)
            goto done;
        // a corridor without verified grounded points cannot be issued as a movement path.
        navigation_surface_discard_route(&path);
    }
    valid = true;
done:
    if (valid)
        *result = path;
    else {
        navigation_mesh_path_clear(&path);
        if (error)
            *error = failed;
    }
    return valid;
}
bool navigation_mesh_find(const NavigationMesh* mesh, const NavigationMeshQuery* query, NavigationMeshPath* result,
                          NavigationError* error) {
    NavigationSurfaceView view = {0};
    if (!navigation_mesh_surface((NavigationMesh*)mesh, &view))
        return nm_fail(error, "navigation: missing surface", 0);
    NavigationSurfaceQuery request = {0};
    if (!query)
        return nm_fail(error, "navigation: missing surface query", 0);
    request.path = *query;
    return navigation_surface_find(&view, &request, result, error);
}
