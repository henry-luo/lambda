#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/dom_lifecycle.hpp"
#include "../../lib/digest.h"
#include "../../lib/hash.h"
#include "../../lib/hex.h"
#include "../../lib/mempool.h"
#include "../../lib/mesh_primitive.h"
#include "../../lib/str.h"
#include "../../lib/strbuf.h"
#include "scene3d_source.hpp"
#include <float.h>

bool scene3d_read_geometry(DomElement *node, Pool *pool, Scene3dGeometrySource *source, char *diagnostic,
                           size_t capacity, unsigned vertex_limit, unsigned index_limit) {
    auto fail = [&](const char* message) {
        str_copy(diagnostic, capacity, message, strlen(message));
        return false;
    };
    if (!node || !pool || !source)
        return fail("geometry source requires a node, pool and output");
    if (!vertex_limit || vertex_limit > SCENE3D_GEOMETRY_VERTEX_LIMIT || !index_limit ||
        index_limit > SCENE3D_GEOMETRY_VERTEX_LIMIT * 6)
        return fail("invalid geometry vertex budget");
    *source = {};
    if (node->first_child) {
        return fail("geometry cannot have children");
    }
    const char* type = scene3d_text(node, "type");
    float* raw_indices = nullptr;
    const char* uv_mode = scene3d_text(node, "drawable-uvs");
    if (uv_mode && strcmp(uv_mode, "source") && strcmp(uv_mode, "surface")) {
        return fail("invalid drawable-uvs mode");
    }
    source->source_uvs = uv_mode && !strcmp(uv_mode, "source");
    unsigned pc = 0, nc = 0, uc = 0, uc1 = 0, cc = 0, tc = 0, sc = 0, ic = 0, dc[4] = {};
    if (type && (!strcmp(type, "box") || !strcmp(type, "plane"))) {
        bool box = !strcmp(type, "box");
        Scene3dVec size;
        if (!scene3d_vector(node, "size", {1, 1, 1}, &size) || size.x <= 0 || size.y <= 0 || size.z <= 0) {
            return fail("invalid primitive size");
        }
        source->vertex_count = box ? MESH_BOX_VERTICES : MESH_PLANE_VERTICES;
        source->index_count = box ? MESH_BOX_INDICES : MESH_PLANE_INDICES;
        if (source->vertex_count > vertex_limit)
            return fail("geometry vertex budget exceeded");
        source->positions = (float*)pool_alloc(pool, source->vertex_count * 3 * sizeof(float));
        source->normals = (float*)pool_alloc(pool, source->vertex_count * 3 * sizeof(float));
        source->uvs = (float*)pool_alloc(pool, source->vertex_count * 2 * sizeof(float));
        source->indices = (uint32_t*)pool_alloc(pool, source->index_count * sizeof(uint32_t));
        if (!source->positions || !source->normals || !source->uvs || !source->indices)
            return fail("geometry source allocation failed");
        float sizes[] = {size.x, size.y, size.z};
        mesh_primitive(box, sizes, source->positions, source->normals, source->uvs, source->indices);
        pc = source->vertex_count * 3;
        nc = pc;
        uc = source->vertex_count * 2;
    } else if (type && !strcmp(type, "buffer")) {
        struct Stream {
            const char* key;
            float** data;
            unsigned* count;
            unsigned limit;
        };
        Stream streams[] = {{"positions", &source->positions, &pc, vertex_limit * 3},
                            {"normals", &source->normals, &nc, vertex_limit * 3},
                            {"uvs", &source->uvs, &uc, vertex_limit * 2},
                            {"uvs1", &source->uvs1, &uc1, vertex_limit * 2},
                            {"colors", &source->colors, &cc, vertex_limit * 4},
                            {"tangents", &source->tangents, &tc, vertex_limit * 4},
                            {"indices", &raw_indices, &ic, index_limit}};
        for (const auto& stream : streams)
            if (!scene3d_numbers(node, stream.key, pool, stream.data, stream.count, stream.limit)) {
                return fail("invalid geometry component counts");
            }
        if (!pc || pc % 3 || (nc && nc != pc) || (uc && uc != pc / 3 * 2) || (uc1 && uc1 != pc / 3 * 2) ||
            (cc && cc != pc && cc != pc / 3 * 4) || (tc && tc != pc / 3 * 4)) {
            return fail("invalid geometry component counts");
        }
        source->vertex_count = pc / 3;
        source->index_count = ic;
        source->color_components = cc ? cc / source->vertex_count : 0;
        if (ic) {
            source->indices = (uint32_t*)pool_alloc(pool, ic * sizeof(uint32_t));
            if (!source->indices)
                return fail("geometry source allocation failed");
        }
        for (unsigned i = 0; i < ic; i++) {
            if (raw_indices[i] < 0 || raw_indices[i] >= source->vertex_count ||
                floorf(raw_indices[i]) != raw_indices[i]) {
                return fail("geometry index out of bounds");
            }
            source->indices[i] = (uint32_t)raw_indices[i];
        }
    } else {
        return fail("unknown geometry type");
    }
    if (!scene3d_numbers(node, "point-sizes", pool, &source->sizes, &sc, vertex_limit) ||
        (sc && sc != source->vertex_count)) {
        return fail("point-sizes needs one positive number per vertex");
    }
    for (unsigned i = 0; i < sc; i++)
        if (source->sizes[i] <= 0) {
            return fail("point-sizes needs one positive number per vertex");
        }
    const char* deformation_keys[] = {"skin-indices", "skin-weights", "morph-positions", "morph-normals"};
    for (unsigned i = 0; i < 4; i++)
        if (!scene3d_numbers(node, deformation_keys[i], pool, &source->deformation[i], &dc[i], vertex_limit * 6)) {
            return fail("invalid geometry deformation data");
        }
    unsigned nv = source->vertex_count, morph_size = nv * 3;
    if ((dc[0] && dc[0] != nv * 4) || dc[0] != dc[1] || dc[2] % morph_size || dc[2] > morph_size * 2 ||
        (dc[3] && dc[3] != dc[2])) {
        return fail("invalid skin or morph component counts");
    }
    source->morph_count = dc[2] / morph_size;
    source->lo = {FLT_MAX, FLT_MAX, FLT_MAX};
    source->hi = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (unsigned i = 0; i < nv; i++) {
        const float* v = source->positions + i * 3;
        source->lo = {fminf(source->lo.x, v[0]), fminf(source->lo.y, v[1]), fminf(source->lo.z, v[2])};
        source->hi = {fmaxf(source->hi.x, v[0]), fmaxf(source->hi.y, v[1]), fmaxf(source->hi.z, v[2])};
        if (nc) {
            Scene3dVec n = {source->normals[i * 3], source->normals[i * 3 + 1], source->normals[i * 3 + 2]};
            if (!scene3d_normalize(&n)) {
                return fail("degenerate geometry normals");
            }
        }
        if (cc)
            for (unsigned c = 0; c < source->color_components; c++) {
                float* color = &source->colors[i * source->color_components + c];
                if (*color < 0 || *color > 1) {
                    return fail("vertex color out of range");
                }
                if (c < 3)
                    *color = scene3d_linear(*color);
                else
                    source->vertex_alpha |= *color < 1;
            }
        if (tc) {
            const float* t = source->tangents + i * 4;
            if (fabsf(t[3]) != 1 || fabsf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2] - 1) > 1e-2f) {
                return fail("invalid geometry tangent");
            }
        }
        if (dc[0]) {
            float sum = 0;
            for (unsigned c = 0; c < 4; c++) {
                float index = source->deformation[0][i * 4 + c], weight = source->deformation[1][i * 4 + c];
                if (index < 0 || index >= SCENE3D_GEOMETRY_BONE_LIMIT || floorf(index) != index || weight < 0 ||
                    weight > 1) {
                    return fail("invalid skin index or weight");
                }
                sum += weight;
                if (weight > 0 && source->bone_index_count < (unsigned)index + 1)
                    source->bone_index_count = (unsigned)index + 1;
            }
            if (fabsf(sum - 1) > 1e-5f) {
                return fail("skin weights must sum to one");
            }
        }
    }
    return true;
}

DomElement *scene3d_find_source(DomElement *root, const char *id, bool *valid) {
    if (!root || !id || !valid) {
        if (valid)
            *valid = false;
        return nullptr;
    }
    DomElement* found = nullptr;
    unsigned work = 0;
    *valid = true;
    for (DomNode* node = root; node; node = node->is_element() && node->as_element()->first_child
                                                ? node->as_element()->first_child.get()
                                                : dom_next_after_subtree(root, node)) {
        if (++work > 8192) {
            *valid = false;
            return nullptr;
        }
        if (!node->is_element())
            continue;
        const char* name = scene3d_text(node->as_element(), "id");
        if (name && !strcmp(name, id)) {
            if (found) {
                *valid = false;
                return nullptr;
            }
            found = node->as_element();
        }
    }
    return found;
}
struct GeometrySelection {
    DomElement *node = nullptr, *definition = nullptr;
    Scene3dMatrix world;
};
static const char* geometry_selection(DomElement* root, const Scene3dNavigationSelection& selection,
                                      GeometrySelection* result) {
    if (!selection.id || !*selection.id || strlen(selection.id) >= 128 || !isfinite(selection.cost) ||
        selection.cost < 0)
        return "invalid selection ID or area cost";
    bool valid;
    result->node = scene3d_find_source(root, selection.id, &valid);
    if (!valid)
        return "ambiguous source ID or scene node quota exceeded";
    if (!result->node || strcmp(result->node->local_name(), "mesh"))
        return "requires an explicitly selected mesh";
    if (scene3d_text(result->node, "skeleton"))
        return "skinned sources require an explicit pose snapshot";
    float instances;
    if (!scene3d_number(result->node, "instance-count", 1, &instances) || instances != 1)
        return "instanced sources require an explicit instance snapshot";
    const char* reference = scene3d_text(result->node, "geometry");
    if (reference) {
        result->definition = scene3d_find_source(root, reference, &valid);
        if (!valid || !result->definition || strcmp(result->definition->local_name(), "geometry"))
            return "missing or ambiguous geometry reference";
    } else
        for (DomNode* child = result->node->first_child; child; child = child->next_sibling)
            if (child->is_element() && !strcmp(child->as_element()->local_name(), "geometry")) {
                if (result->definition)
                    return "multiple inline geometries";
                result->definition = child->as_element();
            }
    if (!result->definition)
        return "missing source geometry";
    bool visible;
    if (!scene3d_world_pose(root, result->node, nullptr, nullptr, nullptr, &result->world, &visible, true))
        return "invalid authored source transform";
    return nullptr;
}
bool scene3d_navigation_geometry_key(DomElement* root, const Scene3dNavigationSelection* selections, size_t count,
                                     uint64_t* key, char* diagnostic, size_t capacity) {
    if (!root || !selections || !count || count > 64 || !key)
        return false;
    uint64_t hash = 5381;
    unsigned work = 0;
    for (size_t i = 0; i < count; i++) {
        GeometrySelection selected;
        const char* error = geometry_selection(root, selections[i], &selected);
        if (error) {
            str_copy(diagnostic, capacity, error, strlen(error));
            return false;
        }
        if (!scene3d_source_key(selected.definition, &hash, &work, 0, false)) {
            const char* message = "navigation source fingerprint quota exceeded";
            str_copy(diagnostic, capacity, message, strlen(message));
            return false;
        }
        // only authored source transforms participate; a camera edit never decodes the geometry.
        hash = hash_djb2_add_extend(hash, selected.world.v, sizeof(selected.world.v));
    }
    *key = hash;
    return true;
}
bool scene3d_collect_navigation_geometry(DomElement* root, const Scene3dNavigationSelection* selections, size_t count,
                                         Scene3dNavigationGeometry* output, lam::ArrayList<DomNodeRef>* references,
                                         char* diagnostic, size_t capacity) {
    const char* selected = "";
    auto fail = [&](const char* message) {
        StrBuf* text = strbuf_new();
        if (!text)
            return false;
        strbuf_append_str(text, "navigation source ");
        strbuf_append_str(text, selected);
        strbuf_append_str(text, ": ");
        strbuf_append_str(text, message);
        str_copy(diagnostic, capacity, text->str, text->length);
        strbuf_free(text);
        return false;
    };
    if (!root || !root->doc || strcmp(root->local_name(), "scene3d") || !output || !selections || !count || count > 64)
        return fail("requires a scene and 1..64 source selections");
    struct Scratch {
        Pool* pool = pool_create();
        DigestCtx* digest = digest_ctx_new(256);
        ~Scratch() {
            digest_ctx_free(digest);
            if (pool)
                pool_destroy(pool);
        }
    } scratch;
    if (!scratch.pool || !scratch.digest)
        return fail("snapshot allocation failed");
    struct CachedGeometry {
        DomElement* node;
        Scene3dGeometrySource source;
    };
    lam::ArrayList<CachedGeometry> cache;
    lam::ArrayList<DomNodeRef> refs;
    Scene3dNavigationGeometry geometry;
    unsigned vertices = 0;
    if (!digest_update_u64_le(scratch.digest, 1) || !digest_update_u64_le(scratch.digest, count))
        return fail("source digest failed");
    for (size_t i = 0; i < count; i++) {
        selected = selections[i].id;
        GeometrySelection resolved;
        const char* error = geometry_selection(root, selections[i], &resolved);
        if (error) {
            if (!selected)
                selected = "";
            return fail(error);
        }
        for (size_t j = 0; j < i; j++)
            if (!strcmp(selected, selections[j].id))
                return fail("duplicate source selection");
        auto* node = resolved.node;
        auto* definition = resolved.definition;
        Scene3dGeometrySource* source = nullptr;
        for (auto& entry : cache)
            if (entry.node == definition) {
                source = &entry.source;
                break;
            }
        if (!source) {
            CachedGeometry entry = {};
            entry.node = definition;
            char message[256] = {};
            if (!scene3d_read_geometry(definition, scratch.pool, &entry.source, message, sizeof(message),
                                       SCENE3D_GEOMETRY_VERTEX_LIMIT - vertices))
                return fail(message);
            vertices += entry.source.vertex_count;
            if (!cache.append(entry))
                return fail("geometry cache allocation failed");
            source = &cache[cache.size() - 1].source;
        }
        if (source->bone_index_count || source->morph_count)
            return fail("deformed sources require an explicit pose snapshot");
        unsigned indices = source->index_count ? source->index_count : source->vertex_count;
        if (indices % 3 || geometry.triangles.size() + indices / 3 > SCENE3D_BAKE_TRIANGLE_LIMIT)
            return fail("triangle topology or snapshot quota exceeded");
        Scene3dMatrix world = resolved.world;
        if (!refs.append(dom_node_ref(node)) || !refs.append(dom_node_ref(definition)))
            return fail("source reference allocation failed");
        if (!digest_update_u64_le(scratch.digest, strlen(selected)) ||
            !digest_update(scratch.digest, selected, strlen(selected)) ||
            !digest_update_u64_le(scratch.digest, indices / 3))
            return fail("source digest failed");
        for (unsigned offset = 0; offset < indices; offset += 3) {
            Scene3dNavigationTriangle triangle = {};
            triangle.walkable = selections[i].walkable;
            triangle.groups = selections[i].groups;
            triangle.cost = selections[i].cost;
            for (unsigned corner = 0; corner < 3; corner++) {
                uint32_t index = source->indices ? source->indices[offset + corner] : offset + corner;
                const float* p = source->positions + 3 * index;
                Scene3dVec transformed = scene3d_point(world, {p[0], p[1], p[2]});
                if (!isfinite(transformed.x) || !isfinite(transformed.y) || !isfinite(transformed.z))
                    return fail("source transform overflow");
                triangle.vertices[corner] = {transformed.x, transformed.y, transformed.z};
                if (!digest_update_f64_le(scratch.digest, transformed.x) ||
                    !digest_update_f64_le(scratch.digest, transformed.y) ||
                    !digest_update_f64_le(scratch.digest, transformed.z))
                    return fail("source digest failed");
            }
            if (!digest_update_u64_le(scratch.digest, triangle.walkable) ||
                !digest_update_u64_le(scratch.digest, triangle.groups) ||
                !digest_update_f64_le(scratch.digest, triangle.cost) || !geometry.triangles.append(triangle))
                return fail("snapshot allocation or digest failed");
        }
    }
    uint8_t digest[32];
    if (!digest_finalize(scratch.digest, digest, sizeof(digest)))
        return fail("source digest finalization failed");
    hex_encode(digest, sizeof(digest), geometry.source_hash);
    *output = static_cast<Scene3dNavigationGeometry&&>(geometry);
    if (references)
        *references = static_cast<lam::ArrayList<DomNodeRef>&&>(refs);
    return true;
}
