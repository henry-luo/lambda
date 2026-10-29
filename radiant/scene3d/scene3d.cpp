#include "scene3d.hpp"
#include "scene3d_math.hpp"
#include "scene3d_source.hpp"
#include "scene3d_animation.hpp"
#include "scene3d_physics.hpp"
#include "scene3d_post_shaders.hpp"
#include "../render.hpp"
#include "../radiant.hpp"
#include "../layout.hpp"
#include "../../lambda/core/mark_reader.hpp"
#include "../../lambda/core/physics_scene.hpp"
#include "../../lambda/runtime/lambda-root-frame.hpp"
#include "../../lambda/runtime/lambda-error.h"
#include "../../lib/strbuf.h"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/dom_lifecycle.hpp"
#include "../../lib/color.h"
#include "../../lib/mem.h"
#include "../../lib/mem_factory.h"
#include "../../lib/mem_grow.hpp"
#include "../../lib/mesh_primitive.h"
#include "../../lib/scene3d_material.h"
#include "../../lib/scene3d_drawable.h"
#include "scene3d_physical_shaders.hpp"
#include "scene3d_drawable_shaders.hpp"
#include "scene3d_outdoor_shaders.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/hash.h"
#include "../../lib/hashmap.h"
#include "../../lib/image_hdr.h"
#include "../../lib/file.h"
#include "../../lib/url.h"
#include <ctype.h>
#include <float.h>

static constexpr unsigned SCENE_NODE_LIMIT = 8192;
static constexpr unsigned SCENE_EFFECT_LIMIT = 8;
static constexpr unsigned SCENE_TRANSMISSION_FEATURE=1u<<(SCENE_MAP_COUNT-SCENE_MAP_CLEARCOAT);
static constexpr unsigned SCENE_DRAWABLE_SHIFT=SCENE_MAP_COUNT-SCENE_MAP_CLEARCOAT+1;
// bones per skeleton: the palette is a data texture, so the cap is a resource
// bound rather than the vertex-uniform space GL 4.1 guarantees (64 mat4)
static constexpr unsigned SCENE_BONE_LIMIT = 256;
static constexpr unsigned SCENE_VERTEX_LIMIT = 262144;
static constexpr unsigned SCENE_INSTANCE_LIMIT = 16384;
static constexpr unsigned SCENE_TONE_CURVES = 3;
// material model (basic/Lambert, standard) x tone curve x alpha mask x environment light; a
// shader that can discard changes multisampled depth even where it never does, so masks get their own
// material model x tone curve x alpha mask x environment light x shadow layout (none, or which of
// the four shadow slots are point-light cubes: 16 layouts)
static constexpr unsigned SCENE_SHADOW_LAYOUTS = 17;
static constexpr unsigned SCENE_PROGRAM_VARIANTS = 2 * SCENE_TONE_CURVES * 2 * 2 * 2 * SCENE_SHADOW_LAYOUTS;
// lights that cast shadows (SP5): each owns a depth map, sampled through its own texture unit
static constexpr unsigned SCENE_SHADOW_LIMIT = 4;
// A prefiltered environment is Three.js r186's PMREM cube-UV atlas: 256-texel faces, then
// halving levels down to 16 texels and six more 16-texel levels of rising roughness.
static constexpr unsigned SCENE_ENV_SIZE = 256, SCENE_ENV_LODS = 11, SCENE_ENV_LOD_MAX = 8, SCENE_ENV_LOD_MIN = 4;
// Shader materials (SH1–SH3): Lambda-authored GLSL bodies behind a host prefix. A uniform name stays
// short enough for its animation path `uniforms.<name>` to fit a playback binding.
static constexpr unsigned SCENE_SHADER_UNIFORM_LIMIT = 32, SCENE_SHADER_SAMPLER_LIMIT = 8, SCENE_SHADER_VALUE_LIMIT = 256,
    SCENE_SHADER_NAME_LIMIT = 23, SCENE_SHADER_PROGRAM_LIMIT = 32, SCENE_INSTANCE_DATA_LIMIT = 6;
// GPU simulations (SH5): per scene, variables per simulation, records per variable, and the fixed
// steps one frame may take before a far-behind simulation resynchronizes with its time
static constexpr unsigned SCENE_SIMULATION_LIMIT = 4, SCENE_VARIABLE_LIMIT = 4, SCENE_SIMULATION_RECORDS = 262144,
    SCENE_SIMULATION_STEPS = 240;

// scratch vertices are packed by one shared lane table before upload and VAO construction.
struct Scene3dVertex {
    float position[3], normal[3], uv[2], color[4];
    float tangent[4]; // xyz along +u, w the handedness of +v
    float uv1[2];
    float skin_indices[4], skin_weights[4], morph_positions[2][3], morph_normals[2][3];
};
struct Scene3dLane { unsigned location, components, offset; };
static const Scene3dLane scene3d_lanes[]={
    {0,3,offsetof(Scene3dVertex,position)},{1,3,offsetof(Scene3dVertex,normal)},{2,2,offsetof(Scene3dVertex,uv)},
    {3,4,offsetof(Scene3dVertex,color)},{14,4,offsetof(Scene3dVertex,tangent)},{15,2,offsetof(Scene3dVertex,uv1)},
    {8,4,offsetof(Scene3dVertex,skin_indices)},{9,4,offsetof(Scene3dVertex,skin_weights)},
    {10,3,offsetof(Scene3dVertex,morph_positions)},{11,3,offsetof(Scene3dVertex,morph_positions)+3*sizeof(float)},
    {12,3,offsetof(Scene3dVertex,morph_normals)},{13,3,offsetof(Scene3dVertex,morph_normals)+3*sizeof(float)}
};
static constexpr unsigned SCENE_LANE_COUNT=sizeof(scene3d_lanes)/sizeof(*scene3d_lanes);
// source streams are pool-owned and topology-neutral; consumers cache their own normalized uploads.
struct Scene3dGeometrySource {
    unsigned vertex_count,index_count,color_components,morph_count,bone_index_count;
    float *positions,*normals,*uvs,*uvs1,*colors,*tangents,*sizes,*deformation[4];
    uint32_t* indices;
    bool vertex_alpha, source_uvs;
    Scene3dVec lo,hi;
};
struct Scene3dGeometry {
    NativeGlResource vertices, indices;
    unsigned vertex_count, index_count, vertex_stride;
    Scene3dTopology topology;
    Scene3dVec center;
    NativeGlResource unit_vertices;
    unsigned bone_index_count, morph_count;
    bool has_colors, has_tangents, rgba, vertex_alpha, has_uv0, has_uv1, source_uvs;
    unsigned lanes, offsets[SCENE_LANE_COUNT];
    uint64_t hash; // of the uploaded vertices and indices, so environment captures survive re-projection
    Scene3dVec lo, hi; // bounds of the undeformed vertices, which fit shadow frustums
    float maximum_point_size;
};
struct Scene3dMaterial {
    DomNodeRef source;
    float color[4], emissive[3];   // emissive is linear color times its intensity
    float pbr[2];                  // standard materials: metalness, roughness
    // standard materials' maps (metalness in B and roughness in G; occlusion in R) and their strengths
    NativeGlResource maps[SCENE_MAP_COUNT];
    float normal_scale, occlusion_strength;
    float outdoor[SCENE_OUTDOOR_COUNT],foliage_color[3];
    float alpha_cutoff;            // alpha-mode 'mask': fragments below it are discarded; 0 otherwise
    bool source_transparent;
    bool lambert, standard, physical, transparent, force_opaque, depth_only;
    int side;
    float physical_values[SCENE_PHYSICAL_COUNT], physical_colors[SCENE_PHYSICAL_COLORS][3], dielectric;
    bool transmission; // declared factor/map/track; numeric playback does not change the program key
    float uv_rows[SCENE_MAP_COUNT][6], uv_sets[SCENE_MAP_COUNT]; // base, metal-rough, normal, occlusion, emission
    unsigned required_uvs;
    uint64_t texture_hash; // of its textures' sources and samplers
    struct Scene3dShader* shader; // a shader material's program inputs; null for the built-in models
};
// A shader-material or simulation uniform as authored: numbers (a float, vector, mat4 or an array of
// them; a color string is three linear numbers) or what a sampler reads: a texture, or a simulation
// variable's latest records. `components` per element comes from the linked programs; uniforms no
// program uses keep 0 and are never set.
struct Scene3dShaderUniform {
    char name[SCENE_SHADER_NAME_LIMIT];
    float *authored, *values; // `values` restarts from `authored` each frame, then playback overrides it
    unsigned length, components;
    NativeGlResource texture;
    struct Scene3dSimulation* simulation;
    unsigned variable;
};
// SH5: one simulated quantity: a pair of exact float targets (one record per texel) and the pass
// program that computes the next records from every variable's current ones
struct Scene3dVariable {
    char name[SCENE_SHADER_NAME_LIMIT];
    NativeGlResource state[2], program;
    float* initial; // width × height RGBA records, restored when the simulation resets
};
// A <simulation> belongs to the viewport, not a projection: it survives re-projections while its
// structure (`key`) is unchanged, so uniform-only re-renders keep its state. `uniforms` and `source`
// are refreshed by every projection that still declares it.
struct Scene3dSimulation {
    char id[128];
    uint64_t key, generation;
    unsigned width, height, variable_count, current; // state[current] holds the latest records
    Scene3dVariable variables[SCENE_VARIABLE_LIMIT];
    double clock, timestep; // simulated time, advanced in fixed steps toward the `time` uniform
    bool started;
    DomNodeRef source;
    Scene3dShaderUniform* uniforms;
    unsigned uniform_count;
};
struct Scene3dShader {
    const char *vertex, *fragment; // borrowed from the material element during projection only
    Scene3dShaderUniform* uniforms;
    unsigned uniform_count;
    bool alpha_to_coverage;
};
enum Scene3dEffectKind { SCENE_EFFECT_BLOOM, SCENE_EFFECT_FXAA, SCENE_EFFECT_SHADER };
struct Scene3dEffect {
    DomNodeRef source;
    Scene3dEffectKind kind;
    bool display;
    NativeGlResource program;
    Scene3dShaderUniform* uniforms;
    unsigned uniform_count;
};

// a named per-instance attribute of a shader mesh: `components` floats per instance, at location 8 + its index
struct Scene3dInstanceData {
    char name[SCENE_SHADER_NAME_LIMIT];
    const float* values;
    unsigned components;
};
// a linked shader-material program; it outlives re-projections while the projection built in
// `generation` still uses it, so a re-render that changes only uniforms never recompiles
struct Scene3dShaderProgram {
    uint64_t key, generation;
    NativeGlResource program;
};
static constexpr unsigned SCENE_LOD_LEVEL_LIMIT=16;
struct Scene3dLodChoice {
    Scene3dLodChoice* next;
    DomNodeRef source;
    uint64_t key;
    unsigned selected;
    bool initialized,used;
};
struct Scene3dLod {
    DomNodeRef source;
    Scene3dMatrix world;
    float distances[SCENE_LOD_LEVEL_LIMIT],hysteresis;
    unsigned count;
    Scene3dLodChoice* choice;
};
struct Scene3dLodBranch { Scene3dLod* lod;unsigned level;Scene3dLodBranch* parent; };
struct Scene3dMesh {
    DomNodeRef source;
    Scene3dGeometry* geometry;
    Scene3dMaterial* material;
    Scene3dMatrix world;
    Scene3dMatrix local_instance;
    NativeGlResource instances, vertices;
    unsigned instance_count;
    bool clockwise;
    bool instance_mirrored;
    bool visible;
    unsigned char layer; // SCENE_LAYER_*: the scene, the environment lighting it, or an unused environment
    uint64_t instance_hash;
    const float* instance_matrices; // a batch's matrices (pool-owned), for its shadow bounds; null for one instance
    bool cast_shadow, receive_shadow;
    float depth;
    float drawable_size[2],drawable_center[2];
    bool screen_size;
    bool frustum_culling,authored_bounds,view_visible;
    Scene3dVec bounds_lo,bounds_hi;
    Scene3dLodBranch* lod;
    unsigned order;
    // one record per skeleton bone in preorder, sized to the skeleton
    DomNodeRef* bones;
    Scene3dMatrix *inverse_bind, *bone_matrices, bind_matrix;
    bool* authored_bind;
    unsigned bone_count, bone_capacity;
    // the bone matrices, one RGBA32F row of four texels per bone
    NativeGlResource palette;
    float morph_weights[2], morph_base_weights[2];
    NativeGlResource program; // a shader material's program, linked for this mesh's instance attributes
};
enum { SCENE_LAYER_SCENE, SCENE_LAYER_ENVIRONMENT, SCENE_LAYER_UNUSED };
// The lights of one layer: ambient and hemisphere light outside the quota of 8 others
struct Scene3dLights {
    float ambient[3], directions[8][3], lights[8][3];
    // per light: world position, and (kind 0 directional/1 point/2 spot, distance, cos outer, cos inner)
    float light_positions[8][3], light_params[8][4];
    // the hemisphere light: sky and ground colors times its intensity, and the unit direction toward the sky
    float sky[3], ground[3], hemisphere[3];
    unsigned count;
    bool has_hemisphere;
};
// A light that casts shadows as authored; its frame is fitted each frame.
struct Scene3dShadow {
    unsigned light;             // its index in the scene light set
    unsigned kind;              // the light's: 0 directional, 1 point (a cube map), 2 spot
    Scene3dVec origin, target;
    float angle, distance;      // a spot light's half-angle; a spot or point light's reach (0 fits the casters and receivers)
    float extent;               // a directional light's half-size (0 fits the casters and receivers)
    unsigned size;              // map texels per side (per face for a point light)
    float params[4];            // intensity, bias, normal bias, softness (PCF radius in texels)
    // world to each face's clip space (one face unless a point light), and to the coordinates
    // receivers compare: map coordinates, or a point light's offset from the light
    float clip[6][16], matrix[16];
    float range[2];             // a point light's near and far planes
    unsigned cascade,cascades;
    float cascade_distance,cascade_lambda,cascade_transition,cascade_near,cascade_far;
};
struct Scene3dShadows {
    Scene3dShadow slots[SCENE_SHADOW_LIMIT];
    unsigned count;
};
struct Scene3dFog { float params[4],height[2],color[3];bool declared; };
struct Scene3dProjection {
    Pool* pool;
    Scene3dMesh** meshes;
    int mesh_count, mesh_capacity;
    Scene3dLod** lods;
    int lod_count,lod_capacity;
    bool frustum_culling;
    NativeGlResource identity;
    NativeGlResource* resources;
    int resource_count, resource_capacity;
    Scene3dMatrix camera_world;
    Scene3dEffect effects[SCENE_EFFECT_LIMIT];
    unsigned effect_count, linear_effects, display_effects;
    Scene3dVec camera_target, camera_up;
    bool camera_has_target, camera_found;
    float fov, near_plane, far_plane, aspect, view_height;
    bool orthographic;
    float background[4];
    Scene3dFog fog;
    // the scene's tone-mapping curve (0 none, 1 neutral, 2 ACES) and the exposure it applies
    int tone_mapping;
    float exposure;
    // one light set per layer; an unused environment's lights are validated into `unused`
    Scene3dLights lights, environment_lights, unused_lights;
    Scene3dShadows shadows; // the scene layer's casting lights; environments cast none
    // the <environment> the scene's `environment` names, captured from its origin with its own
    // background and clip planes, and scaled by the scene's `environment-intensity`
    DomNodeRef environment;
    float environment_background[4], environment_near, environment_far, environment_intensity;
    // an image environment (`src`) in place of scene content, and its decoded pixels' hash
    bool environment_imaged;
    uint64_t environment_image_hash;
    unsigned geometry_count, texture_count;
    DomNodeRef* dynamic_objects;
    int dynamic_count, dynamic_capacity;
};
struct Scene3dMappedProgram { unsigned key; NativeGlResource program; uint64_t frame; };
struct Scene3dEntry {
    Scene3dEntry* next;
    Scene3dLodChoice* lod_choices; // hysteresis survives disposable GL projections and context recovery
    DomNodeRef root;
    NativeGlContext* graphics;
    // one shader variant per material model (basic/Lambert, standard) and tone-mapping curve,
    // compiled on first use; `program_frame` marks the frame whose scene uniforms each holds
    NativeGlResource programs[SCENE_PROGRAM_VARIANTS], target, hdr_target, output_programs[SCENE_TONE_CURVES][2], white, dfg;
    uint64_t program_frame[SCENE_PROGRAM_VARIANTS], frame;
    Scene3dMappedProgram* mapped_programs;
    size_t mapped_count,mapped_capacity;
    NativeGlResource post_linear[2], post_blur[2], post_display[2], post_programs[4];
    unsigned post_width, post_height, post_needs;
    NativeGlResource transmission_target,transmission_copy;
    unsigned transmission_width,transmission_height;

    // the prefiltered environment atlas, the ping-pong target its filter passes go through, the
    // filter program and one quad set per atlas level; rebuilt only when `environment_key` changes
    NativeGlResource environment_atlas, environment_pingpong, environment_filter, environment_planes[SCENE_ENV_LODS];
    uint64_t environment_key;
    // the last image environment's equirectangular texture, kept across re-projections by its
    // resolved path, and the program that unwraps it into the atlas
    NativeGlResource environment_image, environment_unwrap;
    char* environment_path;
    uint64_t environment_image_hash;
    // one depth map per casting light, re-rendered only when `shadow_keys` changes; the depth programs
    // (plain and alpha-masked); and a cleared map bound to the samplers of unused shadow slots
    NativeGlResource shadow_maps[SCENE_SHADOW_LIMIT], shadow_programs[2], shadow_blank;
    unsigned shadow_sizes[SCENE_SHADOW_LIMIT];
    bool shadow_cubes[SCENE_SHADOW_LIMIT];
    uint64_t shadow_keys[SCENE_SHADOW_LIMIT];
    Scene3dShaderProgram shader_programs[SCENE_SHADER_PROGRAM_LIMIT];
    unsigned shader_program_count;
    Scene3dSimulation* simulations[SCENE_SIMULATION_LIMIT];
    unsigned simulation_count;
    NativeGlResource fullscreen_buffer, fullscreen_triangle; // one full-screen triangle for every pass
    Scene3dProjection projection;
    Scene3dRuntime* runtime;
    Scene3dAnimationState* animation;
    uint64_t animated_generation;
    Scene3dPhysics* physics;
    uint64_t physical_generation;
    ImageSurface* snapshot;
    uint64_t mutation_epoch, source_key, projection_generation, snapshot_generation;
    float width, height, scale;
    Scene3dStats stats;
    char diagnostic[2048];
};
struct Scene3dRegistry : DomDocumentResourceData {
    DomDocument* document;
    Scene3dEntry* entries;
    unsigned count;
};
// build-only references never survive projection; IDs and GPU payloads are copied.
struct Scene3dDefinition {
    DomElement* source;
    char id[128];
    Scene3dGeometrySource* geometry_source;
    Scene3dGeometry* geometries[SCENE_TOPOLOGY_COUNT];
    Scene3dMaterial* material;
    // a texture uploads once per color space it is read in: sRGB for colors, linear for data maps
    NativeGlResource texture, linear_texture;
    uint64_t texture_hash;
    bool texture_alpha;
};
struct Scene3dBuild {
    Scene3dEntry* entry;
    UiContext* ui;
    DomElement* root;
    Scene3dDefinition* definitions;
    unsigned definition_count, nodes;
    const char* camera;
    const char* environment; // the id of the environment that lights the scene
    unsigned char layer;
    DomElement* mesh_source;
    Scene3dLodBranch* lod;
};
static bool scene3d_world(Scene3dEntry* entry,DomElement* node,Scene3dMatrix* world,bool* visible,bool authored=false);
// simulation passes set uniforms ahead of the drawing code that defines this
static bool scene3d_uniform(Scene3dEntry* entry, NativeGlResource program, const char* name, const float* values,
    unsigned components, unsigned count);

static void scene3d_retire_snapshot(Scene3dEntry* entry) {
    image_surface_bump_generation(entry->snapshot);
    image_surface_snapshot_release(entry->snapshot);entry->snapshot=nullptr;
}

static bool scene3d_fail(Scene3dEntry* entry, const char* message) {
    str_copy(entry->diagnostic, sizeof(entry->diagnostic), message, strlen(message));
    log_error("scene3d projection: %s", message); return false;
}
static bool scene3d_tag(DomElement* node, const char* tag) { return strcmp(node->local_name(), tag) == 0; }
static ItemReader scene3d_value(DomElement* node, const char* key) {
    Element* backing = dom_element_backing(node);
    return backing ? ElementReader(backing).get_attr(key) : ItemReader();
}
static bool scene3d_present(DomElement* node,const char* key) {
    return node->has_attribute(key)||!scene3d_value(node,key).isNull();
}
// camera view attributes affect only the main pass. Everything else, including node identity,
// child order and typed resource values, participates so mixed edits cannot retain stale draws.
static bool scene3d_source_key(DomNode* node, uint64_t* hash, unsigned* count, unsigned depth=0) {
    if (depth>64 || ++*count>SCENE_NODE_LIMIT) return false;
    *hash=dom_node_ref_identity_hash(dom_node_ref(node),*hash,0);
    if (node->is_element()) {
        DomElement* element=node->as_element();
        const char* tag=element->local_name();
        *hash=hash_djb2_add_extend(*hash,tag,strlen(tag)+1);
        bool camera=scene3d_tag(element,"camera");
        int attributes=0;const char** names=element->attribute_names(&attributes);
        for (int i=0;i<attributes;i++) {
            const char* key=names[i];
            if (camera && (!strcmp(key,"position") || !strcmp(key,"rotation") || !strcmp(key,"quaternion") ||
                !strcmp(key,"scale") || !strcmp(key,"target") || !strcmp(key,"up") || !strcmp(key,"type") ||
                !strcmp(key,"fov") || !strcmp(key,"near") || !strcmp(key,"far") || !strcmp(key,"aspect") ||
                !strcmp(key,"view_height") || !strcmp(key,"visible"))) continue;
            *hash=hash_djb2_add_extend(*hash,key,strlen(key)+1);
            const char* text=element->get_attribute(key);
            if (text) *hash=hash_djb2_add_extend(*hash,text,strlen(text)+1);
            else {
                // compound attributes are immutable Lambda values; hash their identity without
                // retaining GC pointers or walking large vertex/index arrays on every camera move.
                Item value=scene3d_value(element,key).item();
                *hash=hash_djb2_add_extend(*hash,&value.item,sizeof(value.item));
            }
        }
    } else if (node->is_text() && node->as_text()->native_string) {
        String* text=node->as_text()->native_string;
        *hash=hash_djb2_add_extend(*hash,text->chars,text->len+1);
    }
    if (node->is_element())
        for (DomNode* child=node->as_element()->first_child;child;child=child->next_sibling)
            if (!scene3d_source_key(child,hash,count,depth+1)) return false;
    const uint8_t end=0xff;
    *hash=hash_djb2_add_extend(*hash,&end,sizeof(end));return true;
}
const char* scene3d_text(DomElement* node, const char* key) {
    ItemReader value=scene3d_value(node,key);
    // dom text conversion does not widen the scene schema's string/symbol lane.
    if (!value.isNull() && !value.isString() && !value.isSymbol()) return nullptr;
    const char* text=node->get_attribute(key);
    if (text) return text;
    // element attributes may be symbols as in type:'perspective'; DOM strings alone omit them.
    return value.isSymbol()?value.asSymbol()->chars:nullptr;
}
static bool scene3d_scalar(ItemReader value, float* result) {
    double number;
    // asFloat is a typed accessor: integers and decimals need the shared numeric promotion.
    if (!item_try_to_double(value.item(),&number)) return false;
    *result=(float)number;return isfinite(*result);
}
bool scene3d_number(DomElement* node, const char* key, float fallback, float* result) {
    const char* text = node->get_attribute(key);
    if (text) {
        char* end; float value = strtof(text, &end);
        if (end == text || *str_skip_ascii_space(end) || !isfinite(value)) return false;
        *result = value; return true;
    }
    ItemReader value = scene3d_value(node, key);
    if (value.isNull()) { *result = fallback; return true; }
    return scene3d_scalar(value,result);
}
bool scene3d_flag(DomElement* node, const char* key, bool fallback, bool* result) {
    const char* text = node->get_attribute(key);
    if (text) {
        if (!strcmp(text,"true") || !strcmp(text,"1")) { *result = true; return true; }
        if (!strcmp(text,"false") || !strcmp(text,"0")) { *result = false; return true; }
        return false;
    }
    ItemReader value = scene3d_value(node, key);
    if (value.isNull()) { *result = fallback; return true; }
    if (!value.isBool()) return false;
    *result = value.asBool(); return true;
}
bool scene3d_numbers(DomElement* node, const char* key, Pool* pool,
    float** values, unsigned* count, unsigned limit) {
    *values = nullptr; *count = 0;
    const char* text = node->get_attribute(key);
    ItemReader value = scene3d_value(node, key);
    if (!text && value.isNull()) return true;
    if (!text && !value.isArray()) return false;
    unsigned length = 0;
    if (text) {
        const char* cursor = text;
        double number;int status;
        while((status=str_next_double(&cursor,&number))==1)
            if(!isfinite((float)number)||++length>limit)return false;
        if(status<0)return false;
    } else {
        int64_t n = value.asArray().length(); if (n < 0 || (uint64_t)n > limit) return false;
        length = (unsigned)n;
    }
    if (!length) return true;
    float* data = (float*)pool_alloc(pool, length * sizeof(float)); if (!data) return false;
    bool present;
    if(!scene3d_components(node,key,data,length,&present)) {pool_free(pool,data);return false;}
    *values = data; *count = length; return true;
}
bool scene3d_components(DomElement* node,const char* key,float* data,unsigned count,bool* present) {
    const char* text=node->get_attribute(key);ItemReader value=scene3d_value(node,key);
    *present=text||!value.isNull();if(!*present) return true;
    if(text) {
        const char* cursor=text;
        for(unsigned i=0;i<count;i++) {
            double number;if(str_next_double(&cursor,&number)!=1)return false;
            data[i]=(float)number;if(!isfinite(data[i]))return false;
        }
        double extra;return str_next_double(&cursor,&extra)==0;
    }
    if(!value.isArray()||value.asArray().length()!=count) return false;
    for(unsigned i=0;i<count;i++) if(!scene3d_scalar(value.asArray().get(i),&data[i])) return false;
    return true;
}
bool scene3d_vector(DomElement* node,const char* key,Scene3dVec fallback,Scene3dVec* result) {
    float data[3];bool present;if(!scene3d_components(node,key,data,3,&present)) return false;
    *result=present?Scene3dVec{data[0],data[1],data[2]}:fallback;return true;
}
static bool scene3d_local(DomElement* node, Scene3dMatrix* matrix, Scene3dAnimationState* animation=nullptr) {
    bool authored_matrix;
    if(!scene3d_components(node,"matrix",matrix->v,16,&authored_matrix)) return false;
    if(authored_matrix) {
        if(node->has_attribute("position")||node->has_attribute("rotation")||node->has_attribute("quaternion")||node->has_attribute("scale")) return false;
        Scene3dMatrix inverse;return scene3d_inverse_affine(*matrix,&inverse);
    }
    Scene3dVec position, rotation, scale;
    if (!scene3d_vector(node,"position",{},&position) || !scene3d_vector(node,"rotation",{},&rotation) ||
        !scene3d_vector(node,"scale",{1,1,1},&scale) || scale.x == 0 || scale.y == 0 || scale.z == 0) return false;
    AnimationValue value={};
    if(scene3d_animation_value(animation,node,"position",&value)) position={(float)value.numbers[0],(float)value.numbers[1],(float)value.numbers[2]};
    if(scene3d_animation_value(animation,node,"scale",&value)) scale={(float)value.numbers[0],(float)value.numbers[1],(float)value.numbers[2]};
    if(scale.x==0||scale.y==0||scale.z==0) return false;
    bool quaternion;float q[4];if(!scene3d_components(node,"quaternion",q,4,&quaternion)) return false;
    bool animated_quaternion=scene3d_animation_value(animation,node,"quaternion",&value);
    if(quaternion||animated_quaternion) {
        if(!animated_quaternion) for(unsigned i=0;i<4;i++) value.numbers[i]=q[i];
        double rotation_matrix[16];if(!animation_quaternion_matrix(value.numbers,rotation_matrix)) return false;
        *matrix=scene3d_rotation_matrix(rotation_matrix,position,scale);
    } else *matrix = scene3d_transform(position, rotation, scale);
    return true;
}
static bool scene3d_animated_flag(Scene3dAnimationState* animation,DomElement* node,bool* visible) {
    if(!scene3d_flag(node,"visible",true,visible)) return false;
    AnimationValue value={};if(scene3d_animation_value(animation,node,"visible",&value)) *visible=value.numbers[0]!=0;
    return true;
}
static bool scene3d_animated_number(Scene3dEntry* entry,DomElement* node,const char* key,float fallback,float* number) {
    if(!scene3d_number(node,key,fallback,number)) return false;
    AnimationValue value={};if(scene3d_animation_value(entry->animation,node,key,&value)) *number=(float)value.numbers[0];
    return isfinite(*number);
}
static bool scene3d_animated_color(Scene3dEntry* entry,DomElement* node,float color[4],
    const char* key="color",const char* fallback="#ffffff") {
    if(!scene3d_color(node,key,fallback,color)) return false;
    AnimationValue value={};if(scene3d_animation_value(entry->animation,node,key,&value))
        for(unsigned c=0;c<3;c++) color[c]=(float)value.numbers[c];
    return isfinite(color[0])&&isfinite(color[1])&&isfinite(color[2]);
}
static float scene3d_linear(float value) {
    return value <= .04045f ? value / 12.92f : powf((value + .055f) / 1.055f, 2.4f);
}
bool scene3d_color(DomElement* node, const char* key, const char* fallback, float color[4]) {
    const char* text = scene3d_text(node,key); if (!text) text = fallback;
    if (!strcmp(text,"transparent")) { memset(color, 0, sizeof(float)*4); return true; }
    uint8_t r,g,b,a;
    if (!color_parse_hex(text,&r,&g,&b,&a)) return false;
    color[0]=scene3d_linear(r/255.0f); color[1]=scene3d_linear(g/255.0f);
    color[2]=scene3d_linear(b/255.0f); color[3]=a/255.0f; return true;
}
// 1 for numbers (or a color) written to `values`, 0 for any other text (a texture reference), -1 when malformed
static int scene3d_uniform_value(ItemReader value, float* values, unsigned capacity, unsigned* count, bool* color) {
    *count=0;*color=false;
    if (value.isString() || value.isSymbol()) {
        const char* text=value.isString()?value.cstring():value.asSymbol()->chars;
        if (!text || text[0]!='#') return 0;
        uint8_t r,g,b,a;
        if (capacity<3 || !color_parse_hex(text,&r,&g,&b,&a)) return -1;
        values[0]=scene3d_linear(r/255.0f);values[1]=scene3d_linear(g/255.0f);values[2]=scene3d_linear(b/255.0f);
        *count=3;*color=true;return 1;
    }
    if (value.isArray()) {
        ArrayReader array=value.asArray();int64_t length=array.length();
        if (length<1 || (uint64_t)length>capacity) return -1;
        for (int64_t i=0;i<length;i++) if (!scene3d_scalar(array.get(i),&values[i])) return -1;
        *count=(unsigned)length;return 1;
    }
    if (capacity<1 || !scene3d_scalar(value,&values[0])) return -1;
    *count=1;return 1;
}
bool scene3d_uniform_numbers(DomElement* material, const char* name, float* values, unsigned capacity,
        unsigned* count, bool* color) {
    ItemReader uniforms=scene3d_value(material,"uniforms");
    if (!uniforms.isMap() || !uniforms.asMap().has(name)) return false;
    return scene3d_uniform_value(uniforms.asMap().get(name),values,capacity,count,color)==1;
}
// a GLSL identifier the host prefix does not already declare: a shader material's vertex layout and
// camera, or a simulation pass's grid size and step
static bool scene3d_shader_name(const char* name, unsigned space=0) {
    static const char* material_names[]={"position","normal","uv","uv1","color","tangent","instance_matrix",
        "model_matrix","view_matrix","projection_matrix","camera_position","fragment_color"};
    static const char* simulation_names[]={"resolution","delta","fragment_color"};
    static const char* post_names[]={"color_input","resolution","fragment_color","pass_color","effect_main"};
    size_t length=name?strlen(name):0;
    if (!length || length>=SCENE_SHADER_NAME_LIMIT || !strncmp(name,"gl_",3) || isdigit((unsigned char)name[0])) return false;
    for (size_t i=0;i<length;i++) if (!isalnum((unsigned char)name[i]) && name[i]!='_') return false;
    const char* const* reserved=space==2?post_names:space==1?simulation_names:material_names;
    unsigned reserved_count=space==2?sizeof(post_names)/sizeof(*post_names):space==1?
        sizeof(simulation_names)/sizeof(*simulation_names):sizeof(material_names)/sizeof(*material_names);
    for (unsigned i=0;i<reserved_count;i++) if (!strcmp(name,reserved[i])) return false;
    return true;
}
static bool scene3d_resource(Scene3dBuild* build, NativeGlResource resource) {
    if (!resource.id) return scene3d_fail(build->entry, native_gl_diagnostic(build->entry->graphics));
    Scene3dProjection* p = &build->entry->projection;
    if (!lam::pool_grow_array(p->pool,&p->resources,&p->resource_capacity,p->resource_count+1,32)) {
        // failed adoption must release the just-created resource before discarding the projection.
        native_gl_release(build->entry->graphics,resource);
        return scene3d_fail(build->entry,"resource registry allocation failed");
    }
    p->resources[p->resource_count++] = resource; return true;
}
static Scene3dDefinition* scene3d_definition(Scene3dBuild* build, DomElement* source, const char* reference, const char* tag) {
    for (unsigned i=0;i<build->definition_count;i++) {
        Scene3dDefinition* d = &build->definitions[i];
        if ((reference && !strcmp(d->id,reference)) || (!reference && d->source == source))
            return scene3d_tag(d->source,tag) ? d : nullptr;
    }
    return nullptr;
}
static bool scene3d_collect_definitions(Scene3dBuild* build, DomElement* node, unsigned depth) {
    if (depth > 64 || ++build->nodes > SCENE_NODE_LIMIT) return scene3d_fail(build->entry,"scene hierarchy quota exceeded");
    Scene3dDefinition* d = &build->definitions[build->definition_count++]; d->source = node;
    // malformed typed attributes must not fall through as absent defaults or inline resources.
    const char* text_keys[]={"id","type","color","background","side","geometry","material","src","camera","skeleton",
        "color-space","alpha-mode","tone-mapping",
        "environment","ground-color","sheen-color","physics-body"};
    for (const char* key:text_keys)
        if (!scene3d_value(node,key).isNull() && !scene3d_text(node,key))
            return scene3d_fail(build->entry,"scene text attribute requires a string or symbol");
    for(const auto& spec:scene3d_map_specs) if(!scene3d_value(node,spec.attribute).isNull()&&!scene3d_text(node,spec.attribute))
        return scene3d_fail(build->entry,"material texture reference requires a string or symbol");
    const char* id = scene3d_text(node,"id");
    if (id) {
        if (!*id || strlen(id) >= sizeof(d->id)) return scene3d_fail(build->entry,"invalid scene ID");
        for (unsigned i=0;i+1<build->definition_count;i++) if (!strcmp(build->definitions[i].id,id))
            return scene3d_fail(build->entry,"duplicate scene ID");
        str_copy(d->id,sizeof(d->id),id,strlen(id));
    }
    for (DomNode* child=node->first_child; child; child=child->next_sibling) {
        if (child->is_element()) { if (!scene3d_collect_definitions(build,child->as_element(),depth+1)) return false; }
        else if (child->is_text() && child->as_text()->native_string &&
            strspn(child->as_text()->native_string->chars," \t\r\n") != child->as_text()->native_string->len)
            return scene3d_fail(build->entry,"scene children must be scene elements");
    }
    return true;
}

static Scene3dGeometrySource* scene3d_geometry_source(Scene3dBuild* build,Scene3dDefinition* definition) {
    if(!definition) {scene3d_fail(build->entry,"missing geometry reference");return nullptr;}
    if(definition->geometry_source) return definition->geometry_source;
    DomElement* node=definition->source;Pool* pool=build->entry->projection.pool;
    if(node->first_child) {scene3d_fail(build->entry,"geometry cannot have children");return nullptr;}
    auto* source=(Scene3dGeometrySource*)pool_calloc(pool,sizeof(Scene3dGeometrySource));if(!source) return nullptr;
    const char* type=scene3d_text(node,"type");float* raw_indices=nullptr;
    const char* uv_mode=scene3d_text(node,"drawable-uvs");
    if(uv_mode&&strcmp(uv_mode,"source")&&strcmp(uv_mode,"surface")) {scene3d_fail(build->entry,"invalid drawable-uvs mode");return nullptr;}
    source->source_uvs=uv_mode&&!strcmp(uv_mode,"source");
    unsigned pc=0,nc=0,uc=0,uc1=0,cc=0,tc=0,sc=0,ic=0,dc[4]={};
    if(type&&(!strcmp(type,"box")||!strcmp(type,"plane"))) {
        bool box=!strcmp(type,"box");Scene3dVec size;
        if(!scene3d_vector(node,"size",{1,1,1},&size)||size.x<=0||size.y<=0||size.z<=0) {scene3d_fail(build->entry,"invalid primitive size");return nullptr;}
        source->vertex_count=box?MESH_BOX_VERTICES:MESH_PLANE_VERTICES;source->index_count=box?MESH_BOX_INDICES:MESH_PLANE_INDICES;
        source->positions=(float*)pool_alloc(pool,source->vertex_count*3*sizeof(float));source->normals=(float*)pool_alloc(pool,source->vertex_count*3*sizeof(float));
        source->uvs=(float*)pool_alloc(pool,source->vertex_count*2*sizeof(float));source->indices=(uint32_t*)pool_alloc(pool,source->index_count*sizeof(uint32_t));
        if(!source->positions||!source->normals||!source->uvs||!source->indices) return nullptr;
        float sizes[]={size.x,size.y,size.z};mesh_primitive(box,sizes,source->positions,source->normals,source->uvs,source->indices);
        pc=source->vertex_count*3;nc=pc;uc=source->vertex_count*2;
    } else if(type&&!strcmp(type,"buffer")) {
        struct Stream {const char* key;float** data;unsigned* count;unsigned width;};
        Stream streams[]={{"positions",&source->positions,&pc,3},{"normals",&source->normals,&nc,3},
            {"uvs",&source->uvs,&uc,2},{"uvs1",&source->uvs1,&uc1,2},{"colors",&source->colors,&cc,4},
            {"tangents",&source->tangents,&tc,4},{"indices",&raw_indices,&ic,6}};
        for(const auto& stream:streams) if(!scene3d_numbers(node,stream.key,pool,stream.data,stream.count,SCENE_VERTEX_LIMIT*stream.width)) {
            scene3d_fail(build->entry,"invalid geometry component counts");return nullptr;
        }
        if(!pc||pc%3||(nc&&nc!=pc)||(uc&&uc!=pc/3*2)||(uc1&&uc1!=pc/3*2)||(cc&&cc!=pc&&cc!=pc/3*4)||(tc&&tc!=pc/3*4)) {
            scene3d_fail(build->entry,"invalid geometry component counts");return nullptr;
        }
        source->vertex_count=pc/3;source->index_count=ic;source->color_components=cc?cc/source->vertex_count:0;
        if(ic) {source->indices=(uint32_t*)pool_alloc(pool,ic*sizeof(uint32_t));if(!source->indices) return nullptr;}
        for(unsigned i=0;i<ic;i++) {
            if(raw_indices[i]<0||raw_indices[i]>=source->vertex_count||floorf(raw_indices[i])!=raw_indices[i]) {scene3d_fail(build->entry,"geometry index out of bounds");return nullptr;}
            source->indices[i]=(uint32_t)raw_indices[i];
        }
    } else {scene3d_fail(build->entry,"unknown geometry type");return nullptr;}
    if(!scene3d_numbers(node,"point-sizes",pool,&source->sizes,&sc,SCENE_VERTEX_LIMIT)||(sc&&sc!=source->vertex_count)) {
        scene3d_fail(build->entry,"point-sizes needs one positive number per vertex");return nullptr;
    }
    for(unsigned i=0;i<sc;i++) if(source->sizes[i]<=0) {scene3d_fail(build->entry,"point-sizes needs one positive number per vertex");return nullptr;}
    const char* deformation_keys[]={"skin-indices","skin-weights","morph-positions","morph-normals"};
    for(unsigned i=0;i<4;i++) if(!scene3d_numbers(node,deformation_keys[i],pool,&source->deformation[i],&dc[i],SCENE_VERTEX_LIMIT*6)) {
        scene3d_fail(build->entry,"invalid geometry deformation data");return nullptr;
    }
    unsigned nv=source->vertex_count,morph_size=nv*3;
    if((dc[0]&&dc[0]!=nv*4)||dc[0]!=dc[1]||dc[2]%morph_size||dc[2]>morph_size*2||(dc[3]&&dc[3]!=dc[2])) {
        scene3d_fail(build->entry,"invalid skin or morph component counts");return nullptr;
    }
    source->morph_count=dc[2]/morph_size;source->lo={FLT_MAX,FLT_MAX,FLT_MAX};source->hi={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for(unsigned i=0;i<nv;i++) {
        const float* v=source->positions+i*3;
        source->lo={fminf(source->lo.x,v[0]),fminf(source->lo.y,v[1]),fminf(source->lo.z,v[2])};
        source->hi={fmaxf(source->hi.x,v[0]),fmaxf(source->hi.y,v[1]),fmaxf(source->hi.z,v[2])};
        if(nc) {Scene3dVec n={source->normals[i*3],source->normals[i*3+1],source->normals[i*3+2]};if(!scene3d_normalize(&n)) {scene3d_fail(build->entry,"degenerate geometry normals");return nullptr;}}
        if(cc) for(unsigned c=0;c<source->color_components;c++) {
            float* color=&source->colors[i*source->color_components+c];
            if(*color<0||*color>1) {scene3d_fail(build->entry,"vertex color out of range");return nullptr;}
            if(c<3) *color=scene3d_linear(*color);else source->vertex_alpha|=*color<1;
        }
        if(tc) {
            const float* t=source->tangents+i*4;
            if(fabsf(t[3])!=1||fabsf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]-1)>1e-2f) {scene3d_fail(build->entry,"invalid geometry tangent");return nullptr;}
        }
        if(dc[0]) {
            float sum=0;
            for(unsigned c=0;c<4;c++) {
                float index=source->deformation[0][i*4+c],weight=source->deformation[1][i*4+c];
                if(index<0||index>=SCENE_BONE_LIMIT||floorf(index)!=index||weight<0||weight>1) {scene3d_fail(build->entry,"invalid skin index or weight");return nullptr;}
                sum+=weight;if(weight>0&&source->bone_index_count<(unsigned)index+1) source->bone_index_count=(unsigned)index+1;
            }
            if(fabsf(sum-1)>1e-5f) {scene3d_fail(build->entry,"skin weights must sum to one");return nullptr;}
        }
    }
    definition->geometry_source=source;return source;
}
static Scene3dVertex scene3d_source_vertex(const Scene3dGeometrySource* source,unsigned index) {
    Scene3dVertex vertex={};memcpy(vertex.position,source->positions+index*3,sizeof(vertex.position));
    if(source->normals) memcpy(vertex.normal,source->normals+index*3,sizeof(vertex.normal));
    for(unsigned c=0;c<4;c++) vertex.color[c]=source->colors&&c<source->color_components?source->colors[index*source->color_components+c]:1;
    if(source->uvs) memcpy(vertex.uv,source->uvs+index*2,sizeof(vertex.uv));
    if(source->uvs1) memcpy(vertex.uv1,source->uvs1+index*2,sizeof(vertex.uv1));
    if(source->tangents) memcpy(vertex.tangent,source->tangents+index*4,sizeof(vertex.tangent));
    if(source->bone_index_count) {
        memcpy(vertex.skin_indices,source->deformation[0]+index*4,sizeof(vertex.skin_indices));
        memcpy(vertex.skin_weights,source->deformation[1]+index*4,sizeof(vertex.skin_weights));
    }
    for(unsigned t=0;t<source->morph_count;t++) for(unsigned c=0;c<3;c++) {
        unsigned offset=t*source->vertex_count*3+index*3+c;
        vertex.morph_positions[t][c]=source->deformation[2][offset];
        vertex.morph_normals[t][c]=source->deformation[3]?source->deformation[3][offset]:0;
    }
    return vertex;
}
static unsigned scene3d_source_index(const Scene3dGeometrySource* source,unsigned at) {
    return source->index_count?source->indices[at]:at;
}
static void scene3d_append_quad(Scene3dVertex* vertices,uint32_t* indices,unsigned* vertex,unsigned* index,
        const Scene3dVertex quad[4],bool ribbon) {
    static const unsigned orders[2][6]={{0,1,2,0,2,3},{0,1,2,2,1,3}};
    memcpy(vertices+*vertex,quad,sizeof(Scene3dVertex)*4);
    for(unsigned i=0;i<6;i++) indices[(*index)++]=*vertex+orders[ribbon?1:0][i];
    *vertex+=4;
}
static bool scene3d_expand_drawable(Scene3dBuild* build,const Scene3dGeometrySource* source,Scene3dGeometry* geometry,
        Scene3dVertex** vertices,uint32_t** indices) {
    Pool* pool=build->entry->projection.pool;Scene3dTopology kind=geometry->topology;
    unsigned count=source->index_count?source->index_count:source->vertex_count;
    if(source->bone_index_count||source->morph_count) return scene3d_fail(build->entry,"drawables do not support geometry deformation");
    bool ribbon=kind==SCENE_LINE||kind==SCENE_SEGMENTS;
    if((kind==SCENE_SPRITE&&count!=1)||(ribbon&&(count<2||(kind==SCENE_SEGMENTS&&count%2))))
        return scene3d_fail(build->entry,"invalid drawable topology component counts");
    uint64_t segments=kind==SCENE_LINE?count-1:kind==SCENE_SEGMENTS?count/2:0,joins=kind==SCENE_LINE&&count>2?count-2:0;
    uint64_t vc=ribbon?segments*4+joins*3:(uint64_t)count*4,ic=ribbon?segments*6+joins*3:(uint64_t)count*6;
    if(vc>SCENE_VERTEX_LIMIT||ic>SCENE_VERTEX_LIMIT*6) return scene3d_fail(build->entry,"expanded drawable vertex quota exceeded");
    geometry->vertex_count=(unsigned)vc;geometry->index_count=(unsigned)ic;geometry->has_uv0=!source->source_uvs||source->uvs;geometry->has_tangents=ribbon;
    *vertices=(Scene3dVertex*)pool_calloc(pool,vc*sizeof(Scene3dVertex));*indices=(uint32_t*)pool_alloc(pool,ic*sizeof(uint32_t));
    if(!*vertices||!*indices) return false;
    unsigned vertex=0,index=0;
    if(!ribbon) {
        static const float corners[4][2]={{0,0},{1,0},{1,1},{0,1}};
        for(unsigned i=0;i<count;i++) {
            unsigned at=scene3d_source_index(source,i);Scene3dVertex point=scene3d_source_vertex(source,at),quad[4];
            float size=source->sizes?source->sizes[at]:1;
            for(unsigned c=0;c<4;c++) {
                quad[c]=point;quad[c].normal[0]=quad[c].normal[1]=size;quad[c].normal[2]=(float)c;
                if(!source->source_uvs) memcpy(quad[c].uv,corners[c],sizeof(quad[c].uv));
            }
            scene3d_append_quad(*vertices,*indices,&vertex,&index,quad,false);
        }
        return true;
    }
    // double arc lengths avoid overflowing float distances before their normalized U is computed.
    double* arc=nullptr;
    if(kind==SCENE_LINE&&!source->uvs) {
        arc=(double*)pool_calloc(pool,count*sizeof(double));if(!arc) return false;
        for(unsigned i=1;i<count;i++) {
            const float *a=source->positions+scene3d_source_index(source,i-1)*3,*b=source->positions+scene3d_source_index(source,i)*3;
            double x=(double)b[0]-a[0],y=(double)b[1]-a[1],z=(double)b[2]-a[2];arc[i]=arc[i-1]+sqrt(x*x+y*y+z*z);
        }
    }
    auto u=[&](unsigned i,bool end)->float {
        if(source->uvs) return source->uvs[scene3d_source_index(source,i)*2];
        return arc?(arc[count-1]>0?(float)(arc[i]/arc[count-1]):0):end?1.f:0.f;
    };
    for(unsigned i=0;i<segments;i++) {
        unsigned a=kind==SCENE_LINE?i:i*2,b=a+1;
        Scene3dVertex from=scene3d_source_vertex(source,scene3d_source_index(source,a)),to=scene3d_source_vertex(source,scene3d_source_index(source,b)),quad[4];
        for(unsigned c=0;c<4;c++) {
            quad[c]=from;quad[c].normal[0]=u(a,false);quad[c].normal[1]=u(b,true);quad[c].normal[2]=from.uv[1];
            memcpy(quad[c].tangent,to.position,sizeof(to.position));quad[c].tangent[3]=to.uv[1];
            quad[c].skin_indices[0]=(float)c;quad[c].skin_indices[1]=to.uv1[0];quad[c].skin_indices[2]=to.uv1[1];quad[c].skin_indices[3]=0;
            memcpy(quad[c].skin_weights,to.color,sizeof(to.color));
        }
        scene3d_append_quad(*vertices,*indices,&vertex,&index,quad,true);
    }
    for(unsigned i=1;i+1<count&&kind==SCENE_LINE;i++) {
        Scene3dVertex before=scene3d_source_vertex(source,scene3d_source_index(source,i-1)),center=scene3d_source_vertex(source,scene3d_source_index(source,i)),after=scene3d_source_vertex(source,scene3d_source_index(source,i+1));
        for(unsigned c=0;c<3;c++) {
            Scene3dVertex join=center;memcpy(join.normal,before.position,sizeof(before.position));memcpy(join.tangent,after.position,sizeof(after.position));
            join.skin_indices[0]=(float)(4+c);join.uv[0]=u(i,false);if(!source->source_uvs) join.uv[1]=.5f;
            (*vertices)[vertex]=join;(*indices)[index++]=vertex++;
        }
    }
    return true;
}
static Scene3dGeometry* scene3d_geometry(Scene3dBuild* build,Scene3dDefinition* definition,Scene3dTopology topology=SCENE_TRIANGLES) {
    Scene3dGeometrySource* source=scene3d_geometry_source(build,definition);if(!source) return nullptr;
    if(definition->geometries[topology]) return definition->geometries[topology];
    Scene3dProjection* p=&build->entry->projection;
    auto* geometry=(Scene3dGeometry*)pool_calloc(p->pool,sizeof(Scene3dGeometry));if(!geometry) return nullptr;
    geometry->topology=topology;geometry->source_uvs=source->source_uvs;geometry->vertex_count=source->vertex_count;geometry->index_count=source->index_count;
    geometry->has_colors=source->color_components!=0;geometry->rgba=source->color_components==4;geometry->vertex_alpha=source->vertex_alpha;
    geometry->has_uv0=source->uvs!=nullptr;geometry->has_uv1=source->uvs1!=nullptr;geometry->has_tangents=source->tangents!=nullptr;
    geometry->bone_index_count=source->bone_index_count;geometry->morph_count=source->morph_count;
    geometry->maximum_point_size=source->sizes?0:1;
    if(source->sizes) for(unsigned i=0;i<source->vertex_count;i++) geometry->maximum_point_size=fmaxf(geometry->maximum_point_size,source->sizes[i]);
    Scene3dVertex* vertices=nullptr;uint32_t* indices=source->indices;
    if(topology==SCENE_TRIANGLES) {
        if((geometry->index_count?geometry->index_count:geometry->vertex_count)%3) {scene3d_fail(build->entry,"invalid geometry component counts");return nullptr;}
        vertices=(Scene3dVertex*)pool_calloc(p->pool,sizeof(Scene3dVertex)*geometry->vertex_count);if(!vertices) return nullptr;
        for(unsigned i=0;i<geometry->vertex_count;i++) vertices[i]=scene3d_source_vertex(source,i);
        if(!source->normals) {
            float* sums=(float*)pool_calloc(p->pool,geometry->vertex_count*3*sizeof(float));if(!sums) return nullptr;
            mesh_accumulate_normals(source->positions,indices,geometry->index_count?geometry->index_count:geometry->vertex_count,sums);
            for(unsigned i=0;i<geometry->vertex_count;i++) memcpy(vertices[i].normal,sums+i*3,sizeof(vertices[i].normal));
        }
        for(unsigned i=0;i<geometry->vertex_count;i++) {
            Scene3dVec n={vertices[i].normal[0],vertices[i].normal[1],vertices[i].normal[2]};
            if(!scene3d_normalize(&n)) {scene3d_fail(build->entry,"degenerate geometry normals");return nullptr;}
            vertices[i].normal[0]=n.x;vertices[i].normal[1]=n.y;vertices[i].normal[2]=n.z;
        }
    } else if(!scene3d_expand_drawable(build,source,geometry,&vertices,&indices)) return nullptr;
    geometry->lo=source->lo;geometry->hi=source->hi;geometry->center=Scene3dVec{source->lo.x*.5f+source->hi.x*.5f,source->lo.y*.5f+source->hi.y*.5f,source->lo.z*.5f+source->hi.z*.5f};
    // optional streams cost no vertex bytes when absent; the same descriptors bind every VAO.
    geometry->lanes=7u|(geometry->has_colors?1u<<3:0)|(geometry->has_tangents?1u<<4:0)|(geometry->has_uv1?1u<<5:0);
    if(geometry->bone_index_count||topology==SCENE_LINE||topology==SCENE_SEGMENTS) geometry->lanes|=(1u<<6)|(1u<<7);
    for(unsigned t=0;t<geometry->morph_count;t++) geometry->lanes|=(1u<<(8+t))|(1u<<(10+t));
    for(unsigned lane=0;lane<SCENE_LANE_COUNT;lane++) if(geometry->lanes&(1u<<lane)) {
        geometry->offsets[lane]=geometry->vertex_stride;
        geometry->vertex_stride+=(lane==3&&!geometry->rgba?3:scene3d_lanes[lane].components)*sizeof(float);
    }
    for(unsigned i=0;i<geometry->vertex_count;i++) for(unsigned lane=0;lane<SCENE_LANE_COUNT;lane++) if(geometry->lanes&(1u<<lane)) {
        unsigned bytes=(lane==3&&!geometry->rgba?3:scene3d_lanes[lane].components)*sizeof(float);
        memmove((char*)vertices+i*geometry->vertex_stride+geometry->offsets[lane],(char*)(vertices+i)+scene3d_lanes[lane].offset,bytes);
    }
    geometry->hash=hashmap_xxhash3(vertices,geometry->vertex_stride*geometry->vertex_count,geometry->vertex_stride,geometry->index_count);
    geometry->hash=hash_fnv1a_64_extend(geometry->hash,&geometry->topology,sizeof(geometry->topology));
    geometry->hash=hash_fnv1a_64_extend(geometry->hash,&geometry->source_uvs,sizeof(geometry->source_uvs));
    if (geometry->index_count) geometry->hash=hashmap_xxhash3(indices,sizeof(uint32_t)*geometry->index_count,geometry->hash,0);
    geometry->vertices=native_gl_buffer(build->entry->graphics,vertices,geometry->vertex_stride*geometry->vertex_count,false);
    if (!scene3d_resource(build,geometry->vertices)) return nullptr;
    if (geometry->index_count) {
        geometry->indices=native_gl_buffer(build->entry->graphics,indices,sizeof(uint32_t)*geometry->index_count);
        if (!scene3d_resource(build,geometry->indices)) return nullptr;
    }
    p->geometry_count++; definition->geometries[topology]=geometry; return geometry;
}

// `srgb` is the slot's default: base color and emission are sRGB, data maps linear;
// a texture's own `color-space` overrides it
static NativeGlResource scene3d_texture(Scene3dBuild* build, Scene3dDefinition* definition, bool srgb = true) {
    if (!definition) { scene3d_fail(build->entry,"missing texture reference"); return {}; }
    const char* space=scene3d_text(definition->source,"color-space");
    if (space && strcmp(space,"srgb") && strcmp(space,"linear")) { scene3d_fail(build->entry,"invalid texture color space"); return {}; }
    if (space) srgb=!strcmp(space,"srgb");
    NativeGlResource* cached=srgb?&definition->texture:&definition->linear_texture;
    if (cached->id) return *cached;
    if (definition->source->first_child) { scene3d_fail(build->entry,"texture cannot have children"); return {}; }
    const char* source=scene3d_text(definition->source,"src");
    if (!source || !*source) { scene3d_fail(build->entry,"texture requires a local image source"); return {}; }
    NativeGlSampler sampler;unsigned* fields[]={&sampler.wrap_s,&sampler.wrap_t,&sampler.min_filter,&sampler.mag_filter};
    const char* keys[]={"wrap-s","wrap-t","min-filter","mag-filter"};
    for(unsigned i=0;i<4;i++) {
        float value;if(!scene3d_number(definition->source,keys[i],*fields[i],&value)||value<0||value>65535||floorf(value)!=value) {
            scene3d_fail(build->entry,"invalid texture sampler");return {};
        }*fields[i]=(unsigned)value;
    }
    float max_size;
    if(!scene3d_number(definition->source,"max-size",4096,&max_size)||max_size<1||max_size>4096||floorf(max_size)!=max_size) {
        scene3d_fail(build->entry,"texture max-size must be an integer in 1..4096");return {};
    }
    sampler.max_size=(unsigned)max_size;sampler.srgb=srgb;
    ImageSurface* image=load_document_image(build->root->doc,build->ui,source);
    if (!image || image->width<=0 || image->height<=0) {
        scene3d_fail(build->entry,"texture acquisition or format failed");return {};
    }
    ImageSurface raster={};lam::Temp<uint32_t> straight;
    if (image->format==IMAGE_FORMAT_SVG) {
        float width,height;
        if (!scene3d_number(definition->source,"width",image->width,&width) ||
            !scene3d_number(definition->source,"height",image->height,&height) ||
            !isfinite(width) || !isfinite(height) || width<1 || height<1 ||
            floorf(width)!=width || floorf(height)!=height) {
            scene3d_fail(build->entry,"invalid SVG texture dimensions");return {};
        }
        float scale=fminf(1.0f,max_size/fmaxf(width,height));
        raster.width=(int)fmaxf(1,floorf(width*scale)); // INT_CAST_OK: bounded SVG raster pixel extent.
        raster.height=(int)fmaxf(1,floorf(height*scale)); // INT_CAST_OK: bounded SVG raster pixel extent.
        if (!render_media_rasterize_svg_picture(image,raster.width,raster.height)) {
            scene3d_fail(build->entry,"SVG texture rasterization failed");return {};
        }
        // keep the document's premultiplied SVG cache intact; GPU sampling needs straight sRGB.
        straight.reset((uint32_t*)mem_alloc((size_t)raster.width*raster.height*4,MEM_CAT_RENDER));
        if (!straight) return {};
        for (unsigned y=0;y<(unsigned)raster.height;y++) {
            const uint32_t* row=(const uint32_t*)((const uint8_t*)image->pixels+(size_t)y*image->pitch);
            for (unsigned x=0;x<(unsigned)raster.width;x++)
                straight.get()[(size_t)y*raster.width+x]=render_pixel_unpremultiply_abgr(row[x]);
        }
        raster.pixels=straight.get();raster.pitch=raster.width*4;raster.alpha_mode=IMAGE_ALPHA_STRAIGHT;
        image=&raster;
    } else {
        if (image->width>4096 || image->height>4096) {
            scene3d_fail(build->entry,"texture dimensions exceed quota");return {};
        }
        image_surface_ensure_decoded(image,image->width,image->height);
    }
    if (!image->pixels || image->alpha_mode!=IMAGE_ALPHA_STRAIGHT) {
        scene3d_fail(build->entry,"texture acquisition or format failed");return {};
    }
    // what an environment capture's key records of the texture: its source and sampling, in either color space
    NativeGlSampler key=sampler;key.srgb=false;
    definition->texture_hash=hash_fnv1a_64_extend(hash_fnv1a_64_extend_cstr(HASH_FNV1A_64_OFFSET_BASIS,source),&key,sizeof(key));
    // SVG raster dimensions change its sampled content even when the source URL is unchanged.
    unsigned extent[]={(unsigned)image->width,(unsigned)image->height};
    definition->texture_hash=hash_fnv1a_64_extend(definition->texture_hash,extent,sizeof(extent));
    NativeGlResource texture=native_gl_texture(build->entry->graphics,image,sampler);
    if (!scene3d_resource(build,texture)) return {};
    for (unsigned y=0;y<(unsigned)image->height && !definition->texture_alpha;y++)
        for (unsigned x=0;x<(unsigned)image->width;x++)
            if (((const uint8_t*)image->pixels)[y*image->pitch+x*4+3]!=255) { definition->texture_alpha=true;break; }
    *cached=texture; build->entry->projection.texture_count++; return texture;
}
// SH1: GLSL bodies, the drawing state they need, and the values of the uniforms they declare
// a simulation the projection under construction declares
static Scene3dSimulation* scene3d_simulation_find(Scene3dEntry* entry, const char* id, size_t length) {
    for (unsigned i=0;i<entry->simulation_count;i++) {
        Scene3dSimulation* simulation=entry->simulations[i];
        if (simulation->generation==entry->projection_generation+1 && strlen(simulation->id)==length &&
            !strncmp(simulation->id,id,length)) return simulation;
    }
    return nullptr;
}
// SH1/SH5: a `uniforms` map of numbers, number arrays and "#rrggbb" colors. With `samplers`, any other
// text names what a sampler reads: a simulation variable as "<simulation>.<variable>", or a texture ID.
static bool scene3d_shader_uniforms(Scene3dBuild* build, DomElement* node, bool samplers,
        Scene3dShaderUniform** result, unsigned* count, uint64_t* texture_hash, unsigned names=0) {
    Pool* pool=build->entry->projection.pool;*result=nullptr;*count=0;
    ItemReader uniforms=scene3d_value(node,"uniforms");
    if (!uniforms.isNull() && (!uniforms.isMap() || uniforms.asMap().size()>SCENE_SHADER_UNIFORM_LIMIT))
        return scene3d_fail(build->entry,"shader uniforms must be a map of at most 32 values");
    // an empty map allocates nothing, which must not read as an allocation failure
    if (uniforms.isNull() || !uniforms.asMap().size()) return true;
    Scene3dShaderUniform* list=(Scene3dShaderUniform*)pool_calloc(pool,uniforms.asMap().size()*sizeof(Scene3dShaderUniform));
    if (!list) return false;
    unsigned used=0,bound=0;float values[SCENE_SHADER_VALUE_LIMIT];
    MapReader map=uniforms.asMap();MapReader::EntryIterator entries=map.entries();
    const char* name;ItemReader value;
    while (entries.next(&name,&value)) {
        Scene3dShaderUniform* u=&list[used++];
        // only materials sample; simulations declare a different host prefix
        if (!scene3d_shader_name(name,names?names:!samplers)) return scene3d_fail(build->entry,"invalid shader uniform name");
        str_copy(u->name,sizeof(u->name),name,strlen(name));
        bool color;int kind=scene3d_uniform_value(value,values,SCENE_SHADER_VALUE_LIMIT,&u->length,&color);
        if (kind<0 || (kind==0 && !samplers)) return scene3d_fail(build->entry,"invalid shader uniform value");
        if (kind==0) {
            const char* reference=value.isString()?value.cstring():value.asSymbol()->chars;
            if (++bound>SCENE_SHADER_SAMPLER_LIMIT) return scene3d_fail(build->entry,"shader sampler quota exceeded");
            if (const char* dot=strchr(reference,'.')) {
                // SH5: a simulation variable, sampled at its latest records each frame
                u->simulation=scene3d_simulation_find(build->entry,reference,dot-reference);
                while (u->simulation && u->variable<u->simulation->variable_count &&
                    strcmp(u->simulation->variables[u->variable].name,dot+1)) u->variable++;
                if (!u->simulation || u->variable==u->simulation->variable_count)
                    return scene3d_fail(build->entry,"shader uniform names no simulation variable");
                continue;
            }
            Scene3dDefinition* texture=scene3d_definition(build,nullptr,reference,"texture");
            u->texture=scene3d_texture(build,texture);if (!u->texture.id) return false;
            if (texture_hash) *texture_hash=hash_fnv1a_64_extend(*texture_hash,&texture->texture_hash,sizeof(uint64_t));
            continue;
        }
        u->authored=(float*)pool_alloc(pool,u->length*sizeof(float));u->values=(float*)pool_alloc(pool,u->length*sizeof(float));
        if (!u->authored || !u->values) return false;
        memcpy(u->authored,values,u->length*sizeof(float));memcpy(u->values,values,u->length*sizeof(float));
    }
    *result=list;*count=used;return true;
}
static Scene3dMaterial* scene3d_shader_material(Scene3dBuild* build, Scene3dDefinition* definition, Scene3dMaterial* material) {
    DomElement* node=definition->source;Pool* pool=build->entry->projection.pool;
    for(const auto& spec:scene3d_outdoor_specs) if(scene3d_present(node,spec.attribute)) {
        scene3d_fail(build->entry,"outdoor factors require a built-in material");return nullptr;
    }
    if(scene3d_present(node,"foliage-color")) {scene3d_fail(build->entry,"outdoor factors require a built-in material");return nullptr;}
    Scene3dShader* shader=(Scene3dShader*)pool_calloc(pool,sizeof(Scene3dShader));if (!shader) return nullptr;
    shader->vertex=scene3d_text(node,"vertex");shader->fragment=scene3d_text(node,"fragment");
    if (!shader->vertex || !shader->fragment || !*shader->vertex || !*shader->fragment) {
        scene3d_fail(build->entry,"shader material requires vertex and fragment sources");return nullptr;
    }
    material->shader=shader;material->source=dom_node_ref(node);
    for (unsigned c=0;c<4;c++) material->color[c]=1;
    const char* side=scene3d_text(node,"side");
    if ((side && strcmp(side,"front") && strcmp(side,"back") && strcmp(side,"double")) ||
        !scene3d_flag(node,"transparent",false,&material->source_transparent) ||
        !scene3d_flag(node,"alpha-to-coverage",false,&shader->alpha_to_coverage) || node->first_child) {
        scene3d_fail(build->entry,"invalid shader material side, transparency or children");return nullptr;
    }
    material->side=side&&!strcmp(side,"double")?2:side&&!strcmp(side,"back")?1:0;
    material->transparent=material->source_transparent;
    if (!scene3d_shader_uniforms(build,node,true,&shader->uniforms,&shader->uniform_count,&material->texture_hash)) return nullptr;
    definition->material=material;return material;
}
// projection and retained pose refresh must read the same animated material values.
static bool scene3d_material_values(Scene3dEntry* entry,DomElement* node,Scene3dMaterial* material) {
    if(!material->shader) {
        for(unsigned i=0;i<SCENE_OUTDOOR_COUNT;i++) {
            const auto& spec=scene3d_outdoor_specs[i];float* value=&material->outdoor[i];
            if((spec.standard&&!material->standard&&scene3d_present(node,spec.attribute))||
                !scene3d_animated_number(entry,node,spec.attribute,spec.fallback,value)||!scene3d_outdoor_valid(spec,*value))
                return scene3d_fail(entry,"invalid outdoor material factor or model");
        }
        if(!material->standard&&scene3d_present(node,"foliage-color")) return scene3d_fail(entry,"foliage color requires a standard material");
        float color[4];if(!scene3d_animated_color(entry,node,color,"foliage-color","#ffffff")) return scene3d_fail(entry,"invalid foliage color");
        memcpy(material->foliage_color,color,3*sizeof(float));
    }
    if(material->standard&&(!scene3d_animated_number(entry,node,"metalness",0,&material->pbr[0])||
        !scene3d_animated_number(entry,node,"roughness",1,&material->pbr[1])||
        !(material->pbr[0]>=0&&material->pbr[0]<=1)||!(material->pbr[1]>=0&&material->pbr[1]<=1)))
        return scene3d_fail(entry,"invalid material metalness or roughness");
    if(material->physical) {
        for(unsigned i=0;i<SCENE_PHYSICAL_COUNT;i++) {
            const auto& property=scene3d_physical_specs[i];float* value=&material->physical_values[i];AnimationValue animated={};
            if(!scene3d_animated_number(entry,node,property.attribute,property.fallback,value)||
                !scene3d_physical_valid(property,*value,scene3d_present(node,property.attribute)||
                    scene3d_animation_value(entry->animation,node,property.attribute,&animated))) {
                char message[160];snprintf(message,sizeof(message),"invalid physical material factor: %s",property.attribute);return scene3d_fail(entry,message);
            }
        }
        for(unsigned i=0;i<SCENE_PHYSICAL_COLORS;i++) {
            const auto& field=scene3d_physical_colors[i];float color[4];
            bool valid=scene3d_animated_color(entry,node,color,field.attribute,field.fallback);
            for(unsigned c=0;valid&&c<3;c++) valid=color[c]>=0&&color[c]<=1;
            if(!valid) {
                char message[160];snprintf(message,sizeof(message),"invalid physical material color: %s",field.attribute);return scene3d_fail(entry,message);
            }
            memcpy(material->physical_colors[i],color,3*sizeof(float));
        }
        double ior=material->physical_values[SCENE_IOR],ratio=(ior-1)/(ior+1);
        material->dielectric=(float)(ratio*ratio);
    }
    float opacity;
    if(!scene3d_animated_color(entry,node,material->color)||
        !scene3d_animated_number(entry,node,"opacity",1,&opacity)||opacity<0||opacity>1)
        return scene3d_fail(entry,"invalid material color or opacity");
    material->color[3]*=opacity;
    material->transparent=!material->force_opaque&&(material->source_transparent||material->color[3]<1);
    if(!material->shader) {
        float emissive[4],intensity;
        if(!scene3d_animated_color(entry,node,emissive,"emissive","#000000")||
            !scene3d_number(node,"emissive-intensity",1,&intensity)||intensity<0)
            return scene3d_fail(entry,"invalid material emission");
        for(unsigned c=0;c<3;c++) material->emissive[c]=emissive[c]*intensity;
    }
    return true;
}
// map coordinates live on the texture reference, independent of the image and other maps.
static bool scene3d_map_coordinates(Scene3dBuild* build,Scene3dDefinition* texture,Scene3dMaterial* material,unsigned slot) {
    if(!texture) return false;
    float set;bool present;
    if(!scene3d_number(texture->source,"uv-set",0,&set)||set<0||set>1||floorf(set)!=set||
        !scene3d_components(texture->source,"uv-transform",material->uv_rows[slot],6,&present))
        return scene3d_fail(build->entry,"texture requires uv-set 0 or 1 and a six-number UV transform");
    material->uv_sets[slot]=set;
    if(set==1||scene3d_present(texture->source,"uv-set")) material->required_uvs|=1u<<(unsigned)set;
    return true;
}
static Scene3dMaterial* scene3d_material(Scene3dBuild* build, Scene3dDefinition* definition) {
    if (!definition) { scene3d_fail(build->entry,"missing material reference"); return nullptr; }
    if (definition->material) return definition->material;
    DomElement* node=definition->source; Scene3dProjection* p=&build->entry->projection;
    Scene3dMaterial* material=(Scene3dMaterial*)pool_calloc(p->pool,sizeof(Scene3dMaterial)); if (!material) return nullptr;
    material->texture_hash=HASH_FNV1A_64_OFFSET_BASIS;
    const char* type=scene3d_text(node,"type");
    if (type && !strcmp(type,"shader")) return scene3d_shader_material(build,definition,material);
    if (!type || (strcmp(type,"basic") && strcmp(type,"lambert") && strcmp(type,"standard") && strcmp(type,"physical"))) { scene3d_fail(build->entry,"unknown material type"); return nullptr; }
    for(auto& rows:material->uv_rows) {rows[0]=1;rows[4]=1;}
    material->lambert=!strcmp(type,"lambert"); material->physical=!strcmp(type,"physical");material->standard=!strcmp(type,"standard")||material->physical;
    material->source=dom_node_ref(node);
    material->transmission=material->physical&&(scene3d_present(node,"transmission")||scene3d_present(node,"transmission-texture")||
        scene3d_animation_bound(build->entry->animation,node,"transmission"));
    if(!material->physical) {
        for(const auto& spec:scene3d_physical_specs) if(scene3d_present(node,spec.attribute)) {
            scene3d_fail(build->entry,"physical factors require a physical material");return nullptr;
        }
        for(const auto& field:scene3d_physical_colors) if(scene3d_present(node,field.attribute)) {
            scene3d_fail(build->entry,"physical colors require a physical material");return nullptr;
        }
    }
    if (!scene3d_flag(node,"transparent",false,&material->transparent) ||
        !scene3d_flag(node,"depth_only",false,&material->depth_only)) {
        scene3d_fail(build->entry,"invalid material color or opacity"); return nullptr;
    }
    const char* alpha_mode=scene3d_text(node,"alpha-mode");
    if(alpha_mode&&strcmp(alpha_mode,"opaque")&&strcmp(alpha_mode,"blend")&&strcmp(alpha_mode,"mask")) {
        scene3d_fail(build->entry,"invalid material alpha mode");return nullptr;
    }
    // glTF MASK: a cutout, opaque wherever it is drawn
    bool mask=alpha_mode&&!strcmp(alpha_mode,"mask");float cutoff;
    if (!scene3d_number(node,"alpha-cutoff",.5f,&cutoff) || !(cutoff>0 && cutoff<=1)) {
        scene3d_fail(build->entry,"invalid material alpha cutoff");return nullptr;
    }
    material->alpha_cutoff=mask?cutoff:0;
    material->force_opaque=alpha_mode&&(!strcmp(alpha_mode,"opaque")||mask);
    material->source_transparent=material->transparent||(alpha_mode&&!strcmp(alpha_mode,"blend"));
    if(!scene3d_material_values(build->entry,node,material)) return nullptr;
    const char* side=scene3d_text(node,"side");
    if (side && strcmp(side,"front") && strcmp(side,"back") && strcmp(side,"double")) {
        scene3d_fail(build->entry,"invalid material side"); return nullptr;
    }
    material->side=side&&!strcmp(side,"double")?2:side&&!strcmp(side,"back")?1:0;
    const char* texture=scene3d_text(node,"texture");
    Scene3dDefinition* texture_definition=texture?scene3d_definition(build,nullptr,texture,"texture"):nullptr;
    for (DomNode* child=node->first_child;child;child=child->next_sibling) if (child->is_element()) {
        if (!scene3d_tag(child->as_element(),"texture") || texture_definition) { scene3d_fail(build->entry,"invalid material child"); return nullptr; }
        texture_definition=scene3d_definition(build,child->as_element(),nullptr,"texture");
    }
    if (texture || texture_definition) {
        material->maps[SCENE_MAP_BASE]=scene3d_texture(build,texture_definition);
        if (!material->maps[SCENE_MAP_BASE].id||!scene3d_map_coordinates(build,texture_definition,material,0)) return nullptr;
        material->texture_hash=hash_fnv1a_64_extend(material->texture_hash,&texture_definition->texture_hash,sizeof(uint64_t));
        // image alpha participates in the same ordered transparent pass.
        material->transparent=material->transparent || texture_definition->texture_alpha;
        material->source_transparent=material->source_transparent||texture_definition->texture_alpha;
    }
    // one shared descriptor table owns every built-in map's slot and color space.
    for(unsigned slot=1;slot<SCENE_MAP_COUNT;slot++) {
        const Scene3dMapSpec& spec=scene3d_map_specs[slot];const char* reference=scene3d_text(node,spec.attribute);
        if(!reference) continue;
        if(!material->standard||(slot>=SCENE_MAP_CLEARCOAT&&!material->physical)) {
            scene3d_fail(build->entry,slot>=SCENE_MAP_CLEARCOAT?"extension maps require a physical material":"texture maps require a standard material");return nullptr;
        }
        Scene3dDefinition* map=scene3d_definition(build,nullptr,reference,"texture");
        material->maps[slot]=scene3d_texture(build,map,spec.srgb);
        if(!material->maps[slot].id||!scene3d_map_coordinates(build,map,material,slot)) return nullptr;
        material->texture_hash=hash_fnv1a_64_extend(material->texture_hash,&map->texture_hash,sizeof(uint64_t));
    }
    if (!scene3d_number(node,"normal-scale",1,&material->normal_scale) || !isfinite(material->normal_scale) ||
        !scene3d_number(node,"occlusion-strength",1,&material->occlusion_strength) ||
        !(material->occlusion_strength>=0 && material->occlusion_strength<=1)) {
        scene3d_fail(build->entry,"invalid material normal scale or occlusion strength"); return nullptr;
    }
    // glTF OPAQUE ignores alpha from both factors and textures and writes opaque depth.
    if(material->force_opaque) material->transparent=false;
    if (material->depth_only && material->transparent) {
        scene3d_fail(build->entry,"depth-only material must be opaque");return nullptr;
    }
    definition->material=material; return material;
}

static float scene3d_determinant(const float* m) {
    return m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
}
static unsigned scene3d_count_bones(DomElement* node,unsigned depth=0) {
    unsigned count=0;
    if(depth>64) return 0;
    for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) {
        count+=1+scene3d_count_bones(child->as_element(),depth+1);
    }
    return count;
}
static bool scene3d_collect_bones(Scene3dBuild* build,Scene3dMesh* mesh,DomElement* node,unsigned depth=0) {
    if(depth>64) return false;
    for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) {
        DomElement* bone=child->as_element();if(!scene3d_tag(bone,"bone")||mesh->bone_count>=mesh->bone_capacity) return false;
        Scene3dMatrix world;bool visible;unsigned index=mesh->bone_count++;
        if(!scene3d_components(bone,"inverse-bind-matrix",mesh->inverse_bind[index].v,16,&mesh->authored_bind[index])) return false;
        if(mesh->authored_bind[index]) {
            Scene3dMatrix inverse;if(!scene3d_inverse_affine(mesh->inverse_bind[index],&inverse)) return false;
        } else if(!scene3d_world(build->entry,bone,&world,&visible,true)||!scene3d_inverse_affine(world,&mesh->inverse_bind[index])) return false;
        mesh->bones[index]=dom_node_ref(bone);
        if(!scene3d_collect_bones(build,mesh,bone,depth+1)) return false;
    }
    return true;
}

static bool scene3d_transparent(const Scene3dMaterial* material,const Scene3dGeometry* geometry) {
    return material->transparent||(!material->force_opaque&&geometry->vertex_alpha);
}
static bool scene3d_transparent(const Scene3dMesh* mesh) {return scene3d_transparent(mesh->material,mesh->geometry);}

// SH1: what the host declares ahead of a shader material's GLSL: the built-in vertex layout and the
// camera. `#line 1` makes driver diagnostics count lines from the authored body.
static const char* scene3d_shader_common = R"GLSL(#version 330 core
uniform mat4 model_matrix, view_matrix, projection_matrix;
uniform vec3 camera_position;
)GLSL";
static const char* scene3d_shader_inputs = R"GLSL(layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=4) in mat4 instance_matrix;
layout(location=14) in vec4 tangent;
layout(location=15) in vec2 uv1;
)GLSL";
// GL uniform types a shader material may declare (glGetActiveUniform)
static constexpr unsigned SCENE_GL_FLOAT = 0x1406, SCENE_GL_VEC2 = 0x8B50, SCENE_GL_VEC3 = 0x8B51, SCENE_GL_VEC4 = 0x8B52,
    SCENE_GL_MAT4 = 0x8B5C, SCENE_GL_SAMPLER_2D = 0x8B5E;

// every active uniform the program declares beyond those the host binds (`host`) needs an authored value of its type
static bool scene3d_shader_validate(Scene3dBuild* build, Scene3dShaderUniform* uniforms, unsigned uniform_count,
        NativeGlResource program, const char* const* host, unsigned host_count) {
    NativeGlContext* graphics=build->entry->graphics;char message[160];
    for (unsigned i=0,count=native_gl_uniform_count(graphics,program);i<count;i++) {
        char name[64];unsigned type;int size;
        if (!native_gl_uniform_info(graphics,program,i,name,sizeof(name),&type,&size)) return scene3d_fail(build->entry,"shader uniform reflection failed");
        char* bracket=strchr(name,'[');if (bracket) *bracket=0;
        bool bound=!strncmp(name,"gl_",3);
        for (unsigned k=0;k<host_count && !bound;k++) bound=!strcmp(name,host[k]);
        if (bound) continue;
        Scene3dShaderUniform* u=nullptr;
        for (unsigned k=0;k<uniform_count && !u;k++) if (!strcmp(uniforms[k].name,name)) u=&uniforms[k];
        unsigned components=type==SCENE_GL_FLOAT?1:type==SCENE_GL_VEC2?2:type==SCENE_GL_VEC3?3:type==SCENE_GL_VEC4?4:type==SCENE_GL_MAT4?16:0;
        bool sampled=u && (u->texture.id || u->simulation);
        const char* problem=!u?"has no value":type==SCENE_GL_SAMPLER_2D?(sampled?nullptr:"needs a texture"):
            !components?"has an unsupported type":sampled||u->length!=components*(unsigned)size?"does not match its declared type":
            u->components&&u->components!=components?"is declared differently by another program":nullptr;
        if (problem) { snprintf(message,sizeof(message),"shader uniform %s %s",name,problem);return scene3d_fail(build->entry,message); }
        if (components) u->components=components;
    }
    return true;
}
// one source-keyed cache serves mesh shaders and custom image passes across reprojections.
static NativeGlResource scene3d_cached_program(Scene3dEntry* entry, const char* vertex, const char* fragment, const char* label) {
    uint64_t key=hash_fnv1a_64_extend_cstr(hash_fnv1a_64_extend_cstr(HASH_FNV1A_64_OFFSET_BASIS,vertex),fragment);
    NativeGlResource program={};
    for (unsigned i=0;i<entry->shader_program_count && !program.id;i++) if (entry->shader_programs[i].key==key) {
        entry->shader_programs[i].generation=entry->projection_generation+1;program=entry->shader_programs[i].program;
    }
    if (!program.id && entry->shader_program_count>=SCENE_SHADER_PROGRAM_LIMIT) scene3d_fail(entry,"shader program quota exceeded");
    else if (!program.id) {
        program=native_gl_program(entry->graphics,vertex,fragment);
        if (program.id) {
            entry->shader_programs[entry->shader_program_count++]={key,entry->projection_generation+1,program};
            entry->stats.shader_links++;
        }
        else {
            char message[2048];snprintf(message,sizeof(message),"%s: %s",label,native_gl_diagnostic(entry->graphics));
            scene3d_fail(entry,message);
        }
    }
    return program;
}

// SH6: the projection under construction reuses a program linked by an earlier one
static NativeGlResource scene3d_shader_program(Scene3dBuild* build, Scene3dShader* shader,
        const Scene3dInstanceData* data, unsigned data_count, bool rgba) {
    Scene3dEntry* entry=build->entry;
    static const char* types[]={"float","vec2","vec3","vec4"};
    StrBuf* vertex=strbuf_new();strbuf_append_str(vertex,scene3d_shader_common);strbuf_append_str(vertex,scene3d_shader_inputs);
    strbuf_append_format(vertex,"layout(location=3) in %s color;\n",rgba?"vec4":"vec3");
    for (unsigned i=0;i<data_count;i++)
        strbuf_append_format(vertex,"layout(location=%u) in %s %s;\n",8+i,types[data[i].components-1],data[i].name);
    strbuf_append_str(vertex,"#line 1\n");strbuf_append_str(vertex,shader->vertex);
    StrBuf* fragment=strbuf_new();strbuf_append_str(fragment,scene3d_shader_common);
    strbuf_append_str(fragment,"out vec4 fragment_color;\n#line 1\n");strbuf_append_str(fragment,shader->fragment);
    NativeGlResource program=scene3d_cached_program(entry,vertex->str,fragment->str,"shader material");
    strbuf_free(vertex);strbuf_free(fragment);
    static const char* host[]={"model_matrix","view_matrix","projection_matrix","camera_position"};
    if (program.id && !scene3d_shader_validate(build,shader->uniforms,shader->uniform_count,program,host,4)) return {};
    return program;
}
// programs the finished projection no longer uses are released; a failed one keeps them all
static void scene3d_shader_prune(Scene3dEntry* entry) {
    unsigned kept=0;
    for (unsigned i=0;i<entry->shader_program_count;i++) {
        if (entry->shader_programs[i].generation==entry->projection_generation) entry->shader_programs[kept++]=entry->shader_programs[i];
        else native_gl_release(entry->graphics,entry->shader_programs[i].program);
    }
    entry->shader_program_count=kept;entry->stats.shader_programs=kept;
}

// one retained triangle serves simulation, postprocessing and output without duplicated uploads.
static bool scene3d_fullscreen(Scene3dEntry* entry) {
    if (entry->fullscreen_triangle.id) return true;
    const float corners[]={-1,-1,0, 3,-1,0, -1,3,0};
    if (!entry->fullscreen_buffer.id) entry->fullscreen_buffer=native_gl_buffer(entry->graphics,corners,sizeof(corners),false);
    NativeGlAttribute attribute={entry->fullscreen_buffer,0,3,0,0,0};
    if (entry->fullscreen_buffer.id) entry->fullscreen_triangle=native_gl_vertices(entry->graphics,&attribute,1,{});
    return entry->fullscreen_triangle.id || scene3d_fail(entry,native_gl_diagnostic(entry->graphics));
}

// SH5: a simulation pass draws one full-screen triangle into each variable's next records
static const char* scene3d_fullscreen_vertex = R"GLSL(
layout(location=0) in vec3 position;
void main() { gl_Position = vec4(position.xy, 0.0, 1.0); }
)GLSL";
static NativeGlResource scene3d_fullscreen_program(Scene3dEntry* entry, const char* fragment, const char* version) {
    StrBuf* vertex=strbuf_create(version);strbuf_append_str(vertex,scene3d_fullscreen_vertex);
    NativeGlResource program=native_gl_program(entry->graphics,vertex->str,fragment);strbuf_free(vertex);return program;
}
// material, simulation and image-pass uniforms share reflection, sampling and numeric updates.
static bool scene3d_bind_uniforms(Scene3dEntry* entry, NativeGlResource program,
        Scene3dShaderUniform* uniforms, unsigned count, NativeGlDraw* draw) {
    for (unsigned i=0;i<count;i++) {
        const Scene3dShaderUniform* u=&uniforms[i];
        if (u->texture.id || u->simulation) {
            if (draw->binding_count>=NATIVE_GL_BINDING_LIMIT) return scene3d_fail(entry,"shader sampler quota exceeded");
            draw->bindings[draw->binding_count++]=u->texture.id?NativeGlBinding{u->name,u->texture,NATIVE_GL_BIND_TEXTURE}:
                NativeGlBinding{u->name,u->simulation->variables[u->variable].state[u->simulation->current],NATIVE_GL_BIND_FLOAT};
        } else if (u->components && !scene3d_uniform(entry,program,u->name,u->values,u->components,u->length/u->components)) return false;
    }
    return true;
}
static bool scene3d_effect_numbers(Scene3dEntry* entry, DomElement* node, float* strength, float* threshold, float* radius) {
    return (scene3d_animated_number(entry,node,"strength",1,strength) && *strength>=0 &&
        scene3d_animated_number(entry,node,"threshold",1,threshold) && *threshold>=0 &&
        scene3d_animated_number(entry,node,"radius",2,radius) && *radius>=0) ||
        scene3d_fail(entry,"bloom strength, threshold and radius must be finite nonnegative numbers");
}
static bool scene3d_build_effects(Scene3dBuild* build) {
    Scene3dEntry* entry=build->entry;Scene3dProjection* p=&entry->projection;
    DomElement* chain=nullptr;bool display=false;
    for (DomNode* child=build->root->first_child;child;child=child->next_sibling) if (child->is_element() && scene3d_tag(child->as_element(),"postprocessing")) {
        if (chain) return scene3d_fail(entry,"scene accepts one postprocessing chain");
        chain=child->as_element();
    }
    for (DomNode* child=chain?chain->first_child:nullptr;child;child=child->next_sibling) if (child->is_element()) {
        DomElement* node=child->as_element();const char* type=scene3d_text(node,"type"),*domain=scene3d_text(node,"domain");
        if (!scene3d_tag(node,"effect") || node->first_child || p->effect_count>=SCENE_EFFECT_LIMIT)
            return scene3d_fail(entry,"postprocessing requires at most eight effect children");
        Scene3dEffect* effect=&p->effects[p->effect_count++];effect->source=dom_node_ref(node);
        if (type && !strcmp(type,"bloom")) effect->kind=SCENE_EFFECT_BLOOM;
        else if (type && !strcmp(type,"fxaa")) effect->kind=SCENE_EFFECT_FXAA;
        else if (type && !strcmp(type,"shader")) effect->kind=SCENE_EFFECT_SHADER;
        else return scene3d_fail(entry,"unknown postprocessing effect");
        effect->display=domain?!strcmp(domain,"display"):effect->kind==SCENE_EFFECT_FXAA;
        if ((domain && strcmp(domain,"linear") && strcmp(domain,"display")) ||
            (effect->kind==SCENE_EFFECT_BLOOM && effect->display) || (effect->kind==SCENE_EFFECT_FXAA && !effect->display) ||
            (display && !effect->display)) return scene3d_fail(entry,"postprocessing requires linear effects before display effects");
        display=effect->display;
        if (effect->display) p->display_effects++;else p->linear_effects++;
        if (effect->kind==SCENE_EFFECT_BLOOM) {
            float strength,threshold,radius;
            if (p->background[3]!=1) return scene3d_fail(entry,"bloom currently requires an opaque viewport background");
            if (!scene3d_effect_numbers(entry,node,&strength,&threshold,&radius)) return false;
        }
        if (effect->kind!=SCENE_EFFECT_SHADER) {
            if (!scene3d_value(node,"uniforms").isNull()) return scene3d_fail(entry,"built-in effects take numeric options, not uniforms");
            continue;
        }
        const char* body=scene3d_text(node,"fragment");
        if (!body || !*body) return scene3d_fail(entry,"shader effect requires a fragment source");
        if (!scene3d_shader_uniforms(build,node,true,&effect->uniforms,&effect->uniform_count,nullptr,2)) return false;
        StrBuf* vertex=strbuf_create("#version 330 core\n");strbuf_append_str(vertex,scene3d_fullscreen_vertex);
        StrBuf* fragment=strbuf_create(scene3d_post_header);
        strbuf_append_str(fragment,"#define main effect_main\n#line 1\n");strbuf_append_str(fragment,body);
        // coverage belongs to the input; a color pass cannot accidentally destroy silhouette alpha.
        strbuf_append_str(fragment,"\n#undef main\nvoid main(){effect_main();fragment_color.a=texture(color_input,gl_FragCoord.xy/resolution).a;}\n");
        effect->program=scene3d_cached_program(entry,vertex->str,fragment->str,"shader effect");
        strbuf_free(vertex);strbuf_free(fragment);
        static const char* host[]={"color_input","resolution"};
        if (!effect->program.id || !scene3d_shader_validate(build,effect->uniforms,effect->uniform_count,effect->program,host,2)) return false;
    }
    return true;
}
static void scene3d_simulation_release(NativeGlContext* graphics, Scene3dSimulation* simulation) {
    for (unsigned v=0;v<simulation->variable_count;v++) {
        Scene3dVariable* variable=&simulation->variables[v];
        // a lost context has already destroyed the GPU objects
        if (graphics) for (NativeGlResource resource:{variable->state[0],variable->state[1],variable->program}) native_gl_release(graphics,resource);
        if (variable->initial) mem_free(variable->initial);
    }
    mem_free(simulation);
}
// SH5/SH6: a <simulation> of one to four <variable>s, reused across projections while its structure is unchanged
static bool scene3d_simulation(Scene3dBuild* build, DomElement* node) {
    Scene3dEntry* entry=build->entry;Pool* pool=entry->projection.pool;
    const char* id=scene3d_text(node,"id");float width,height,timestep;
    if (!id || !*id || strlen(id)>=sizeof(Scene3dSimulation::id) || strchr(id,'.') ||
        !scene3d_number(node,"width",0,&width) || !scene3d_number(node,"height",0,&height) ||
        !scene3d_number(node,"timestep",1.0f/60,&timestep) || width<1 || height<1 || floorf(width)!=width ||
        floorf(height)!=height || width*height>SCENE_SIMULATION_RECORDS || !(timestep>0 && timestep<=1))
        return scene3d_fail(entry,"simulation requires an ID without dots, at most 262144 width × height records and a timestep in (0, 1]");
    unsigned records=(unsigned)(width*height);
    struct { const char *name,*shader; float* initial; unsigned length; } authored[SCENE_VARIABLE_LIMIT];
    unsigned count=0;float shape[3]={width,height,timestep};
    uint64_t key=hash_fnv1a_64_extend(hash_fnv1a_64_extend_cstr(HASH_FNV1A_64_OFFSET_BASIS,id),shape,sizeof(shape));
    for (DomNode* child=node->first_child;child;child=child->next_sibling) if (child->is_element()) {
        DomElement* element=child->as_element();
        if (!scene3d_tag(element,"variable") || count>=SCENE_VARIABLE_LIMIT || element->first_child)
            return scene3d_fail(entry,"simulation requires one to four variable children");
        auto& v=authored[count++];v.name=scene3d_text(element,"name");v.shader=scene3d_text(element,"shader");
        if (!v.name || !scene3d_shader_name(v.name,true) ||
            !v.shader || !*v.shader || !scene3d_numbers(element,"initial",pool,&v.initial,&v.length,records*4) ||
            (v.length && v.length!=records*4))
            return scene3d_fail(entry,"simulation variable requires a GLSL name, a shader and 4 initial numbers per record");
        for (unsigned k=0;k+1<count;k++) if (!strcmp(authored[k].name,v.name)) return scene3d_fail(entry,"duplicate simulation variable");
        key=hash_fnv1a_64_extend_cstr(hash_fnv1a_64_extend_cstr(key,v.name),v.shader);
        if (v.length) key=hashmap_xxhash3(v.initial,v.length*sizeof(float),key,0);
    }
    if (!count) return scene3d_fail(entry,"simulation requires one to four variable children");
    Scene3dShaderUniform* uniforms;unsigned uniform_count;bool timed=false;
    if (!scene3d_shader_uniforms(build,node,false,&uniforms,&uniform_count,nullptr)) return false;
    for (unsigned i=0;i<uniform_count;i++) {
        timed=timed||(!strcmp(uniforms[i].name,"time")&&uniforms[i].length==1);
        for (unsigned k=0;k<count;k++) if (!strcmp(uniforms[i].name,authored[k].name))
            return scene3d_fail(entry,"simulation uniform repeats a variable name");
    }
    if (!timed) return scene3d_fail(entry,"simulation requires a numeric time uniform");
    Scene3dSimulation* simulation=nullptr;
    for (unsigned i=0;i<entry->simulation_count && !simulation;i++)
        if (entry->simulations[i]->key==key && !strcmp(entry->simulations[i]->id,id)) simulation=entry->simulations[i];
    if (!simulation) {
        if (entry->simulation_count>=SCENE_SIMULATION_LIMIT) return scene3d_fail(entry,"simulation quota exceeded");
        if (!scene3d_fullscreen(entry)) return false;
        simulation=(Scene3dSimulation*)mem_calloc(1,sizeof(Scene3dSimulation),MEM_CAT_RENDER);if (!simulation) return false;
        str_copy(simulation->id,sizeof(simulation->id),id,strlen(id));simulation->key=key;simulation->timestep=timestep;
        simulation->width=(unsigned)width;simulation->height=(unsigned)height;simulation->variable_count=count;
        bool made=true;
        for (unsigned v=0;v<count && made;v++) {
            Scene3dVariable* variable=&simulation->variables[v];
            str_copy(variable->name,sizeof(variable->name),authored[v].name,strlen(authored[v].name));
            variable->initial=(float*)mem_calloc(records*4,sizeof(float),MEM_CAT_RENDER);
            if (variable->initial && authored[v].length) memcpy(variable->initial,authored[v].initial,records*4*sizeof(float));
            for (NativeGlResource& state:variable->state)
                state=native_gl_float_target(entry->graphics,simulation->width,simulation->height,false,true);
            // the host declares every variable's sampler, the grid size and the step ahead of the authored body
            StrBuf* fragment=strbuf_new();strbuf_append_str(fragment,"#version 330 core\nuniform vec2 resolution;\nuniform float delta;\n");
            for (unsigned k=0;k<count;k++) strbuf_append_format(fragment,"uniform sampler2D %s;\n",authored[k].name);
            strbuf_append_str(fragment,"out vec4 fragment_color;\n#line 1\n");strbuf_append_str(fragment,authored[v].shader);
            variable->program=scene3d_fullscreen_program(entry,fragment->str,"#version 330 core\n");strbuf_free(fragment);
            made=variable->initial && variable->state[0].id && variable->state[1].id && variable->program.id;
        }
        if (!made) {
            char message[2048];snprintf(message,sizeof(message),"simulation %s: %s",id,native_gl_diagnostic(entry->graphics));
            scene3d_simulation_release(entry->graphics,simulation);return scene3d_fail(entry,message);
        }
        entry->simulations[entry->simulation_count++]=simulation;
    }
    simulation->generation=entry->projection_generation+1;simulation->source=dom_node_ref(node);
    simulation->uniforms=uniforms;simulation->uniform_count=uniform_count;
    const char* host[2+SCENE_VARIABLE_LIMIT]={"resolution","delta"};
    for (unsigned v=0;v<count;v++) host[2+v]=simulation->variables[v].name;
    for (unsigned v=0;v<count;v++)
        if (!scene3d_shader_validate(build,uniforms,uniform_count,simulation->variables[v].program,host,2+count)) return false;
    return true;
}
// simulations come first, so material uniforms can name their variables
static bool scene3d_build_simulations(Scene3dBuild* build) {
    for (unsigned i=0;i<build->definition_count;i++)
        if (scene3d_tag(build->definitions[i].source,"simulation") && !scene3d_simulation(build,build->definitions[i].source)) return false;
    return true;
}
// simulations the finished projection no longer declares are released with their state
static void scene3d_simulation_prune(Scene3dEntry* entry) {
    unsigned kept=0;
    for (unsigned i=0;i<entry->simulation_count;i++) {
        Scene3dSimulation* simulation=entry->simulations[i];
        if (simulation->generation==entry->projection_generation) entry->simulations[kept++]=simulation;
        else scene3d_simulation_release(entry->graphics,simulation);
    }
    entry->simulation_count=kept;
}
// one fixed step: each variable's next records from every variable's current ones
static bool scene3d_simulation_pass(Scene3dEntry* entry, Scene3dSimulation* simulation) {
    NativeGlContext* graphics=entry->graphics;unsigned next=1-simulation->current;
    float resolution[2]={(float)simulation->width,(float)simulation->height},delta=(float)simulation->timestep;
    for (unsigned v=0;v<simulation->variable_count;v++) {
        NativeGlResource program=simulation->variables[v].program;
        if (!native_gl_begin_region(graphics,simulation->variables[v].state[next],0,0,simulation->width,simulation->height,nullptr) ||
            !scene3d_uniform(entry,program,"resolution",resolution,2,1) || !scene3d_uniform(entry,program,"delta",&delta,1,1)) return false;
        NativeGlDraw draw={program,entry->fullscreen_triangle,3,1,false,false,2,false};
        if (!scene3d_bind_uniforms(entry,program,simulation->uniforms,simulation->uniform_count,&draw)) return false;
        for (unsigned k=0;k<simulation->variable_count;k++) {
            const Scene3dVariable* variable=&simulation->variables[k];
            draw.bindings[draw.binding_count++]={variable->name,variable->state[simulation->current],NATIVE_GL_BIND_FLOAT};
        }
        if (!native_gl_draw(graphics,&draw)) return false;
    }
    simulation->current=next;entry->stats.simulation_steps++;return true;
}
// SH5: each simulation advances in fixed steps up to its `time` uniform; time going back restarts it
static bool scene3d_simulations_advance(Scene3dEntry* entry) {
    for (unsigned i=0;i<entry->simulation_count;i++) {
        Scene3dSimulation* simulation=entry->simulations[i];double now=0;
        for (unsigned u=0;u<simulation->uniform_count;u++)
            if (!strcmp(simulation->uniforms[u].name,"time")) now=simulation->uniforms[u].values[0];
        // the clock may lead `now` by the step tolerance; only a real rewind restarts
        if (!simulation->started || now+1e-3<simulation->clock) {
            for (unsigned v=0;v<simulation->variable_count;v++)
                if (!native_gl_float_target_update(entry->graphics,simulation->variables[v].state[simulation->current],
                    simulation->variables[v].initial)) return false;
            simulation->clock=0;simulation->started=true;
        }
        unsigned steps=0;
        while (simulation->clock+simulation->timestep<=now+1e-4 && steps<SCENE_SIMULATION_STEPS) {
            if (!scene3d_simulation_pass(entry,simulation)) return false;
            simulation->clock+=simulation->timestep;steps++;
        }
        // far behind after a long seek: resynchronize rather than stall the frame
        if (simulation->clock+simulation->timestep<=now) simulation->clock=now;
    }
    return true;
}

static bool scene3d_drawable_values(Scene3dEntry* entry,DomElement* node,Scene3dMesh* mesh) {
    Scene3dTopology kind=mesh->geometry->topology;if(kind==SCENE_TRIANGLES) return true;
    const char* mode=scene3d_text(node,"size-mode");
    if(mode&&strcmp(mode,"screen")&&strcmp(mode,"world")) return scene3d_fail(entry,"size-mode must be screen or world");
    mesh->screen_size=mode?!strcmp(mode,"screen"):kind!=SCENE_SPRITE;
    mesh->drawable_size[0]=mesh->drawable_size[1]=1;mesh->drawable_center[0]=mesh->drawable_center[1]=.5f;
    if(kind==SCENE_SPRITE) {
        bool present;
        if(!scene3d_components(node,"size",mesh->drawable_size,2,&present)||!scene3d_components(node,"center",mesh->drawable_center,2,&present))
            return scene3d_fail(entry,"sprite size and center require two numbers");
        AnimationValue value={};
        if(scene3d_animation_value(entry->animation,node,"size",&value)) for(unsigned c=0;c<2;c++) mesh->drawable_size[c]=(float)value.numbers[c];
        if(scene3d_animation_value(entry->animation,node,"center",&value)) for(unsigned c=0;c<2;c++) mesh->drawable_center[c]=(float)value.numbers[c];
    } else {
        if(!scene3d_animated_number(entry,node,kind==SCENE_POINTS?"size":"width",1,&mesh->drawable_size[0]))
            return scene3d_fail(entry,"drawable size must be a positive number");
        mesh->drawable_size[1]=mesh->drawable_size[0];
    }
    for(unsigned c=0;c<2;c++) if(!isfinite(mesh->drawable_size[c])||mesh->drawable_size[c]<=0||!isfinite(mesh->drawable_center[c]))
        return scene3d_fail(entry,"drawable size must be positive and its center finite");
    return true;
}
static bool scene3d_mesh_record(Scene3dBuild* build, Scene3dGeometry* geometry, Scene3dMaterial* material,
    const Scene3dMatrix& world, const float* matrices, unsigned count, bool clockwise,
    const Scene3dMatrix& local_instance=scene3d_identity(), const Scene3dInstanceData* data=nullptr, unsigned data_count=0) {
    Scene3dProjection* p=&build->entry->projection;
    if (p->mesh_count>=SCENE_NODE_LIMIT) return scene3d_fail(build->entry,"draw record quota exceeded");
    Scene3dMesh* mesh=(Scene3dMesh*)pool_calloc(p->pool,sizeof(Scene3dMesh)); if (!mesh) return false;
    mesh->geometry=geometry;mesh->material=material;mesh->world=world;mesh->clockwise=geometry->topology==SCENE_TRIANGLES&&clockwise;mesh->instance_count=count;
    mesh->source=dom_node_ref(build->mesh_source);mesh->local_instance=local_instance;mesh->visible=true;
    mesh->lod=build->lod;
    if(!scene3d_flag(build->mesh_source,"frustum-culling",true,&mesh->frustum_culling)) return scene3d_fail(build->entry,"invalid frustum-culling flag");
    float* bounds;unsigned bound_count;
    mesh->authored_bounds=scene3d_present(build->mesh_source,"bounds");
    if(!scene3d_numbers(build->mesh_source,"bounds",p->pool,&bounds,&bound_count,6)||(mesh->authored_bounds&&bound_count!=6))
        return scene3d_fail(build->entry,"bounds requires six ordered finite numbers");
    if(mesh->authored_bounds) {
        mesh->bounds_lo={bounds[0],bounds[1],bounds[2]};mesh->bounds_hi={bounds[3],bounds[4],bounds[5]};
        if(bounds[0]>bounds[3]||bounds[1]>bounds[4]||bounds[2]>bounds[5]) return scene3d_fail(build->entry,"bounds requires six ordered finite numbers");
    }
    mesh->layer=build->layer;mesh->instance_hash=matrices?hashmap_xxhash3(matrices,count*16*sizeof(float),count,0):0;
    for (unsigned i=0;i<data_count;i++)
        mesh->instance_hash=hashmap_xxhash3(data[i].values,count*data[i].components*sizeof(float),mesh->instance_hash,0);
    mesh->instance_matrices=matrices;
    if(!scene3d_drawable_values(build->entry,build->mesh_source,mesh)) return false;
    bool shaded=material->shader!=nullptr;
    // the host prefix declares no deformation lanes; the vertex shader owns every displacement
    if (shaded && (geometry->bone_index_count || geometry->morph_count || scene3d_text(build->mesh_source,"skeleton")))
        return scene3d_fail(build->entry,"shader materials cannot draw skinned or morphed geometry");
    // SP3: meshes cast and receive unless they opt out; it matters only once a light casts
    if (!scene3d_flag(build->mesh_source,"cast-shadow",geometry->topology==SCENE_TRIANGLES,&mesh->cast_shadow) ||
        !scene3d_flag(build->mesh_source,"receive-shadow",geometry->topology==SCENE_TRIANGLES,&mesh->receive_shadow))
        return scene3d_fail(build->entry,"invalid mesh shadow flags");
    if(geometry->topology!=SCENE_TRIANGLES&&(mesh->cast_shadow||mesh->receive_shadow)) return scene3d_fail(build->entry,"unlit drawables do not cast or receive shadows");
    mesh->instance_mirrored=clockwise!=(scene3d_determinant(world.v)<0);
    if(geometry->morph_count) {
        float* values;unsigned count;
        if(!scene3d_numbers(build->mesh_source,"morph-weights",p->pool,&values,&count,2)||count!=geometry->morph_count)
            return scene3d_fail(build->entry,"morph weights must match the geometry targets");
        for(unsigned target=0;target<count;target++) mesh->morph_base_weights[target]=mesh->morph_weights[target]=values[target];
    }
    const char* skeleton=scene3d_text(build->mesh_source,"skeleton");
    if(skeleton||geometry->bone_index_count) {
        Scene3dDefinition* rig=scene3d_definition(build,nullptr,skeleton,"skeleton");bool visible;
        unsigned bones=rig?scene3d_count_bones(rig->source):0;
        if(bones>SCENE_BONE_LIMIT) return scene3d_fail(build->entry,"skeleton exceeds 256 bones");
        if(bones) {
            mesh->bones=(DomNodeRef*)pool_calloc(p->pool,bones*sizeof(DomNodeRef));
            mesh->inverse_bind=(Scene3dMatrix*)pool_calloc(p->pool,bones*sizeof(Scene3dMatrix));
            mesh->bone_matrices=(Scene3dMatrix*)pool_calloc(p->pool,bones*sizeof(Scene3dMatrix));
            mesh->authored_bind=(bool*)pool_calloc(p->pool,bones*sizeof(bool));
            if(!mesh->bones||!mesh->inverse_bind||!mesh->bone_matrices||!mesh->authored_bind) return false;
            mesh->bone_capacity=bones;
        }
        if(!rig||!scene3d_collect_bones(build,mesh,rig->source)||!geometry->bone_index_count||
            mesh->bone_count<geometry->bone_index_count||!scene3d_world(build->entry,build->mesh_source,&mesh->bind_matrix,&visible,true))
            return scene3d_fail(build->entry,"invalid skeleton, bind pose or skin indices");
        if(count!=1||matrices||memcmp(local_instance.v,scene3d_identity().v,sizeof(local_instance.v)))
            return scene3d_fail(build->entry,"skinned meshes require a single identity instance");
        mesh->palette=native_gl_data_texture(build->entry->graphics,4,mesh->bone_count);
        if(!scene3d_resource(build,mesh->palette)) return false;
    }
    bool unit=!matrices;
    if (unit && !shaded && geometry->unit_vertices.id) mesh->vertices=geometry->unit_vertices;
    else {
        if (unit && !p->identity.id) {
            Scene3dMatrix identity=scene3d_identity();
            p->identity=native_gl_buffer(build->entry->graphics,identity.v,sizeof(identity.v),false);
            if (!scene3d_resource(build,p->identity)) return false;
        }
        mesh->instances=unit?p->identity:native_gl_buffer(build->entry->graphics,matrices,count*16*sizeof(float),false);
        if (!unit && !scene3d_resource(build,mesh->instances)) return false;
        NativeGlAttribute attributes[16]={};unsigned attribute_count=0;
        for(unsigned lane=0;lane<SCENE_LANE_COUNT;lane++) if(geometry->lanes&(1u<<lane)) {
            const Scene3dLane& field=scene3d_lanes[lane];
            attributes[attribute_count++]={geometry->vertices,field.location,lane==3&&!geometry->rgba?3:field.components,
                geometry->vertex_stride,geometry->offsets[lane],0};
        }
        // SH3: `instance-count` instances without matrices all read the one identity matrix
        unsigned divisor=unit&&count>1?count:1;
        for(unsigned i=0;i<4;i++) attributes[attribute_count++]={mesh->instances,i+4,4,16*sizeof(float),i*4*sizeof(float),divisor};
        for(unsigned i=0;i<data_count;i++) {
            NativeGlResource buffer=native_gl_buffer(build->entry->graphics,data[i].values,count*data[i].components*sizeof(float),false);
            if (!scene3d_resource(build,buffer)) return false;
            attributes[attribute_count++]={buffer,8+i,data[i].components,0,0,1};
        }
        mesh->vertices=native_gl_vertices(build->entry->graphics,attributes,attribute_count,geometry->indices);
        if (!scene3d_resource(build,mesh->vertices)) return false;
        if (unit && !shaded) geometry->unit_vertices=mesh->vertices;
    }
    if (shaded && !(mesh->program=scene3d_shader_program(build,material->shader,data,data_count,geometry->rgba)).id) return false;
    if (!lam::pool_grow_array(p->pool,&p->meshes,&p->mesh_capacity,p->mesh_count+1,16)) return false;
    mesh->order=p->mesh_count;p->meshes[p->mesh_count++]=mesh;return true;
}

static bool scene3d_mesh(Scene3dBuild* build, DomElement* node, const Scene3dMatrix& world) {
    build->mesh_source=node;
    Scene3dProjection* p=&build->entry->projection;
    const char* geometry_ref=scene3d_text(node,"geometry"); const char* material_ref=scene3d_text(node,"material");
    Scene3dDefinition* gd=geometry_ref?scene3d_definition(build,nullptr,geometry_ref,"geometry"):nullptr;
    Scene3dDefinition* md=material_ref?scene3d_definition(build,nullptr,material_ref,"material"):nullptr;
    for (DomNode* child=node->first_child;child;child=child->next_sibling) if (child->is_element()) {
        DomElement* element=child->as_element();
        if (scene3d_tag(element,"geometry") && !gd && !geometry_ref) gd=scene3d_definition(build,element,nullptr,"geometry");
        else if (scene3d_tag(element,"material") && !md && !material_ref) md=scene3d_definition(build,element,nullptr,"material");
        else return scene3d_fail(build->entry,"mesh requires one geometry and one material");
    }
    int kind=scene3d_drawable_kind(node->local_name());
    Scene3dGeometry* geometry=scene3d_geometry(build,gd,(Scene3dTopology)kind); Scene3dMaterial* material=scene3d_material(build,md);
    if (!geometry || !material) return false;
    if(kind!=SCENE_TRIANGLES&&(material->lambert||material->standard||material->shader||material->depth_only||
        scene3d_present(node,"skeleton")||scene3d_present(node,"morph-weights")))
        return scene3d_fail(build->entry,"drawables require basic color without deformation");
    if(material->transmission&&(material->depth_only||scene3d_transparent(material,geometry))) return scene3d_fail(build->entry,"transmission requires color with opaque or masked coverage");
    float* matrices=nullptr; unsigned count=0;
    ItemReader instance_value=scene3d_value(node,"instances");
    if (!instance_value.isNull() && instance_value.isArray() && instance_value.asArray().length()>0 &&
        instance_value.asArray().get(0).isArray()) {
        ArrayReader instances=instance_value.asArray();
        if ((uint64_t)instances.length()>SCENE_INSTANCE_LIMIT) return scene3d_fail(build->entry,"instance quota exceeded");
        count=(unsigned)instances.length()*16;
        matrices=(float*)pool_alloc(p->pool,count*sizeof(float)); if (!matrices) return false;
        for (unsigned i=0;i<count/16;i++) {
            ItemReader instance=instances.get(i);
            if (!instance.isArray() || instance.asArray().length()!=16) return scene3d_fail(build->entry,"instance requires a 16-component matrix");
            for (unsigned c=0;c<16;c++) {
                if (!scene3d_scalar(instance.asArray().get(c),&matrices[i*16+c])) return scene3d_fail(build->entry,"invalid instance matrix");
            }
        }
    } else if (!scene3d_numbers(node,"instances",p->pool,&matrices,&count,SCENE_INSTANCE_LIMIT*16))
        return scene3d_fail(build->entry,"invalid instance data");
    Scene3dInstanceData data[SCENE_INSTANCE_DATA_LIMIT];unsigned data_count=0,instances=0;
    if (material->shader) {
        // SH3: `instance-count` draws instances the vertex shader places itself, beside optional matrices
        float requested;bool counted=scene3d_present(node,"instance-count");
        if (!scene3d_number(node,"instance-count",1,&requested) || requested<1 || requested>SCENE_INSTANCE_LIMIT ||
            floorf(requested)!=requested || (counted && count) || count%16)
            return scene3d_fail(build->entry,"instance-count must be an integer in 1..16384 without instance matrices");
        instances=count?count/16:(unsigned)requested;
        ItemReader authored=scene3d_value(node,"instance-data");
        if (!authored.isNull()) {
            if (!authored.isMap() || authored.asMap().size()>SCENE_INSTANCE_DATA_LIMIT)
                return scene3d_fail(build->entry,"instance-data must be a map of at most 6 attributes");
            // the iterator points at its reader, which must outlive it
            MapReader map=authored.asMap();MapReader::EntryIterator entries=map.entries();const char* name;ItemReader value;
            while (entries.next(&name,&value)) {
                Scene3dInstanceData* d=&data[data_count++];
                int64_t length=value.isArray()?value.asArray().length():-1;
                if (!scene3d_shader_name(name) || length<instances || length>4*(int64_t)instances || length%instances)
                    return scene3d_fail(build->entry,"instance data needs 1 to 4 numbers per instance under a GLSL name");
                float* values=(float*)pool_alloc(p->pool,length*sizeof(float));if (!values) return false;
                for (int64_t i=0;i<length;i++) if (!scene3d_scalar(value.asArray().get(i),&values[i]))
                    return scene3d_fail(build->entry,"invalid instance data value");
                str_copy(d->name,sizeof(d->name),name,strlen(name));d->values=values;d->components=(unsigned)(length/instances);
            }
        }
    } else if (node->has_attribute("instance-count") || !scene3d_value(node,"instance-count").isNull() ||
        !scene3d_value(node,"instance-data").isNull())
        return scene3d_fail(build->entry,"instance-count and instance-data require a shader material");
    unsigned present_uvs=(geometry->has_uv0?1u:0)|(geometry->has_uv1?2u:0);
    if(material->required_uvs&~present_uvs) return scene3d_fail(build->entry,"material requests an absent texture coordinate set");
    if(material->depth_only&&geometry->vertex_alpha&&!material->force_opaque)
        return scene3d_fail(build->entry,"depth-only material must be opaque");
    Scene3dMatrix identity=scene3d_identity();
    bool authored_matrices=count!=0;
    if (!count) { matrices=identity.v; count=16; }
    if (count%16) return scene3d_fail(build->entry,"instances require 16-component matrices");
    for (unsigned i=0;i<count;i+=16) {
        const float* m=matrices+i;
        float det=scene3d_determinant(m);
        if (!isfinite(det) || fabsf(det)<1e-10f || m[3]!=0 || m[7]!=0 || m[11]!=0 || m[15]!=1)
            return scene3d_fail(build->entry,"instance matrix must be nonsingular and affine");
        // batched opaque instances must validate the same composed transform as individual draws.
        Scene3dMatrix instance;memcpy(instance.v,m,sizeof(instance.v));
        Scene3dMatrix combined=scene3d_multiply(world,instance);
        for (float component:combined.v) if (!isfinite(component)) return scene3d_fail(build->entry,"instance world transform overflow");
        float combined_det=scene3d_determinant(combined.v);
        if (!isfinite(combined_det) || combined_det==0) return scene3d_fail(build->entry,"mesh normal transform is singular or overflows");
    }
    // the GPU places shader instances, so they stay one batch: neither sorted nor split by winding
    if (material->shader) return scene3d_mesh_record(build,geometry,material,world,authored_matrices?matrices:nullptr,instances,
        scene3d_determinant(world.v)<0,scene3d_identity(),data,data_count);
    // transparent instances need individual world bounds for global back-to-front ordering.
    // mirrored opaque instances share each winding batch so culling stays correct.
    if (count==16 || scene3d_transparent(material,geometry)) {
        for (unsigned i=0;i<count;i+=16) {
            Scene3dMatrix instance;memcpy(instance.v,matrices+i,sizeof(instance.v));
            Scene3dMatrix combined=scene3d_multiply(world,instance);
            if (!scene3d_mesh_record(build,geometry,material,combined,nullptr,1,scene3d_determinant(combined.v)<0,instance)) return false;
        }
    } else if(geometry->topology!=SCENE_TRIANGLES) {
        if(!scene3d_mesh_record(build,geometry,material,world,matrices,count/16,false)) return false;
    } else {
        float* batch=(float*)pool_alloc(p->pool,count*sizeof(float));if (!batch) return false;
        bool mirrored=scene3d_determinant(world.v)<0;
        for (unsigned sign=0;sign<2;sign++) {
            unsigned used=0;
            for (unsigned i=0;i<count;i+=16) if ((scene3d_determinant(matrices+i)<0)==(sign!=0)) {
                memcpy(batch+used*16,matrices+i,16*sizeof(float));used++;
            }
            if (used && !scene3d_mesh_record(build,geometry,material,world,batch,used,mirrored!=(sign!=0))) return false;
        }
    }
    return true;
}

static bool scene3d_project_dynamic(Scene3dBuild* build, DomElement* node, const Scene3dMatrix& parent,
        const Scene3dMatrix& world, bool visible) {
    Scene3dProjection* p=&build->entry->projection;
    if (scene3d_tag(node,"camera")) {
        const char* type=scene3d_text(node,"type"); float fov,near_plane,far_plane,aspect,view_height;
        bool orthographic=type && !strcmp(type,"orthographic"),infinite_far;
        if (!type || (!orthographic && strcmp(type,"perspective")) || !scene3d_animated_number(build->entry,node,"fov",50,&fov) ||
            !scene3d_animated_number(build->entry,node,"near",.1f,&near_plane) || !scene3d_animated_number(build->entry,node,"far",1000,&far_plane) ||
            !scene3d_animated_number(build->entry,node,"aspect",0,&aspect) ||
            !scene3d_animated_number(build->entry,node,"view_height",4,&view_height) ||
            !scene3d_flag(node,"infinite-far",false,&infinite_far) || (orthographic&&infinite_far) ||
            fov<=0 || fov>=180 || view_height<=0 || (orthographic?near_plane<0:near_plane<=0) || (!infinite_far&&far_plane<=near_plane) || aspect<0)
            return scene3d_fail(build->entry,"invalid scene camera");
        Scene3dVec target,up;
        if (!scene3d_vector(node,"target",{},&target) || !scene3d_vector(node,"up",{0,1,0},&up)) return scene3d_fail(build->entry,"invalid camera orientation");
        const char* id=scene3d_text(node,"id");
        if ((build->camera && id && !strcmp(build->camera,id)) || (!build->camera && !p->camera_found)) {
            p->camera_found=true; p->camera_world=world; p->camera_target=scene3d_point(parent,target);
            p->camera_up=scene3d_point(parent,up,0);
            p->camera_has_target=node->has_attribute("target"); p->fov=fov;p->near_plane=near_plane;p->far_plane=infinite_far?INFINITY:far_plane;p->aspect=aspect;
            p->orthographic=orthographic;p->view_height=view_height;
        }
    } else if (scene3d_tag(node,"light")) {
        const char* type=scene3d_text(node,"type"); float color[4],ground[4],intensity,distance,angle,penumbra;
        unsigned kind=!type?3:!strcmp(type,"directional")?0:!strcmp(type,"point")?1:!strcmp(type,"spot")?2:!strcmp(type,"hemisphere")?4:3;
        if ((kind==3 && (!type || strcmp(type,"ambient"))) ||
            !scene3d_animated_color(build->entry,node,color) || !scene3d_animated_number(build->entry,node,"intensity",1,&intensity) || intensity<0 ||
            !scene3d_color(node,"ground-color","#ffffff",ground) ||
            !scene3d_animated_number(build->entry,node,"distance",0,&distance) || distance<0 ||
            !scene3d_animated_number(build->entry,node,"angle",(float)M_PI/3,&angle) || angle<=0 || angle>(float)M_PI/2 ||
            !scene3d_animated_number(build->entry,node,"penumbra",0,&penumbra) || penumbra<0 || penumbra>1)
            return scene3d_fail(build->entry,"invalid light");
        // shadows (SP3: lights opt in); point-light cube shadows arrive in M5
        bool cast;float size,shadow[4],extent,cascades,coverage,split,transition;
        if (!scene3d_flag(node,"cast-shadow",false,&cast) ||
            !scene3d_number(node,"shadow-map-size",kind==0?2048:1024,&size) || !(size>=512 && size<=4096) || floorf(size)!=size ||
            !scene3d_number(node,"shadow-intensity",1,&shadow[0]) || !(shadow[0]>=0 && shadow[0]<=1) ||
            !scene3d_number(node,"shadow-bias",0,&shadow[1]) || !isfinite(shadow[1]) ||
            !scene3d_number(node,"shadow-normal-bias",0,&shadow[2]) || !isfinite(shadow[2]) ||
            !scene3d_animated_number(build->entry,node,"shadow-softness",1.5f,&shadow[3]) || !(shadow[3]>=0 && shadow[3]<=16) ||
            !scene3d_number(node,"shadow-extent",0,&extent) || !(extent>=0) || !isfinite(extent))
            return scene3d_fail(build->entry,"invalid light shadow");
        if(!scene3d_number(node,"shadow-cascades",1,&cascades)||cascades<1||cascades>4||floorf(cascades)!=cascades||
            !scene3d_number(node,"shadow-distance",500,&coverage)||coverage<=0||
            !scene3d_number(node,"shadow-split",.6f,&split)||split<0||split>1||
            !scene3d_number(node,"shadow-transition",.1f,&transition)||transition<0||transition>.25f||
            (kind!=0&&cascades!=1)) return scene3d_fail(build->entry,"invalid directional shadow cascades");
        if (cast && kind>2) return scene3d_fail(build->entry,"cast-shadow requires a directional, point or spot light");
        // SP5: cube maps stay within 1024 texels a face
        if (kind==1 && size>1024) return scene3d_fail(build->entry,"point-light shadow maps are at most 1024 texels");
        if (cast && build->layer!=SCENE_LAYER_SCENE) return scene3d_fail(build->entry,"environment lights cannot cast shadows");
        Scene3dLights* set=build->layer==SCENE_LAYER_SCENE?&p->lights:build->layer==SCENE_LAYER_ENVIRONMENT?&p->environment_lights:&p->unused_lights;
        Scene3dVec origin=scene3d_point(world,{});
        if (visible && kind==3) for (unsigned c=0;c<3;c++) set->ambient[c]+=color[c]*intensity;
        else if (visible && kind==4) {
            if (set->has_hemisphere) return scene3d_fail(build->entry,"hemisphere light quota exceeded");
            // as in Three.js the sky lies toward the light's position, straight up when it sits at the origin
            Scene3dVec up=origin;if (!scene3d_normalize(&up)) up={0,1,0};
            set->has_hemisphere=true;set->hemisphere[0]=up.x;set->hemisphere[1]=up.y;set->hemisphere[2]=up.z;
            for (unsigned c=0;c<3;c++) { set->sky[c]=color[c]*intensity;set->ground[c]=ground[c]*intensity; }
        } else if (visible) {
            if (set->count>=8) return scene3d_fail(build->entry,"light quota exceeded");
            unsigned i=set->count++;Scene3dVec aim={};
            if (kind!=1) {
                // directional and spot lights point from their position toward the target.
                Scene3dVec target; if (!scene3d_vector(node,"target",{},&target)) return scene3d_fail(build->entry,"invalid light target");
                aim=scene3d_point(parent,target);
                Scene3dVec direction=scene3d_sub(origin,aim);
                if (!scene3d_normalize(&direction)) return scene3d_fail(build->entry,"light has zero direction");
                set->directions[i][0]=direction.x;set->directions[i][1]=direction.y;set->directions[i][2]=direction.z;
            }
            set->light_positions[i][0]=origin.x;set->light_positions[i][1]=origin.y;set->light_positions[i][2]=origin.z;
            // a hard cone (penumbra 0) keeps smoothstep's edges strictly ordered.
            float outer=cosf(angle),inner=fmaxf(cosf(angle*(1-penumbra)),outer+1e-5f);
            float params[4]={(float)kind,distance,outer,inner};memcpy(set->light_params[i],params,sizeof(params));
            for (unsigned c=0;c<3;c++) set->lights[i][c]=color[c]*intensity;
            if (cast) {
                if (p->shadows.count+(unsigned)cascades>SCENE_SHADOW_LIMIT) return scene3d_fail(build->entry,"shadow light quota exceeded (cascades share four slots)");
                for(unsigned c=0;c<(unsigned)cascades;c++) {
                    Scene3dShadow* slot=&p->shadows.slots[p->shadows.count++];
                    *slot={i,kind,origin,aim,angle,distance,extent,(unsigned)size,{shadow[0],shadow[1],shadow[2],shadow[3]},{},{},{}};
                    slot->cascade=c;slot->cascades=(unsigned)cascades;slot->cascade_distance=coverage;slot->cascade_lambda=split;slot->cascade_transition=transition;
                }
            }
        }
    }
    return true;
}

// An image environment: a local Radiance HDR file decoded once per resolved path into an RGBA32F
// equirectangular texture that the entry keeps across re-projections (D7.2.4).
static bool scene3d_environment_image(Scene3dBuild* build, const char* source) {
    Scene3dEntry* entry=build->entry;DomDocument* document=build->root->doc;
    if (!document_dependency_admits(document,source)) return scene3d_fail(entry,"environment image is not an admitted dependency");
    Url* url=document->url?parse_url(document->url,source):nullptr;
    char* path=url && url->scheme==URL_SCHEME_FILE?url_to_local_path(url):nullptr;
    if (url) url_destroy(url);
    if (!path) return scene3d_fail(entry,"environment image requires a local file");
    if (entry->environment_image.id && entry->environment_path && !strcmp(entry->environment_path,path)) {
        mem_free(path);build->entry->projection.environment_image_hash=entry->environment_image_hash;return true;
    }
    char* data=nullptr;size_t size=0;HdrImage image;const char* error="environment image cannot be read";
    bool decoded=file_read_all_limit(path,MEM_CAT_IMAGE,(uint64_t)256<<20,&data,&size) &&
        hdr_read((const uint8_t*)data,size,4096,&image,&error);
    mem_free(data);
    if (!decoded) { mem_free(path);return scene3d_fail(entry,error); }
    // rows bottom first, so v grows upward as in Three.js's flipped upload
    float* rgba=(float*)mem_alloc((size_t)image.width*image.height*4*sizeof(float),MEM_CAT_IMAGE);
    for (uint32_t y=0;rgba && y<image.height;y++) for (uint32_t x=0;x<image.width;x++) {
        const float* in=image.pixels+((size_t)(image.height-1-y)*image.width+x)*3;float* out=rgba+((size_t)y*image.width+x)*4;
        out[0]=in[0];out[1]=in[1];out[2]=in[2];out[3]=1;
    }
    NativeGlResource texture=rgba?native_gl_data_texture(entry->graphics,image.width,image.height,true):NativeGlResource{};
    bool uploaded=texture.id && native_gl_data_texture_update(entry->graphics,texture,rgba,image.height);
    uint64_t hash=hashmap_xxhash3(image.pixels,(size_t)image.width*image.height*3*sizeof(float),image.width,image.height);
    mem_free(rgba);hdr_image_release(&image);
    if (!uploaded) { native_gl_release(entry->graphics,texture);mem_free(path);return scene3d_fail(entry,native_gl_diagnostic(entry->graphics)); }
    native_gl_release(entry->graphics,entry->environment_image);mem_free(entry->environment_path);
    entry->environment_image=texture;entry->environment_path=path;entry->environment_image_hash=hash;
    build->entry->projection.environment_image_hash=hash;
    return true;
}
static bool scene3d_lod_tag(DomElement* node) {
    return scene3d_tag(node,"group")||scene3d_tag(node,"lod")||scene3d_tag(node,"resources")||
        scene3d_drawable_kind(node->local_name())>=0||scene3d_tag(node,"geometry")||scene3d_tag(node,"material")||scene3d_tag(node,"texture");
}
static void scene3d_lod_prune(Scene3dEntry* entry) {
    auto** link=&entry->lod_choices;
    while(*link) {
        auto* choice=*link;
        if(choice->used) link=&choice->next;
        else {*link=choice->next;mem_free(choice);}
    }
}
static Scene3dLod* scene3d_project_lod(Scene3dBuild* build,DomElement* node,const Scene3dMatrix& world) {
    auto* p=&build->entry->projection;float* distances;unsigned count,children=0;
    for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) {
        if(!scene3d_tag(child->as_element(),"group")) {scene3d_fail(build->entry,"LOD levels must be groups");return nullptr;}
        children++;
    }
    float hysteresis;
    if(!scene3d_numbers(node,"distances",p->pool,&distances,&count,SCENE_LOD_LEVEL_LIMIT)||!count||count!=children||distances[0]!=0||
        !scene3d_number(node,"hysteresis",.1f,&hysteresis)||hysteresis<0||hysteresis>=.5f) {
        scene3d_fail(build->entry,"LOD requires 1..16 ordered distances starting at zero, matching groups, and hysteresis in [0,0.5)");return nullptr;
    }
    for(unsigned i=1;i<count;i++) if(distances[i]<=distances[i-1]) {scene3d_fail(build->entry,"LOD distances must strictly increase");return nullptr;}
    auto* lod=(Scene3dLod*)pool_calloc(p->pool,sizeof(Scene3dLod));
    if(!lod||!lam::pool_grow_array(p->pool,&p->lods,&p->lod_capacity,p->lod_count+1,8)) return nullptr;
    lod->source=dom_node_ref(node);lod->world=world;lod->count=count;lod->hysteresis=hysteresis;
    memcpy(lod->distances,distances,count*sizeof(float));
    auto* entry=build->entry;auto* choice=entry->lod_choices;
    while(choice&&dom_node_ref_identity_compare(choice->source,lod->source)) choice=choice->next;
    if(!choice) {
        choice=(Scene3dLodChoice*)mem_calloc(1,sizeof(Scene3dLodChoice),MEM_CAT_RENDER);if(!choice) return nullptr;
        choice->source=lod->source;choice->next=entry->lod_choices;entry->lod_choices=choice;
    }
    uint64_t key=hashmap_xxhash3(distances,count*sizeof(float),count,0);key=hash_fnv1a_64_extend(key,&hysteresis,sizeof(hysteresis));
    if(choice->key!=key) {choice->initialized=false;choice->key=key;}
    choice->used=true;lod->choice=choice;p->lods[p->lod_count++]=lod;return lod;
}
static bool scene3d_project_node(Scene3dBuild* build, DomElement* node, const Scene3dMatrix& parent, bool inherited_visible) {
    if(build->lod&&!scene3d_lod_tag(node)) return scene3d_fail(build->entry,"LOD levels accept geometric content only");
    if(scene3d_tag(node,"fog")) {
        auto* fog=&build->entry->projection.fog;const char* type=scene3d_text(node,"type");float color[4];
        if(node->parent!=build->root||fog->declared||node->first_child) return scene3d_fail(build->entry,"one leaf fog belongs directly to the scene");
        fog->declared=true;fog->params[0]=type&&!strcmp(type,"linear")?1:type&&!strcmp(type,"exp2")?2:type&&!strcmp(type,"height")?3:-1;
        if(fog->params[0]<0||!scene3d_color(node,"color","#d5dedb",color)||
            !scene3d_number(node,"near",0,&fog->params[1])||!scene3d_number(node,"far",1000,&fog->params[2])||
            !scene3d_number(node,"density",.002f,&fog->params[3])||!scene3d_number(node,"height",0,&fog->height[0])||
            !scene3d_number(node,"falloff",.1f,&fog->height[1])||fog->params[1]<0||fog->params[2]<=fog->params[1]||
            fog->params[3]<0||fog->height[1]<0) return scene3d_fail(build->entry,"invalid fog type, color or range");
        memcpy(fog->color,color,3*sizeof(float));return true;
    }
    if (scene3d_tag(node,"postprocessing")) {
        if (node->parent!=build->root) return scene3d_fail(build->entry,"postprocessing belongs directly to the scene");
        return true;
    }
    // simulations are built before projection (scene3d_build_simulations) and draw nothing themselves
    if(scene3d_tag(node,"physics")||scene3d_tag(node,"joint")||scene3d_tag(node,"ragdoll")||scene3d_tag(node,"collider")||scene3d_tag(node,"collision-geometry")||scene3d_tag(node,"simulation")) return true;
    if (node!=build->root && scene3d_tag(node,"scene3d")) return scene3d_fail(build->entry,"nested scene viewport is not a scene object");
    Scene3dMatrix local; bool visible;
    if (!scene3d_local(node,&local,build->entry->animation) || !scene3d_animated_flag(build->entry->animation,node,&visible)) return scene3d_fail(build->entry,"invalid object transform or visibility");
    Scene3dMatrix world=scene3d_multiply(parent,local); visible=visible&&inherited_visible;
    scene3d_physics_pose(build->entry->physics,node,&world);
    for (float component:world.v) if (!isfinite(component)) return scene3d_fail(build->entry,"world transform overflow");
    // resources and objects have distinct child vocabularies; ignored children must never look valid.
    bool rig=scene3d_tag(node,"skeleton")||scene3d_tag(node,"bone"),environment=scene3d_tag(node,"environment");
    bool is_lod=scene3d_tag(node,"lod");
    bool branch=scene3d_tag(node,"scene3d") || scene3d_tag(node,"group") || is_lod || scene3d_tag(node,"rigid-body") || scene3d_tag(node,"resources")||rig||environment;
    if (!branch && scene3d_drawable_kind(node->local_name())<0 && node->first_child)
        return scene3d_fail(build->entry,"invalid scene child");
    Scene3dProjection* p=&build->entry->projection;
    unsigned char layer=build->layer;
    if (environment) {
        // an environment is a scene of its own: drawn only into its capture, never into the view
        if (layer!=SCENE_LAYER_SCENE) return scene3d_fail(build->entry,"environments cannot nest");
        const char* id=scene3d_text(node,"id");float background[4],near_plane,far_plane;
        if (!scene3d_color(node,"background","#000000",background) || !scene3d_number(node,"near",.1f,&near_plane) ||
            !scene3d_number(node,"far",100,&far_plane) || !(near_plane>0) || !(far_plane>near_plane))
            return scene3d_fail(build->entry,"invalid environment background or clip planes");
        // an image environment takes its light from `src` instead of scene content
        const char* source=scene3d_text(node,"src");
        if (source && (!*source || node->first_child)) return scene3d_fail(build->entry,"an image environment names a file and holds no content");
        build->layer=SCENE_LAYER_UNUSED;
        if (id && build->environment && !strcmp(id,build->environment)) {
            build->layer=SCENE_LAYER_ENVIRONMENT;p->environment=dom_node_ref(node);
            memcpy(p->environment_background,background,sizeof(background));p->environment_near=near_plane;p->environment_far=far_plane;
            p->environment_imaged=source!=nullptr;
            if (source && !scene3d_environment_image(build,source)) return false;
        }
    }
    if (scene3d_tag(node,"camera") && layer!=SCENE_LAYER_SCENE) return scene3d_fail(build->entry,"environments cannot hold cameras");
    if (scene3d_tag(node,"camera") || scene3d_tag(node,"light")) {
        if(!lam::pool_grow_array(p->pool,&p->dynamic_objects,&p->dynamic_capacity,p->dynamic_count+1,8)) return false;
        p->dynamic_objects[p->dynamic_count++]=dom_node_ref(node);
        if(!scene3d_project_dynamic(build,node,parent,world,visible)) return false;
    } else if (scene3d_drawable_kind(node->local_name())>=0) {
        // invisible objects still validate their geometry/material data.
        if (!scene3d_mesh(build,node,world)) return false;
    } else if (!branch)
        return scene3d_fail(build->entry,"unknown scene element");
    Scene3dLod* lod=is_lod?scene3d_project_lod(build,node,world):nullptr;
    if(is_lod&&!lod) return false;
    unsigned level=0;Scene3dLodBranch* parent_lod=build->lod;
    if (branch)
        for (DomNode* child=node->first_child;child;child=child->next_sibling) if (child->is_element()) {
            DomElement* element=child->as_element();
            if(lod) {
                auto* selected=(Scene3dLodBranch*)pool_alloc(p->pool,sizeof(Scene3dLodBranch));if(!selected) return false;
                *selected={lod,level++,parent_lod};build->lod=selected;
            }
            if(build->lod&&!scene3d_lod_tag(element)) return scene3d_fail(build->entry,"LOD levels accept geometric content only");
            if(rig&&!scene3d_tag(element,"bone")) return scene3d_fail(build->entry,"skeletons and bones accept bone children");
            if (scene3d_tag(node,"resources") && !scene3d_tag(element,"geometry") && !scene3d_tag(element,"collision-geometry") &&
                !scene3d_tag(element,"material") && !scene3d_tag(element,"texture") && !scene3d_tag(element,"environment"))
                return scene3d_fail(build->entry,"resources accept geometry, collision-geometry, material, texture and environment definitions");
            if (scene3d_tag(element,"geometry")) { if (!scene3d_geometry_source(build,scene3d_definition(build,element,nullptr,"geometry"))) return false; }
            else if (scene3d_tag(element,"material")) { if (!scene3d_material(build,scene3d_definition(build,element,nullptr,"material"))) return false; }
            else if (scene3d_tag(element,"texture")) continue;
            else if (scene3d_tag(element,"animation-clip")||scene3d_tag(element,"motion-controller")) continue;
            else if (!scene3d_project_node(build,element,world,visible)) return false;
        }
    build->lod=parent_lod;build->layer=layer;
    return true;
}

static void scene3d_projection_destroy(Scene3dEntry* entry) {
    Scene3dProjection* p=&entry->projection;
    for (int i=p->resource_count-1;i>=0;i--) native_gl_release(entry->graphics,p->resources[i]);
    mem_pool_destroy(p->pool); *p={};
    // simulations outlive the projection, but its uniforms were allocated from the projection's pool
    for (unsigned i=0;i<entry->simulation_count;i++) { entry->simulations[i]->uniforms=nullptr;entry->simulations[i]->uniform_count=0; }
}
bool scene3d_world_pose(DomElement* root,DomElement* node,Scene3dAnimationState* animation,
        Scene3dPoseOverrideFn override_pose,void* owner,Scene3dMatrix* world,bool* visible,bool authored) {
    if(!root||!node||!world||!visible||root->doc!=node->doc)return false;
    DomElement* ancestors[65];unsigned count=0;
    for(DomNode* current=node;current&&current->is_element()&&count<65;current=current->parent) {
        ancestors[count++]=current->as_element();if(current==root) break;
    }
    if(!count||ancestors[count-1]!=root) return false;
    *world=scene3d_identity();*visible=true;
    while(count) {
        DomElement* ancestor=ancestors[--count];Scene3dMatrix local;bool shown;
        if(!scene3d_local(ancestor,&local,authored?nullptr:animation)||!scene3d_animated_flag(animation,ancestor,&shown)) return false;
        *world=scene3d_multiply(*world,local);*visible=*visible&&shown;
        if(!authored&&override_pose)override_pose(owner,ancestor,world);
    }
    for(float component:world->v) if(!isfinite(component)) return false;
    return true;
}
static bool scene3d_override_physics_pose(void* owner,DomElement* node,Scene3dMatrix* world) {
    return scene3d_physics_pose((Scene3dPhysics*)owner,node,world);
}
static bool scene3d_world(Scene3dEntry* entry,DomElement* node,Scene3dMatrix* world,bool* visible,bool authored) {
    DomNode* root=dom_node_ref_validate(node->doc,entry->root);
    return root&&root->is_element()&&scene3d_world_pose(root->as_element(),node,entry->animation,
        scene3d_override_physics_pose,entry->physics,world,visible,authored);
}
static bool scene3d_refresh_dynamic(Scene3dEntry* entry,DomElement* root,bool cameras_only) {
    Scene3dProjection* p=&entry->projection;
    p->camera_found=false;
    if (!cameras_only) {p->lights={};p->environment_lights={};p->unused_lights={};p->shadows={};}
    Scene3dBuild build={};build.entry=entry;build.root=root;build.camera=scene3d_text(root,"camera");
    DomNode* lighting=p->environment.address?dom_node_ref_validate(root->doc,p->environment):nullptr;
    for(int i=0;i<p->dynamic_count;i++) {
        DomNode* source=dom_node_ref_validate(root->doc,p->dynamic_objects[i]);
        if(!source||!source->is_element()||!source->parent||!source->parent->is_element()) return false;
        if (cameras_only && !scene3d_tag(source->as_element(),"camera")) continue;
        // a light keeps the layer of the environment (if any) it sits in
        build.layer=SCENE_LAYER_SCENE;
        for(DomNode* ancestor=source->parent;ancestor&&ancestor!=root;ancestor=ancestor->parent)
            if(ancestor->is_element()&&scene3d_tag(ancestor->as_element(),"environment")) {
                build.layer=ancestor==lighting?SCENE_LAYER_ENVIRONMENT:SCENE_LAYER_UNUSED;break;
            }
        Scene3dMatrix parent,world;bool visible,unused;
        if(!scene3d_world(entry,source->parent->as_element(),&parent,&unused)||
            !scene3d_world(entry,source->as_element(),&world,&visible)||
            !scene3d_project_dynamic(&build,source->as_element(),parent,world,visible)) return false;
    }
    return p->camera_found;
}
// SH2: each numeric uniform restarts from its authored value, then takes a playing track's `uniforms.<name>`
static bool scene3d_refresh_uniforms(Scene3dEntry* entry, DomElement* node, Scene3dShaderUniform* uniforms, unsigned count) {
    for (unsigned u=0;u<count;u++) {
        Scene3dShaderUniform* uniform=&uniforms[u];
        if (!uniform->values) continue; // samplers carry no numbers
        memcpy(uniform->values,uniform->authored,uniform->length*sizeof(float));
        char property[32];snprintf(property,sizeof(property),"uniforms.%s",uniform->name);AnimationValue value={};
        if (scene3d_animation_value(entry->animation,node,property,&value)) {
            if (value.count!=uniform->length) return false;
            for (unsigned c=0;c<value.count;c++) uniform->values[c]=(float)value.numbers[c];
        }
    }
    return true;
}
static bool scene3d_refresh_pose(Scene3dEntry* entry,DomElement* root) {
    Scene3dProjection* p=&entry->projection;
    for(int i=0;i<p->lod_count;i++) {
        Scene3dLod* lod=p->lods[i];DomNode* node=dom_node_ref_validate(root->doc,lod->source);bool shown;
        if(!node||!node->is_element()||!scene3d_world(entry,node->as_element(),&lod->world,&shown)) return false;
    }
    // poses update retained draw records; geometry, indices, textures and VAOs keep their resource identities.
    for(int i=0;i<p->mesh_count;i++) {
        auto* mesh=p->meshes[i];DomNode* node=dom_node_ref_validate(root->doc,mesh->source);Scene3dMatrix world;
        if(!node||!node->is_element()||!scene3d_world(entry,node->as_element(),&world,&mesh->visible)) return false;
        mesh->world=scene3d_multiply(world,mesh->local_instance);
        float determinant=scene3d_determinant(mesh->world.v);if(!isfinite(determinant)||determinant==0) return false;
        // shader-expanded billboards and ribbons keep camera-facing CCW winding under mirrored transforms.
        mesh->clockwise=mesh->geometry->topology==SCENE_TRIANGLES&&(mesh->instance_mirrored!=(determinant<0));
        if(!scene3d_drawable_values(entry,node->as_element(),mesh)) return false;
        if(mesh->bone_count) {
            Scene3dMatrix inverse_mesh;if(!scene3d_inverse_affine(mesh->world,&inverse_mesh)) return false;
            for(unsigned bone=0;bone<mesh->bone_count;bone++) {
                DomNode* source=dom_node_ref_validate(root->doc,mesh->bones[bone]);Scene3dMatrix bone_world;bool visible;
                if(!source||!source->is_element()||!scene3d_world(entry,source->as_element(),&bone_world,&visible)) return false;
                // attached skinning converts the current bone world pose back into this mesh's local frame.
                mesh->bone_matrices[bone]=scene3d_multiply(scene3d_multiply(inverse_mesh,bone_world),
                    mesh->authored_bind[bone]?mesh->inverse_bind[bone]:scene3d_multiply(mesh->inverse_bind[bone],mesh->bind_matrix));
            }
        }
        if(mesh->geometry->morph_count) {
            AnimationValue weights={};
            if(scene3d_animation_value(entry->animation,node->as_element(),"morph-weights",&weights)) {
                if(weights.count!=mesh->geometry->morph_count) return false;
                for(unsigned target=0;target<weights.count;target++) mesh->morph_weights[target]=weights.numbers[target];
            } else {
                for(unsigned target=0;target<mesh->geometry->morph_count;target++) mesh->morph_weights[target]=mesh->morph_base_weights[target];
            }
        }
        auto* material=mesh->material;DomNode* source=dom_node_ref_validate(root->doc,material->source);
        if(source&&source->is_element()&&material->shader&&
            !scene3d_refresh_uniforms(entry,source->as_element(),material->shader->uniforms,material->shader->uniform_count)) return false;
        if(!source||!source->is_element()||!scene3d_material_values(entry,source->as_element(),material)) return false;
        if(material->transmission&&scene3d_transparent(mesh)) return scene3d_fail(entry,"transmission requires opaque or masked coverage");
    }
    for(unsigned i=0;i<entry->simulation_count;i++) {
        Scene3dSimulation* simulation=entry->simulations[i];
        DomNode* source=dom_node_ref_validate(root->doc,simulation->source);
        if(!source||!source->is_element()||
            !scene3d_refresh_uniforms(entry,source->as_element(),simulation->uniforms,simulation->uniform_count)) return false;
    }
    for (unsigned i=0;i<p->effect_count;i++) {
        Scene3dEffect* effect=&p->effects[i];DomNode* source=dom_node_ref_validate(root->doc,effect->source);
        if (!source || !source->is_element() ||
            !scene3d_refresh_uniforms(entry,source->as_element(),effect->uniforms,effect->uniform_count)) return false;
    }
    return scene3d_refresh_dynamic(entry,root,false);
}
static void scene3d_entry_destroy(Scene3dEntry* entry) {
    // stop the one coordinator before removing its services, including document teardown.
    scene3d_runtime_pause(entry->runtime,true);
    scene3d_physics_destroy(entry->physics);
    scene3d_animation_destroy(entry->animation);
    scene3d_runtime_destroy(entry->runtime);
    for (unsigned i=0;i<entry->simulation_count;i++) scene3d_simulation_release(entry->graphics,entry->simulations[i]);
    scene3d_projection_destroy(entry); scene3d_retire_snapshot(entry);
    for(auto* choice=entry->lod_choices;choice;choice=choice->next) choice->used=false;
    scene3d_lod_prune(entry);
    native_gl_destroy(entry->graphics);mem_free(entry->mapped_programs);mem_free(entry->environment_path);mem_free(entry);
}
static void scene3d_registry_destroy(DomDocumentResourceData* data) {
    Scene3dRegistry* registry=(Scene3dRegistry*)data;
    while (registry->entries) { Scene3dEntry* entry=registry->entries; registry->entries=entry->next; scene3d_entry_destroy(entry); }
    registry->document->services.scene3d_registry=nullptr; mem_free(registry);
}
static Scene3dRegistry* scene3d_registry(DomDocument* document, bool create) {
    if (!document) return nullptr;
    Scene3dRegistry* registry=(Scene3dRegistry*)document->services.scene3d_registry;
    if (!registry && create) {
        registry=(Scene3dRegistry*)mem_calloc(1,sizeof(Scene3dRegistry),MEM_CAT_RENDER);
        if (!registry) return nullptr; registry->document=document;
        if (!dom_document_add_resource(document,registry,scene3d_registry_destroy)) { mem_free(registry);return nullptr; }
        document->services.scene3d_registry=registry;
    }
    return registry;
}
void scene3d_collect(DomDocument* document) {
    Scene3dRegistry* registry=scene3d_registry(document,false); if (!registry) return;
    Scene3dEntry** link=&registry->entries;
    while (*link) {
        Scene3dEntry* entry=*link; DomNode* node=dom_node_ref_validate(document,entry->root);
        bool attached=false;
        for (DomNode* parent=node;parent;parent=parent->parent) if (parent==document->root) { attached=true;break; }
        if (!attached) { *link=entry->next; scene3d_entry_destroy(entry); registry->count--; }
        else link=&entry->next;
    }
}
// a template re-render replaces the scene's DOM subtree; the entry, its GL
// context, its playback and its physics world follow the structurally corresponding viewport
void scene3d_follow_rebuild(DomDocument* document, DomNode* old_root, DomNode* new_root) {
    Scene3dRegistry* registry=scene3d_registry(document,false);
    if (!registry || !old_root || !new_root || old_root==new_root) return;
    for (Scene3dEntry* entry=registry->entries;entry;entry=entry->next) {
        DomNode* owner=dom_node_ref_validate(document,entry->root);
        DomNode* replacement=owner?view_state_corresponding_node(owner,old_root,new_root):nullptr;
        if (!replacement || !replacement->is_element() || !scene3d_tag(replacement->as_element(),"scene3d")) continue;
        entry->root=dom_node_ref(replacement); entry->mutation_epoch=UINT64_MAX;
        scene3d_runtime_follow_rebuild(entry->runtime,old_root,new_root);
        for(auto* choice=entry->lod_choices;choice;choice=choice->next) {
            DomNode* source=dom_node_ref_validate(document,choice->source);
            choice->source=dom_node_ref(source?view_state_corresponding_node(source,old_root,new_root):nullptr);
        }
        scene3d_animation_follow_rebuild(entry->animation,old_root,new_root);
        scene3d_physics_follow_rebuild(entry->physics,old_root,new_root);
    }
}
void scene3d_release_subtree(DomNode* root) {
    // only elements can contain scene instances and carry document ownership.
    if (!root || !root->is_element()) return;
    DomDocument* doc = root->as_element()->doc;
    if (!doc) return;
    Scene3dRegistry* registry=scene3d_registry(doc,false); if (!registry) return;
    Scene3dEntry** link=&registry->entries;
    while (*link) {
        Scene3dEntry* entry=*link;DomNode* node=dom_node_ref_validate(doc,entry->root);bool contained=false;
        for (DomNode* parent=node;parent;parent=parent->parent) if (parent==root) { contained=true;break; }
        if (contained) { *link=entry->next;scene3d_entry_destroy(entry);registry->count--; }
        else link=&entry->next;
    }
}
static Scene3dEntry* scene3d_entry(DomElement* root, bool create) {
    if (!root || !scene3d_tag(root,"scene3d")) return nullptr;
    Scene3dRegistry* registry=scene3d_registry(root->doc,create); if (!registry) return nullptr;
    DomNodeRef ref=dom_node_ref(root);
    for (Scene3dEntry* entry=registry->entries;entry;entry=entry->next)
        if (entry->root.address==ref.address && entry->root.expected_id==ref.expected_id) return entry;
    if (!create) return nullptr;
    if (registry->count>=8) { log_error("scene3d registry: viewport quota exceeded");return nullptr; }
    Scene3dEntry* entry=(Scene3dEntry*)mem_calloc(1,sizeof(Scene3dEntry),MEM_CAT_RENDER); if (!entry) return nullptr;
    entry->runtime=scene3d_runtime_create(root);
    if(!entry->runtime) {mem_free(entry);return nullptr;}
    entry->root=ref; entry->mutation_epoch=UINT64_MAX; entry->next=registry->entries; registry->entries=entry;registry->count++;return entry;
}
const char* scene3d_diagnostic(DomElement* root) {
    Scene3dEntry* e=scene3d_entry(root,false);if (e) return e->diagnostic;
    Scene3dRegistry* registry=root?scene3d_registry(root->doc,false):nullptr;
    return registry && registry->count>=8?"scene viewport quota exceeded":"scene3d has no rendered projection";
}
Scene3dAnimationState* scene3d_animations(DomElement* root) {
    Scene3dEntry* entry=scene3d_entry(root,false);return entry?entry->animation:nullptr;
}
static bool scene3d_prepare_physics(Scene3dEntry* entry,DomElement* root,bool automatic) {
    auto refreshed=scene3d_physics_refresh(entry->physics,root,entry->diagnostic,sizeof(entry->diagnostic));
    // invalid compatible edits keep the previous owned world; only structural edits replace it.
    if(refreshed==SCENE3D_PHYSICS_REJECT)return false;
    if(refreshed==SCENE3D_PHYSICS_REBUILD) {
        scene3d_physics_destroy(entry->physics);
        entry->physics=nullptr;
        entry->physics=scene3d_physics_create(root,entry->diagnostic,sizeof(entry->diagnostic),automatic,entry->runtime);
    }
    return entry->physics!=nullptr;
}
static bool scene3d_prepare_animation(Scene3dEntry* entry,DomElement* root) {
    if(!scene3d_animation_matches(entry->animation,root)) {
        scene3d_animation_destroy(entry->animation);
        entry->animation=nullptr;
        entry->animation=scene3d_animation_create(root,entry->diagnostic,sizeof(entry->diagnostic),entry->runtime);
    }
    return entry->animation&&scene3d_physics_sample(entry->physics,entry->diagnostic,sizeof(entry->diagnostic));
}
Item scene3d_physics_control(DomElement* root,Item request) {
    RootFrame roots(1);Rooted<Item> command(roots,request);
    Scene3dEntry* entry=scene3d_entry(root,true);if(!entry)return ItemNull;
    if(!scene3d_prepare_physics(entry,root,true)||!scene3d_prepare_animation(entry,root)) return ItemError;
    return scene3d_physics_operate(entry->physics,command.get());
}
Scene3dRuntime* scene3d_runtime(DomElement* root) {
    auto* entry=scene3d_entry(root,false);return entry?entry->runtime:nullptr;
}
bool scene3d_seek(DomElement* root,size_t ticks) {
    auto* entry=scene3d_entry(root,true);if(!entry||ticks>100000||!scene3d_prepare_physics(entry,root,true)||
        !scene3d_prepare_animation(entry,root)||!scene3d_runtime_pause(entry->runtime,true))return false;
    Scene3dRuntimeStats stats={};scene3d_runtime_stats(entry->runtime,&stats);
    if(scene3d_physics_has_ragdolls(entry->physics)) {
        if(!scene3d_physics_replay(entry->physics,ticks,&entry->animation))return false;
    }else if(!scene3d_physics_seek(entry->physics,ticks)||!scene3d_animation_seek(entry->animation,ticks*stats.timestep))return false;
    return scene3d_physics_sample(entry->physics,entry->diagnostic,sizeof(entry->diagnostic))&&scene3d_runtime_reset_clock(entry->runtime,ticks);
}
Item scene3d_control(DomElement* root,Item request) {
    // preparing animation can queue GC-allocating events; retain the command across that handoff (D5.3.3).
    RootFrame roots(1);Rooted<Item> command(roots,request);
    auto* entry=scene3d_entry(root,true);
    if(!entry||!scene3d_prepare_physics(entry,root,true)||!scene3d_prepare_animation(entry,root)) return ItemError;
    auto operation=ItemReader(command.get().to_const()).asMap().get("operation");
    const char* op=operation.isString()?operation.asString()->chars:operation.isSymbol()?operation.asSymbol()->chars:nullptr;
    if(op&&(!strcmp(op,"seek")||!strcmp(op,"reset"))) {
        Scene3dRuntimeStats stats={};scene3d_runtime_stats(entry->runtime,&stats);
        double seconds=0;
        bool valid=strcmp(op,"seek")||physics_scene_real(ItemReader(command.get().to_const()).asMap().get("seconds"),NAN,&seconds);
        double ticks=seconds/stats.timestep,selected=round(ticks);
        if(!valid||!isfinite(ticks)||ticks<0||ticks>100000||fabs(ticks-selected)>1e-8)
            return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,"scene3d seek: requires a fixed-tick boundary within 100000 ticks",nullptr));
        if(!scene3d_seek(root,(size_t)selected))return ItemError; // INT_CAST_OK: validated bounded whole tick count
        // return the selected state directly; command readers consume authored maps, not result VMaps.
        return scene3d_runtime_snapshot(entry->runtime);
    }
    // an explicit scene play starts its prepared services, including an authored paused physics world.
    if(op&&!strcmp(op,"play")&&(!scene3d_physics_play(entry->physics,true)||!scene3d_animation_automatic(entry->animation,true))) return ItemError;
    return scene3d_runtime_operate(entry->runtime,command.get());
}
Scene3dPhysics* scene3d_physics(DomElement* root) {
    Scene3dEntry* entry=scene3d_entry(root,false);return entry?entry->physics:nullptr;
}
bool scene3d_stats(DomElement* root, Scene3dStats* stats) {
    Scene3dEntry* e=scene3d_entry(root,false); if (!e || !stats) return false;
    *stats=e->stats; native_gl_stats(e->graphics,&stats->graphics);
    scene3d_runtime_stats(e->runtime,&stats->runtime);
    PoolStats pool={};pool_get_detailed_stats(e->projection.pool,&pool);
    stats->projection_bytes=pool.live_bytes;stats->projection_reserved_bytes=pool.reserved_bytes;
    stats->snapshot_bytes=e->snapshot?(uint64_t)e->snapshot->pitch*e->snapshot->height:0;
    stats->lods=(unsigned)e->projection.lod_count;stats->lod_state_bytes=0;
    for(auto* choice=e->lod_choices;choice;choice=choice->next) stats->lod_state_bytes+=sizeof(Scene3dLodChoice);
    return true;
}
void scene3d_context_lost(DomElement* root) {
    Scene3dEntry* e=scene3d_entry(root,false); if (!e) return;
    native_gl_lose(e->graphics); scene3d_projection_destroy(e); native_gl_destroy(e->graphics);e->graphics=nullptr;
    for (unsigned i=0;i<SCENE_PROGRAM_VARIANTS;i++) { e->programs[i]={};e->program_frame[i]=0; }
    mem_free(e->mapped_programs);e->mapped_programs=nullptr;e->mapped_count=e->mapped_capacity=0;
    e->target={};e->hdr_target={};for (auto& variants:e->output_programs) for (auto& program:variants) program={};e->white={};e->dfg={};scene3d_retire_snapshot(e);e->mutation_epoch=UINT64_MAX;
    e->environment_atlas={};e->environment_pingpong={};e->environment_filter={};e->environment_key=0;
    e->environment_image={};e->environment_unwrap={};mem_free(e->environment_path);e->environment_path=nullptr;e->environment_image_hash=0;
    e->shadow_programs[0]={};e->shadow_programs[1]={};e->shadow_blank={};
    for (unsigned s=0;s<SCENE_SHADOW_LIMIT;s++) { e->shadow_maps[s]={};e->shadow_sizes[s]=0;e->shadow_cubes[s]=false;e->shadow_keys[s]=0; }
    e->shader_program_count=0;
    for (auto* group:{e->post_linear,e->post_blur,e->post_display,e->post_programs})
        for (unsigned i=0;i<(group==e->post_programs?4u:2u);i++) group[i]={};
    e->post_width=e->post_height=e->post_needs=0;
    e->transmission_target={};e->transmission_copy={};e->transmission_width=e->transmission_height=0;

    // simulation state lived on the lost context; the next projection restarts from the initial records
    for (unsigned i=0;i<e->simulation_count;i++) scene3d_simulation_release(nullptr,e->simulations[i]);
    e->simulation_count=0;e->fullscreen_buffer={};e->fullscreen_triangle={};
    for (NativeGlResource& plane:e->environment_planes) plane={};
}

// The vertex stage, after the `#version` line and the variant's defines (SHADOWS adds the
// shadow-map coordinates); the depth programs of shadow passes use it too.
static const char* scene3d_vertex_body = R"GLSL(
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 color;
layout(location=4) in mat4 instance;
layout(location=8) in vec4 skin_indices;
layout(location=9) in vec4 skin_weights;
layout(location=10) in vec3 morph_position0;
layout(location=11) in vec3 morph_position1;
layout(location=12) in vec3 morph_normal0;
layout(location=13) in vec3 morph_normal1;
layout(location=14) in vec4 tangent;
layout(location=15) in vec2 uv1;
uniform mat4 model, projection;
uniform sampler2D palette;
uniform vec2 morph_weights;
uniform float skinned, colored;
out vec3 world_normal, world_position;
#ifdef TRANSMISSION
out vec3 transmission_scale;
#endif
out vec4 vertex_color;
out vec3 world_tangent, world_bitangent; // zero when the mesh has no tangents
#if DRAWABLE_KIND > 0 && DRAWABLE_KIND == POINT_KIND
sample out vec2 texture_uv,texture_uv1;
sample out vec2 point_coord;
#else
out vec2 texture_uv,texture_uv1;
#endif
#ifdef SHADOWS
// each casting light's map coordinates, from the position pushed along the normal by its normal bias
uniform mat4 shadow_matrices[4];
uniform vec4 shadow_params[4]; // intensity, bias, normal bias, softness
uniform float back_side;       // a back-side material's normals face inward (Three.js FLIP_SIDED), and so does its normal bias
out vec4 shadow_coords[4];
#endif
// a bone matrix is one palette row: four texels, one per column
mat4 bone(float index) {
    int row = int(index);
    return mat4(texelFetch(palette, ivec2(0, row), 0), texelFetch(palette, ivec2(1, row), 0),
        texelFetch(palette, ivec2(2, row), 0), texelFetch(palette, ivec2(3, row), 0));
}
)GLSL";
static const char* scene3d_vertex_main = R"GLSL(
void main() {
    mat4 world = model * instance;
#if DRAWABLE_KIND > 0
    drawable_vertex(world);return;
#else
#ifdef TRANSMISSION
    transmission_scale=vec3(length(world[0].xyz),length(world[1].xyz),length(world[2].xyz));
#endif
    vec3 vertex = position + morph_position0*morph_weights.x + morph_position1*morph_weights.y;
    vec3 posed_normal = normal + morph_normal0*morph_weights.x + morph_normal1*morph_weights.y;
    vec3 posed_tangent = tangent.xyz;
    if(skinned>0.5) {
        mat4 skin = bone(skin_indices.x)*skin_weights.x+bone(skin_indices.y)*skin_weights.y+
            bone(skin_indices.z)*skin_weights.z+bone(skin_indices.w)*skin_weights.w;
        vertex=(skin*vec4(vertex,1.0)).xyz;posed_normal=mat3(skin)*posed_normal;posed_tangent=mat3(skin)*posed_tangent;
    }
    outdoor_vertex(vertex,posed_normal,posed_tangent,world);
    world_normal = transpose(inverse(mat3(world))) * posed_normal;
    // the tangent follows the surface (not the inverse transpose); the bitangent completes the frame
    vec3 t = mat3(world) * posed_tangent;
    world_tangent = dot(t,t) > 0.0 ? normalize(t) : vec3(0.0);
    world_bitangent = dot(t,t) > 0.0 ? normalize(cross(normalize(world_normal), world_tangent) * tangent.w) : vec3(0.0);
    vertex_color = colored>0.5?color:vec4(1); texture_uv = uv; texture_uv1 = uv1;
    vec4 placed = world * vec4(vertex,1.0); world_position = placed.xyz;
#ifdef SHADOWS
    vec3 shadow_normal = normalize(world_normal) * (back_side > 0.5 ? -1.0 : 1.0);
    for (int i=0; i<4; i++) shadow_coords[i] = shadow_matrices[i] * vec4(placed.xyz + shadow_normal*shadow_params[i].z, 1.0);
#endif
    gl_Position = projection * placed;
#endif
}
)GLSL";
static void scene3d_append_drawable_defines(StrBuf* source,unsigned kind) {
    strbuf_append_format(source,"#define DRAWABLE_KIND %u\n#define POINT_KIND %u\n#define SPRITE_KIND %u\n",kind,(unsigned)SCENE_POINTS,(unsigned)SCENE_SPRITE);
}
static void scene3d_append_vertex_source(StrBuf* source,unsigned kind=0) {
    scene3d_append_drawable_defines(source,kind);strbuf_append_str(source,scene3d_vertex_body);strbuf_append_str(source,scene3d_drawable_vertex);strbuf_append_str(source,scene3d_outdoor_vertex);strbuf_append_str(source,scene3d_vertex_main);
}
// The fragment stage, after the `#version` line and the variant's defines:
// STANDARD selects metallic-roughness shading, TONE_MAPPING 1 or 2 the neutral
// or ACES curve. The BRDF and both curves follow Three.js r186 line for line,
// so its renderer can serve as the oracle.
// the same sampling coordinates drive color and alpha-tested shadow passes.
static const char* scene3d_uv_fragment = R"GLSL(
#if DRAWABLE_KIND > 0 && DRAWABLE_KIND == POINT_KIND
sample in vec2 texture_uv,texture_uv1;
sample in vec2 point_coord;
#else
in vec2 texture_uv,texture_uv1;
#endif
uniform vec3 uv_rows[MAP_COUNT*2];
uniform float uv_sets[MAP_COUNT];
vec2 map_uv(int slot) {
    vec3 uv=vec3(uv_sets[slot]>0.5?texture_uv1:texture_uv,1.0);
    return vec2(dot(uv_rows[slot*2],uv),dot(uv_rows[slot*2+1],uv));
}
)GLSL";
static void scene3d_append_uv_source(StrBuf* source,unsigned mapped=0) {
    scene3d_append_drawable_defines(source,mapped>>SCENE_DRAWABLE_SHIFT);
    strbuf_append_format(source,"#define MAP_COUNT %u\n",(unsigned)SCENE_MAP_COUNT);
    for(unsigned slot=0;slot<SCENE_MAP_COUNT;slot++) strbuf_append_format(source,"#define UV_%s %u\n",scene3d_map_specs[slot].sampler,slot);
    for(unsigned slot=SCENE_MAP_CLEARCOAT;slot<SCENE_MAP_COUNT;slot++) {
        bool present=(mapped&(1u<<(slot-SCENE_MAP_CLEARCOAT)))!=0;const char* sampler=scene3d_map_specs[slot].sampler;
        strbuf_append_format(source,"#define HAS_%s %u\n",sampler,present?1u:0u);
        if(present) strbuf_append_format(source,"uniform sampler2D %s;\n",sampler);
    }
    strbuf_append_str(source,scene3d_uv_fragment);
}
static const char* scene3d_fragment_body = R"GLSL(
in vec3 world_normal, world_position;
in vec4 vertex_color;
uniform vec4 material;
uniform float lambert, textured, opaque, light_count;
#ifdef MASK
uniform float alpha_cutoff; // fragments of a masked material below it are cut out
#endif
uniform vec3 ambient, emissive, directions[8], lights[8], light_positions[8];
uniform vec4 light_params[8];
uniform sampler2D image;
out vec4 fragment;
uniform vec3 sky, ground, hemisphere; // the hemisphere light's colors (black without one) and the unit direction to its sky
vec3 hemisphere_light(vec3 n) { return mix(ground, sky, 0.5*dot(n,hemisphere)+0.5); }
#ifdef ENVIRONMENT
uniform sampler2D environment;        // the prefiltered cube-UV atlas
uniform float environment_intensity;
#endif
#ifdef SHADOWS
// Three.js r186 PCF: five Vogel-disk taps, turned per pixel by interleaved gradient noise, each a
// hardware 2x2 depth comparison; beyond a map's frustum the surface is lit. SHADOW_CUBES marks the
// slots that hold a point light's cube map.
#if (SHADOW_CUBES & 1) != 0
uniform samplerCubeShadow shadow0;
#else
uniform sampler2DShadow shadow0;
#endif
#if (SHADOW_CUBES & 2) != 0
uniform samplerCubeShadow shadow1;
#else
uniform sampler2DShadow shadow1;
#endif
#if (SHADOW_CUBES & 4) != 0
uniform samplerCubeShadow shadow2;
#else
uniform sampler2DShadow shadow2;
#endif
#if (SHADOW_CUBES & 8) != 0
uniform samplerCubeShadow shadow3;
#else
uniform sampler2DShadow shadow3;
#endif
uniform vec4 shadow_params[4];  // intensity, bias, normal bias, softness (PCF radius in texels)
uniform float shadow_sizes[4];  // map texels per side
uniform vec2 shadow_ranges[4];  // a point light's near and far planes
uniform float light_shadows[8]; // each light's first shadow slot, or -1
uniform float light_cascades[8];
uniform vec4 cascade_ranges[4]; // camera-space near/far, transition fraction, reserved
uniform mat4 cascade_view;
uniform float receive;          // whether this mesh receives shadows
in vec4 shadow_coords[4];
vec4 shadow_coord(int slot) {
    if (slot == 0) return shadow_coords[0];
    if (slot == 1) return shadow_coords[1];
    if (slot == 2) return shadow_coords[2];
    return shadow_coords[3];
}
vec2 vogel_sample(int index, float phi) {
    float r = sqrt((float(index) + 0.5) / 5.0), theta = float(index)*2.399963229728653 + phi;
    return vec2(cos(theta), sin(theta))*r;
}
float shadow_noise() { return fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))))*6.283185307179586; }
// a directional or spot light's map (getShadow)
float plane_shadow(sampler2DShadow map, vec4 coord, vec4 params, float size) {
    coord.xyz /= coord.w;
    coord.z += params.y;
    float shadow = 1.0;
    if (coord.x >= 0.0 && coord.x <= 1.0 && coord.y >= 0.0 && coord.y <= 1.0 && coord.z <= 1.0) {
        float radius = params.w / size, phi = shadow_noise();
        shadow = 0.0;
        for (int i=0; i<5; i++) shadow += texture(map, vec3(coord.xy + vogel_sample(i,phi)*radius, coord.z));
        shadow *= 0.2;
    }
    return mix(1.0, shadow, params.x);
}
// a point light's cube map (getPointShadow): the face's perspective depth along the major axis,
// with the taps spread across the direction's tangent plane
float cube_shadow(samplerCubeShadow map, vec4 coord, vec4 params, float size, vec2 range) {
    vec3 toward = normalize(coord.xyz), extent = abs(coord.xyz);
    float depth = max(max(extent.x, extent.y), extent.z), shadow = 1.0;
    if (depth - range.y <= 0.0 && depth - range.x >= 0.0) {
        float compare = (range.y*(depth - range.x))/(depth*(range.y - range.x)) + params.y;
        float texel = params.w / size, phi = shadow_noise();
        vec3 across = abs(toward);
        vec3 tangent = normalize(cross(toward, across.x > across.z ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
        vec3 bitangent = cross(toward, tangent);
        shadow = 0.0;
        for (int i=0; i<5; i++) {
            vec2 s = vogel_sample(i, phi);
            shadow += texture(map, vec4(toward + (tangent*s.x + bitangent*s.y)*texel, compare));
        }
        shadow *= 0.2;
    }
    return mix(1.0, shadow, params.x);
}
#if (SHADOW_CUBES & 1) != 0
#define SLOT_SHADOW0(c,p) cube_shadow(shadow0, c, p, shadow_sizes[0], shadow_ranges[0])
#else
#define SLOT_SHADOW0(c,p) plane_shadow(shadow0, c, p, shadow_sizes[0])
#endif
#if (SHADOW_CUBES & 2) != 0
#define SLOT_SHADOW1(c,p) cube_shadow(shadow1, c, p, shadow_sizes[1], shadow_ranges[1])
#else
#define SLOT_SHADOW1(c,p) plane_shadow(shadow1, c, p, shadow_sizes[1])
#endif
#if (SHADOW_CUBES & 4) != 0
#define SLOT_SHADOW2(c,p) cube_shadow(shadow2, c, p, shadow_sizes[2], shadow_ranges[2])
#else
#define SLOT_SHADOW2(c,p) plane_shadow(shadow2, c, p, shadow_sizes[2])
#endif
#if (SHADOW_CUBES & 8) != 0
#define SLOT_SHADOW3(c,p) cube_shadow(shadow3, c, p, shadow_sizes[3], shadow_ranges[3])
#else
#define SLOT_SHADOW3(c,p) plane_shadow(shadow3, c, p, shadow_sizes[3])
#endif
float shadow_slot(int slot) {
    vec4 coord=shadow_coord(slot),params=shadow_params[slot];
    if(slot==0) return SLOT_SHADOW0(coord,params);
    if(slot==1) return SLOT_SHADOW1(coord,params);
    if(slot==2) return SLOT_SHADOW2(coord,params);
    return SLOT_SHADOW3(coord,params);
}
float light_shadow(int light) {
    int first=int(light_shadows[light]),count=int(light_cascades[light]);
    if(first<0||receive<0.5) return 1.0;
    if(count<=1) return shadow_slot(first);
    float depth=-(cascade_view*vec4(world_position,1.0)).z;
    int last=first+count-1,slot=first;
    if(depth>cascade_ranges[last].y) return 1.0;
    for(int i=0;i<3;i++) if(slot<last&&depth>cascade_ranges[slot].y) slot++;
    float result=shadow_slot(slot);
    float width=(cascade_ranges[slot].y-cascade_ranges[slot].x)*cascade_ranges[slot].z;
    if(slot<last&&width>0.0) result=mix(result,shadow_slot(slot+1),smoothstep(cascade_ranges[slot].y-width,cascade_ranges[slot].y,depth));
    return result;
}
#else
float light_shadow(int light) { return 1.0; }
#endif
// how much of light i reaches this point, and the direction `l` toward it
float light_falloff(int i, out vec3 l) {
    vec4 params = light_params[i];
    l = directions[i]; float falloff = 1.0;
    if (params.x > 0.5) {
        // point and spot lights: inverse-square falloff, smoothly cut off at `distance`
        vec3 d = light_positions[i] - world_position; float distance = max(length(d),1e-4);
        l = d / distance; falloff = 1.0 / max(distance*distance,0.01);
        if (params.y > 0.0) { float window = clamp(1.0 - pow(distance/params.y,4.0),0.0,1.0); falloff *= window*window; }
        if (params.x > 1.5) falloff *= smoothstep(params.z,params.w,dot(l,directions[i]));
    }
    return falloff;
}
#ifdef STANDARD
uniform vec4 eye;   // the camera position (w 1), or for orthographic cameras the direction toward it (w 0)
uniform mat4 view;  // world to camera: Three.js measures geometry roughness on view-space normals
uniform vec2 pbr;   // metalness, roughness
uniform sampler2D dfg; // the split-sum table: scale and bias over (roughness, n·v)
// maps: metalness in B and roughness in G, occlusion in R, emission; an absent map is white
uniform sampler2D metal_rough, occlusion, emissive_map, normal_map;
uniform vec4 maps;  // a normal map (x), its scale (y), occlusion strength (z), vertex tangents (w)
in vec3 world_tangent, world_bitangent;
const float PI = 3.141592653589793;
// the tangent frame from screen-space derivatives of position and uv, for meshes without tangents
mat3 tangent_frame(vec3 position, vec3 normal, vec2 uv) {
    vec3 q0 = dFdx(position), q1 = dFdy(position);
    vec2 st0 = dFdx(uv), st1 = dFdy(uv);
    vec3 q1perp = cross(q1, normal), q0perp = cross(normal, q0);
    vec3 T = q1perp*st0.x + q0perp*st1.x, B = q1perp*st0.y + q0perp*st1.y;
    float det = max(dot(T,T), dot(B,B));
    float scale = det == 0.0 ? 0.0 : inversesqrt(det);
    return mat3(T*scale, B*scale, normal);
}
// both normal layers use the coordinates of their own map, including mirrored frames.
vec3 mapped_normal(sampler2D map,int slot,float scale,vec3 n,float face,bool authored) {
    mat3 tbn=authored?mat3(normalize(world_tangent),normalize(world_bitangent),n):tangent_frame(world_position,n,map_uv(slot));
    tbn[0]*=face;tbn[1]*=face;
    vec3 mapped=texture(map,map_uv(slot)).xyz*2.0-1.0;mapped.xy*=scale;
    return normalize(tbn*mapped);
}
float specular_occlusion(float dotNV,float occlusion,float roughness) {
    return clamp(pow(dotNV+occlusion,exp2(-16.0*roughness-1.0))-1.0+occlusion,0.0,1.0);
}
float D_GGX(float alpha, float dotNH) {
    float a2 = alpha*alpha; float denom = dotNH*dotNH*(a2-1.0)+1.0; return 0.3183098861837907*a2/(denom*denom);
}
float V_GGX_SmithCorrelated(float alpha, float dotNL, float dotNV) {
    float a2 = alpha*alpha;
    float gv = dotNL*sqrt(a2+(1.0-a2)*dotNV*dotNV); float gl = dotNV*sqrt(a2+(1.0-a2)*dotNL*dotNL);
    return 0.5/max(gv+gl,1e-6);
}
vec3 F_Schlick(vec3 f0, float dotVH) { float fresnel = exp2((-5.55473*dotVH-6.98316)*dotVH); return f0*(1.0-fresnel)+fresnel; }
vec3 standard_light(vec3 base,vec3 emission) {
    float face = gl_FrontFacing ? 1.0 : -1.0;
    vec3 n = normalize(world_normal) * face;
#ifdef PHYSICAL
    vec3 coat_n=n;
#if HAS_coat_normal_map
    if(coat_normal_params.x>0.5) coat_n=mapped_normal(coat_normal_map,UV_coat_normal_map,coat_normal_params.y,coat_n,face,coat_normal_params.z>0.5);
#endif
#endif
    vec3 v = eye.w > 0.5 ? normalize(eye.xyz - world_position) : normalize(eye.xyz);
    // a surface that curves fast across a pixel gets a wider highlight, as in Three.js
    vec3 view_normal = normalize(mat3(view) * world_normal) * face;
    vec3 dxy = max(abs(dFdx(view_normal)), abs(dFdy(view_normal)));
    if(maps.x>0.5) n=mapped_normal(normal_map,UV_normal_map,maps.y,n,face,maps.w>0.5);
    n=outdoor_normal(n,world_position);
    float geometry_roughness=max(max(dxy.x,dxy.y),dxy.z);
    vec4 texel = texture(metal_rough, map_uv(UV_metal_rough));
    float metalness = pbr.x*texel.b;
    float roughness = min(max(pbr.y*texel.g,0.0525) + geometry_roughness, 1.0);
    float alpha = roughness*roughness;
    // the dielectric reflectance (0.04) and the metal-blended one the specular lobe uses
    vec3 diffuse = base*(1.0-metalness);
#ifdef PHYSICAL
    vec3 dielectric=vec3(dielectric_reflectance);
#else
    vec3 dielectric=vec3(0.04);
#endif
    vec3 f0 = mix(dielectric,base,metalness);
    float dotNV = clamp(dot(n,v),0.0,1.0);
    vec2 fab = texture(dfg, vec2(roughness,dotNV)).rg;
    // energy lost to single-scattering GGX comes back through multiple scattering
    vec3 compensation = 1.0 + f0*(1.0/(fab.x+fab.y) - 1.0);
    vec3 single = dielectric*fab.x + fab.y;
    float lost = 1.0 - (fab.x+fab.y);
    vec3 average = dielectric + (1.0-dielectric)*0.047619;
    vec3 multiple = single*average/(1.0-lost*average)*lost;
    // native intensities carry the physical convention's π (SP1): π·I lights the Lambert term diffuse/π
    vec3 diffuse_irradiance=ambient+hemisphere_light(n);
    vec3 indirect_diffuse=diffuse_irradiance*diffuse*(1.0-single-multiple),indirect_specular=vec3(0.0);
#ifdef PHYSICAL
    float coat=layer_params.x,coat_roughness=layer_params.y;
#if HAS_coat_map
    coat*=texture(coat_map,map_uv(UV_coat_map)).r;
#endif
#if HAS_coat_roughness_map
    coat_roughness*=texture(coat_roughness_map,map_uv(UV_coat_roughness_map)).g;
#endif
    coat_roughness=min(max(coat_roughness,0.0525)+geometry_roughness,1.0);
    float coat_alpha=coat_roughness*coat_roughness,coat_dotNV=clamp(dot(coat_n,v),0.0,1.0);
    vec3 sheen=layer_params.z*sheen_color;float sheen_roughness=layer_params.w;
#if HAS_sheen_color_map
    sheen*=texture(sheen_color_map,map_uv(UV_sheen_color_map)).rgb;
#endif
#if HAS_sheen_roughness_map
    sheen_roughness*=texture(sheen_roughness_map,map_uv(UV_sheen_roughness_map)).a;
#endif
    sheen_roughness=clamp(sheen_roughness,0.07,1.0);
    float sheen_max=max(max(sheen.r,sheen.g),sheen.b),sheen_nv=sheen_albedo(dotNV,sheen_roughness);
    float indirect_weight=1.0-sheen_max*sheen_nv;
    vec3 coat_direct=vec3(0.0),coat_indirect=vec3(0.0),sheen_direct=vec3(0.0);
#endif
#ifdef ENVIRONMENT
    // environment light: the roughest level as irradiance, and the level of this roughness along
    // the reflection, weighted by the dielectric and metallic split-sum terms
    vec3 irradiance = textureCubeUV(environment, n, 1.0).rgb*environment_intensity;
    vec3 reflected = normalize(mix(reflect(-v,n), n, pow(roughness,4.0)));
    vec3 radiance = textureCubeUV(environment, reflected, roughness).rgb*environment_intensity;
    vec3 single_metal = base*fab.x + fab.y, average_metal = base + (1.0-base)*0.047619;
    vec3 multiple_metal = single_metal*average_metal/(1.0-lost*average_metal)*lost;
    indirect_specular = radiance*mix(single,single_metal,metalness) + mix(multiple,multiple_metal,metalness)*irradiance;
    indirect_diffuse += diffuse*(1.0-single-multiple)*irradiance;
    diffuse_irradiance+=irradiance;
#ifdef PHYSICAL
    vec3 coat_reflected=normalize(mix(reflect(-v,coat_n),coat_n,pow(coat_roughness,4.0)));
    vec3 coat_radiance=textureCubeUV(environment,coat_reflected,coat_roughness).rgb*environment_intensity;
    vec2 coat_dfg=texture(dfg,vec2(coat_roughness,coat_dotNV)).rg;
    coat_indirect=coat_radiance*(0.04*coat_dfg.x+coat_dfg.y);
#endif
#endif
    // occlusion darkens indirect light only, specular by Lagarde's approximation; direct light keeps its own visibility
    float occluded = (texture(occlusion, map_uv(UV_occlusion)).r - 1.0)*maps.z + 1.0;
#ifdef PHYSICAL
    indirect_diffuse*=indirect_weight;indirect_specular*=indirect_weight;
    vec3 sheen_indirect=diffuse_irradiance*sheen*sheen_nv*occluded;
    coat_indirect*=specular_occlusion(coat_dotNV,occluded,coat_roughness);
#endif
#ifdef TRANSMISSION
    vec3 outgoing_diffuse=indirect_diffuse*occluded;
#endif
    vec3 color=indirect_diffuse*occluded+indirect_specular*specular_occlusion(dotNV,occluded,roughness);
    for (int i=0; i<8; i++) if (float(i)<light_count) {
        vec3 l; float falloff = light_falloff(i,l);
        vec3 h = normalize(l+v);
        float dotNL = clamp(dot(n,l),0.0,1.0), dotNH = clamp(dot(n,h),0.0,1.0), dotVH = clamp(dot(v,h),0.0,1.0);
        float visibility=light_shadow(i);
        vec3 irradiance=lights[i]*falloff*dotNL*visibility;
#ifdef PHYSICAL
        float coat_dotNL=clamp(dot(coat_n,l),0.0,1.0),coat_dotNH=clamp(dot(coat_n,h),0.0,1.0);
        coat_direct+=lights[i]*falloff*coat_dotNL*visibility*PI*F_Schlick(vec3(0.04),dotVH)*
            V_GGX_SmithCorrelated(coat_alpha,coat_dotNL,coat_dotNV)*D_GGX(coat_alpha,coat_dotNH);
        sheen_direct+=irradiance*PI*sheen*sheen_brdf(sheen_roughness,dotNH,dotNV,dotNL);
        irradiance*=1.0-sheen_max*max(sheen_nv,sheen_albedo(dotNL,sheen_roughness));
#endif
        vec3 specular = PI*F_Schlick(f0,dotVH)*(V_GGX_SmithCorrelated(alpha,dotNL,dotNV)*D_GGX(alpha,dotNH))*compensation;
        vec3 direct_diffuse=diffuse*(1.0-F_Schlick(dielectric,dotVH));
        color += irradiance*(direct_diffuse + specular);
        if(foliage_params.x>0.0) color+=base*foliage_color*foliage_params.x*exp(-foliage_params.y)*
            lights[i]*falloff*max(-dot(n,l),0.0)*visibility;
#ifdef TRANSMISSION
        outgoing_diffuse+=irradiance*direct_diffuse;
#endif
    }
#ifdef TRANSMISSION
    float transmitted=transmission_params.x,thickness=transmission_params.z;
#if HAS_transmission_map
    transmitted*=texture(transmission_map,map_uv(UV_transmission_map)).r;
#endif
#if HAS_thickness_map
    thickness*=texture(thickness_map,map_uv(UV_thickness_map)).g;
#endif
    if(transmitted>0.0) {
        vec3 direction=refract(-v,n,1.0/transmission_params.y);
        vec3 ray=direction*thickness*transmission_scale;
        vec4 clip=transmission_projection*vec4(world_position+ray,1.0);
        vec2 screen_uv=clip.w>0.0?clip.xy/clip.w*0.5+0.5:vec2(-1.0);
        vec3 fallback=transmission_background;
#ifdef ENVIRONMENT
        fallback=textureCubeUV(environment,direction,roughness).rgb*environment_intensity;
#endif
        vec3 radiance=transmitted_radiance(screen_uv,fallback,roughness);
        if(thickness>0.0&&transmission_params.w>0.0) radiance*=pow(attenuation_color,vec3(length(ray)/transmission_params.w));
        vec3 through=radiance*diffuse*(1.0-single)*indirect_weight;
        color+=transmitted*(through-outgoing_diffuse);
    }
#endif
    color+=emission;
#ifdef PHYSICAL
    color+=sheen_direct+sheen_indirect;
    color=color*(1.0-coat*F_Schlick(vec3(0.04),coat_dotNV))+(coat_direct+coat_indirect)*coat;
#endif
    return color;
}
#endif

void main() {
    // preserve primitive coverage through the provider's multisample color merge.
    gl_SampleMask[0] = gl_SampleMaskIn[0];
#if DRAWABLE_KIND > 0 && DRAWABLE_KIND == POINT_KIND
    vec2 point=point_coord-0.5;if(dot(point,point)>0.25) discard;
#endif
    vec4 base = material * vertex_color;
    if (textured > 0.5) base *= texture(image,map_uv(UV_image));
#ifdef MASK
    if (base.a < alpha_cutoff) discard;
#endif
    if (opaque > 0.5) base.a = 1.0;
#ifdef STANDARD
    vec3 color = standard_light(base.rgb,emissive*texture(emissive_map, map_uv(UV_emissive_map)).rgb);
#else
    vec3 irradiance = vec3(1.0);
    if (lambert > 0.5) {
        vec3 n = normalize(world_normal) * (gl_FrontFacing ? 1.0 : -1.0);
        irradiance = ambient + hemisphere_light(n);
#ifdef ENVIRONMENT
        irradiance += textureCubeUV(environment, n, 1.0).rgb*environment_intensity;
#endif
        for (int i=0; i<8; i++) if (float(i)<light_count) {
            vec3 l; float falloff = light_falloff(i,l);
            irradiance += lights[i] * falloff * max(dot(n,l),0.0) * light_shadow(i);
        }
    }
    vec3 color = base.rgb * irradiance + emissive;
#endif
#if TONE_MAPPING > 0
    color = tone_map(outdoor_fog(color,world_position));
#else
    color = outdoor_fog(color,world_position);
#endif
    fragment = vec4(color * base.a, base.a);
}
)GLSL";

// tone mapping follows composition in HDR; the background remains an unlit display color.
static const char* scene3d_tone_body = R"GLSL(
#if TONE_MAPPING > 0
uniform float exposure;
#endif
#if TONE_MAPPING == 1
// Khronos PBR Neutral
vec3 tone_map(vec3 color) {
    const float StartCompression = 0.8 - 0.04; const float Desaturation = 0.15;
    color *= exposure;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25*x*x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < StartCompression) return color;
    float d = 1. - StartCompression;
    float newPeak = 1. - d*d/(peak + d - StartCompression);
    color *= newPeak/peak;
    float g = 1. - 1./(Desaturation*(peak - newPeak) + 1.);
    return mix(color, vec3(newPeak), g);
}
#elif TONE_MAPPING == 2
// the ACES filmic fit (Stephen Hill), as Three.js scales it
vec3 RRTAndODTFit(vec3 v) { vec3 a = v*(v+0.0245786)-0.000090537; vec3 b = v*(0.983729*v+0.4329510)+0.238081; return a/b; }
vec3 tone_map(vec3 color) {
    const mat3 ACESInputMat = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
    const mat3 ACESOutputMat = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));
    color *= exposure/0.6;
    color = ACESOutputMat*RRTAndODTFit(ACESInputMat*color);
    return clamp(color, 0.0, 1.0);
}
#endif
)GLSL";
static const char* scene3d_output_fragment = R"GLSL(
uniform sampler2DMS color_input;
uniform vec4 background;
#if POSTPROCESSING
uniform sampler2D resolved_input, processed_input;
#endif
out vec4 fragment;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 scene = texelFetch(color_input, pixel, gl_SampleID);
    vec3 color = scene.a > 0.0 ? scene.rgb / scene.a : vec3(0.0);
    vec3 addition=vec3(0.0);
#if POSTPROCESSING
    // carry the resolved image change back to each original coverage sample.
    vec3 delta=texelFetch(processed_input,pixel,0).rgb-texelFetch(resolved_input,pixel,0).rgb;
    color=max(color+delta,vec3(0.0));addition=max(delta,vec3(0.0));
#endif
#if TONE_MAPPING > 0
    color = tone_map(color);
#if POSTPROCESSING
    addition=tone_map(addition);
#endif
#endif
    fragment = vec4(color * scene.a + (background.rgb+addition) * background.a * (1.0-scene.a),
        scene.a + background.a * (1.0-scene.a));
}
)GLSL";

// Reading Three.js r186's cube-UV atlas (its cube_uv_reflection_fragment): a direction picks a
// face and a texel inside that face's one-texel border; levels below 16 texels are the extra
// roughness levels side by side. Shared by the environment filter and environment-lit materials.
static const char* scene3d_cube_uv = R"GLSL(
const float CUBEUV_TEXEL_WIDTH = 1.0/768.0, CUBEUV_TEXEL_HEIGHT = 1.0/1024.0, CUBEUV_MAX_MIP = 8.0;
const float cubeUV_minMipLevel = 4.0, cubeUV_minTileSize = 16.0;
float getFace(vec3 direction) {
    vec3 absDirection = abs(direction);
    float face = -1.0;
    if (absDirection.x > absDirection.z) {
        if (absDirection.x > absDirection.y) face = direction.x > 0.0 ? 0.0 : 3.0;
        else face = direction.y > 0.0 ? 1.0 : 4.0;
    } else {
        if (absDirection.z > absDirection.y) face = direction.z > 0.0 ? 2.0 : 5.0;
        else face = direction.y > 0.0 ? 1.0 : 4.0;
    }
    return face;
}
vec2 getUV(vec3 direction, float face) {
    vec2 uv;
    if (face == 0.0) uv = vec2(direction.z, direction.y) / abs(direction.x);
    else if (face == 1.0) uv = vec2(-direction.x, -direction.z) / abs(direction.y);
    else if (face == 2.0) uv = vec2(-direction.x, direction.y) / abs(direction.z);
    else if (face == 3.0) uv = vec2(-direction.z, direction.y) / abs(direction.x);
    else if (face == 4.0) uv = vec2(-direction.x, direction.z) / abs(direction.y);
    else uv = vec2(direction.x, direction.y) / abs(direction.z);
    return 0.5 * (uv + 1.0);
}
vec3 bilinearCubeUV(sampler2D envMap, vec3 direction, float mipInt) {
    float face = getFace(direction);
    float filterInt = max(cubeUV_minMipLevel - mipInt, 0.0);
    mipInt = max(mipInt, cubeUV_minMipLevel);
    float faceSize = exp2(mipInt);
    vec2 uv = getUV(direction, face) * (faceSize - 2.0) + 1.0;
    if (face > 2.0) { uv.y += faceSize; face -= 3.0; }
    uv.x += face * faceSize;
    uv.x += filterInt * 3.0 * cubeUV_minTileSize;
    uv.y += 4.0 * (exp2(CUBEUV_MAX_MIP) - faceSize);
    uv.x *= CUBEUV_TEXEL_WIDTH;
    uv.y *= CUBEUV_TEXEL_HEIGHT;
    return textureLod(envMap, uv, 0.0).rgb;
}
float roughnessToMip(float roughness) {
    float mip = 0.0;
    if (roughness >= 0.8) mip = (1.0 - roughness) * (-1.0 + 2.0) / (1.0 - 0.8) - 2.0;
    else if (roughness >= 0.4) mip = (0.8 - roughness) * (2.0 + 1.0) / (0.8 - 0.4) - 1.0;
    else if (roughness >= 0.305) mip = (0.4 - roughness) * (3.0 - 2.0) / (0.4 - 0.305) + 2.0;
    else if (roughness >= 0.21) mip = (0.305 - roughness) * (4.0 - 3.0) / (0.305 - 0.21) + 3.0;
    else mip = -2.0 * log2(1.16 * roughness);
    return mip;
}
vec4 textureCubeUV(sampler2D envMap, vec3 sampleDir, float roughness) {
    float mip = clamp(roughnessToMip(roughness), -2.0, CUBEUV_MAX_MIP);
    float mipF = fract(mip);
    float mipInt = floor(mip);
    vec3 color0 = bilinearCubeUV(envMap, sampleDir, mipInt);
    if (mipF == 0.0) return vec4(color0, 1.0);
    vec3 color1 = bilinearCubeUV(envMap, sampleDir, mipInt + 1.0);
    return vec4(mix(color0, color1, mipF), 1.0);
}
)GLSL";
// An image environment unwraps into the atlas's top level by the same quads, each texel reading the
// equirectangular image along its direction (Three.js r186 EquirectangularToCubeUV).
static const char* scene3d_unwrap_fragment = R"GLSL(#version 330 core
in vec3 output_direction;
uniform sampler2D source;
out vec4 fragment;
void main() {
    vec3 d = normalize(output_direction);
    vec2 uv = vec2(atan(d.z, d.x)*0.15915494309189535 + 0.5, asin(clamp(d.y, -1.0, 1.0))*0.3183098861837907 + 0.5);
    fragment = vec4(texture(source, uv).rgb, 1.0);
}
)GLSL";
// The environment filter draws one atlas level's six faces as quads whose corners carry the
// direction each texel looks along (Three.js r186 _createPlanes), and GGX-filters the level
// above into it (_getGGXShader); roughness 0 copies.
static const char* scene3d_filter_vertex = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 direction;
out vec3 output_direction;
void main() { output_direction = direction; gl_Position = vec4(position, 1.0); }
)GLSL";
static const char* scene3d_filter_fragment = R"GLSL(
in vec3 output_direction;
uniform sampler2D source;
uniform float roughness, mip_int;
out vec4 fragment;
const float PI = 3.14159265359;
float radical_inverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
// GGX visible-normal sampling (Heitz 2018) about V = +z
vec3 sample_ggx_vndf(vec2 Xi, vec3 V, float roughness) {
    float alpha = roughness * roughness;
    vec3 T1 = vec3(1.0, 0.0, 0.0), T2 = cross(V, T1);
    float r = sqrt(Xi.x), phi = 2.0 * PI * Xi.y;
    float t1 = r * cos(phi), t2 = r * sin(phi), s = 0.5 * (1.0 + V.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * V;
    return normalize(vec3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));
}
void main() {
    vec3 N = normalize(output_direction), V = N;
    if (roughness < 0.001) { fragment = vec4(bilinearCubeUV(source, N, mip_int), 1.0); return; }
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N)), bitangent = cross(N, tangent);
    vec3 total = vec3(0.0); float weight = 0.0;
    for (uint i = 0u; i < 256u; i++) {
        vec3 h = sample_ggx_vndf(vec2(float(i) / 256.0, radical_inverse(i)), vec3(0.0, 0.0, 1.0), roughness);
        vec3 H = normalize(tangent * h.x + bitangent * h.y + N * h.z);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) { total += bilinearCubeUV(source, L, mip_int) * NdotL; weight += NdotL; }
    }
    fragment = vec4(weight > 0.0 ? total / weight : total, 1.0);
}
)GLSL";

// The split-sum DFG table of GGX specular, laid out as Three.js r186 tabulates it:
// 16 x 16 texels over roughness (x) and n·v (y), each the integral at its texel
// centre over 4096 Hammersley samples; scale in R, bias in G. Standard materials
// read it for their multiple-scattering energy terms (and environment light, later).
static const unsigned SCENE_DFG_SIZE = 16;
const float* scene3d_dfg_table() {
    static float table[SCENE_DFG_SIZE*SCENE_DFG_SIZE*4];
    static bool ready=false;
    if (ready) return table;
    const unsigned samples=4096;
    for (unsigned row=0;row<SCENE_DFG_SIZE;row++) for (unsigned col=0;col<SCENE_DFG_SIZE;col++) {
        double rough=(col+.5)/SCENE_DFG_SIZE,nov=(row+.5)/SCENE_DFG_SIZE,alpha=rough*rough,a2=alpha*alpha;
        double vx=sqrt(1-nov*nov),scale=0,bias=0;
        for (unsigned i=0;i<samples;i++) {
            // the Hammersley point: i / n and the bit-reversed radical inverse of i
            uint32_t bits=i;
            bits=(bits<<16)|(bits>>16);
            bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
            bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
            double phi=2*M_PI*i/samples,v=bits*2.3283064365386963e-10;
            double cos_theta=sqrt((1-v)/(1+(a2-1)*v)),sin_theta=sqrt(fmax(0,1-cos_theta*cos_theta));
            double hx=sin_theta*cos(phi),hz=cos_theta,voh=hx*vx+hz*nov,lz=2*voh*hz-nov;
            if (lz<=0) continue;
            double nol=fmin(lz,1),visibility=0.5/(nol*sqrt(nov*nov*(1-a2)+a2)+nov*sqrt(nol*nol*(1-a2)+a2));
            double weight=visibility*nol*4*fmax(voh,0)/hz,fresnel=pow(1-fmin(fmax(voh,0),1),5);
            scale+=(1-fresnel)*weight;bias+=fresnel*weight;
        }
        float* texel=table+(row*SCENE_DFG_SIZE+col)*4;
        texel[0]=(float)(scale/samples);texel[1]=(float)(bias/samples);texel[2]=0;texel[3]=1;
    }
    ready=true;return table;
}
static NativeGlResource scene3d_dfg(Scene3dEntry* entry) {
    if (entry->dfg.id) return entry->dfg;
    entry->dfg=native_gl_data_texture(entry->graphics,SCENE_DFG_SIZE,SCENE_DFG_SIZE,true);
    if (entry->dfg.id && !native_gl_data_texture_update(entry->graphics,entry->dfg,scene3d_dfg_table(),SCENE_DFG_SIZE))
        { native_gl_release(entry->graphics,entry->dfg);entry->dfg={}; }
    if (!entry->dfg.id) scene3d_fail(entry,native_gl_diagnostic(entry->graphics));
    return entry->dfg;
}

// `shadows`: 0 without shadow maps, else 1 plus a bit per point-light (cube) slot
static unsigned scene3d_variant(bool standard, unsigned tone, bool mask, bool environment, unsigned shadows, bool physical=false) {
    return tone+SCENE_TONE_CURVES*((standard?1:0)+(mask?2:0)+(environment?4:0)+(physical?8:0)+shadows*16);
}
static NativeGlResource scene3d_program(Scene3dEntry* entry,unsigned variant,unsigned mapped=0,uint64_t** frame=nullptr) {
    unsigned key=variant+mapped*SCENE_PROGRAM_VARIANTS;
    if(!mapped) {
        if(frame) *frame=&entry->program_frame[variant];
        if(entry->programs[variant].id) return entry->programs[variant];
    } else for(size_t i=0;i<entry->mapped_count;i++) if(entry->mapped_programs[i].key==key) {
        if(frame) *frame=&entry->mapped_programs[i].frame;return entry->mapped_programs[i].program;
    }
    char header[512],shadows[64]="";
    unsigned model=variant/SCENE_TONE_CURVES,layout=model/16,kind=mapped>>SCENE_DRAWABLE_SHIFT;
    if(layout) snprintf(shadows,sizeof(shadows),"#define SHADOWS\n#define SHADOW_CUBES %u\n",layout-1);
    snprintf(header,sizeof(header),"#version 400 core\n%s%s%s%s%s%s#define TONE_MAPPING %u\n",
        model&1?"#define STANDARD\n":"",model&2?"#define MASK\n":"",model&4?"#define ENVIRONMENT\n":"",
        model&8?"#define PHYSICAL\n":"",mapped&SCENE_TRANSMISSION_FEATURE?"#define TRANSMISSION\n":"",shadows,variant%SCENE_TONE_CURVES);
    StrBuf* vertex=strbuf_new();strbuf_append_str(vertex,header);scene3d_append_vertex_source(vertex,kind);
    StrBuf* source=strbuf_new();strbuf_append_str(source,header);
    if(model&4) strbuf_append_str(source,scene3d_cube_uv);
    strbuf_append_str(source,scene3d_tone_body);scene3d_append_uv_source(source,mapped);
    strbuf_append_str(source,scene3d_physical_fragment);strbuf_append_str(source,scene3d_outdoor_fragment);strbuf_append_str(source,scene3d_fragment_body);
    NativeGlResource program=native_gl_program(entry->graphics,vertex->str,source->str);strbuf_free(source);strbuf_free(vertex);
    if(!program.id) {scene3d_fail(entry,native_gl_diagnostic(entry->graphics));return {};}
    // only actually used map combinations consume cache storage and device samplers.
    if(!mapped) entry->programs[variant]=program;
    else {
        if(!lam::mem_grow_array(&entry->mapped_programs,&entry->mapped_capacity,entry->mapped_count+1,(size_t)8,MEM_CAT_RENDER)) {
            native_gl_release(entry->graphics,program);scene3d_fail(entry,"material program cache allocation failed");return {};
        }
        auto* cached=&entry->mapped_programs[entry->mapped_count++];*cached={key,program,0};if(frame) *frame=&cached->frame;
    }
    entry->stats.material_links++;return program;
}

static bool scene3d_compile(Scene3dEntry* entry, DomElement* root, UiContext* ui) {
    scene3d_projection_destroy(entry); entry->diagnostic[0]=0;
    // failed attempts must not mark unused programs as members of the next committed projection.
    unsigned retained_programs=entry->shader_program_count;
    for(auto* choice=entry->lod_choices;choice;choice=choice->next) choice->used=false;
    for (unsigned i=0;i<retained_programs;i++) entry->shader_programs[i].generation=0;
    if(!scene3d_prepare_physics(entry,root,!ui->export_native_graphics)||!scene3d_prepare_animation(entry,root)) return false;
    Scene3dProjection* p=&entry->projection;
    p->pool=mem_pool_create((MemContext*)root->doc->services.mem_ctx,MEM_ROLE_RENDER,"scene3d.projection");
    if (!p->pool) return scene3d_fail(entry,"projection allocation failed");
    Scene3dBuild build={};build.entry=entry;build.ui=ui;build.root=root;build.camera=scene3d_text(root,"camera");
    build.definitions=(Scene3dDefinition*)mem_calloc(SCENE_NODE_LIMIT,sizeof(Scene3dDefinition),MEM_CAT_RENDER);
    if (!build.definitions) return scene3d_fail(entry,"definition allocation failed");
    const char* tone=scene3d_text(root,"tone-mapping");
    if(!scene3d_flag(root,"frustum-culling",true,&p->frustum_culling)) {mem_free(build.definitions);return scene3d_fail(entry,"invalid frustum-culling flag");}
    p->tone_mapping=!tone||!strcmp(tone,"none")?0:!strcmp(tone,"neutral")?1:!strcmp(tone,"aces")?2:-1;
    if (p->tone_mapping<0) { mem_free(build.definitions); return scene3d_fail(entry,"unknown tone mapping"); }
    if (!scene3d_number(root,"exposure",1,&p->exposure) || !isfinite(p->exposure) || p->exposure<=0)
        { mem_free(build.definitions); return scene3d_fail(entry,"invalid exposure"); }
    build.environment=scene3d_text(root,"environment");
    if (!scene3d_number(root,"environment-intensity",1,&p->environment_intensity) || !isfinite(p->environment_intensity) ||
        p->environment_intensity<0) { mem_free(build.definitions); return scene3d_fail(entry,"invalid environment intensity"); }
    bool valid=scene3d_color(root,"background","transparent",p->background) &&
        scene3d_collect_definitions(&build,root,0) && scene3d_build_simulations(&build) && scene3d_build_effects(&build) &&
        scene3d_project_node(&build,root,scene3d_identity(),true);
    // consumers choose the color space first; validate unused declarations without uploading an unused sRGB copy of data maps.
    for (unsigned i=0;valid&&i<build.definition_count;i++) {
        Scene3dDefinition* definition=&build.definitions[i];
        if (scene3d_tag(definition->source,"texture")&&!definition->texture.id&&!definition->linear_texture.id)
            valid=scene3d_texture(&build,definition).id!=0;
    }
    mem_free(build.definitions);
    scene3d_lod_prune(entry);
    if (valid && !p->camera_found) valid=scene3d_fail(entry,"active camera is missing");
    if (valid && build.environment && !p->environment.address) valid=scene3d_fail(entry,"scene environment names no environment element");
    if (!valid) {
        // roll back programs created by this attempt; retain the previously committed source cache.
        while (entry->shader_program_count>retained_programs)
            native_gl_release(entry->graphics,entry->shader_programs[--entry->shader_program_count].program);
        if (!entry->diagnostic[0]) scene3d_fail(entry,"invalid scene description");return false;
    }
    entry->projection_generation++;scene3d_shader_prune(entry);scene3d_simulation_prune(entry);return true;
}
static int scene3d_draw_compare(const void* a, const void* b) {
    const Scene3dMesh* left=*(Scene3dMesh* const*)a;const Scene3dMesh* right=*(Scene3dMesh* const*)b;
    if (left->material->depth_only!=right->material->depth_only) return left->material->depth_only?-1:1;
    unsigned l=scene3d_transparent(left)?2:left->material->transmission?1:0,r=scene3d_transparent(right)?2:right->material->transmission?1:0;
    if(l!=r) return l<r?-1:1;
    if (scene3d_transparent(left) && left->depth!=right->depth) return left->depth<right->depth?-1:1;
    return left->order<right->order?-1:left->order>right->order?1:0;
}
static bool scene3d_uniform(Scene3dEntry* entry, NativeGlResource program, const char* name, const float* values,
        unsigned components, unsigned count=1) {
    NativeGlUniform uniform=native_gl_uniform(entry->graphics,program,name);
    // each variant's compiler drops the uniforms it never reads
    return uniform.location<0 || native_gl_uniform_set(entry->graphics,uniform,values,components,count);
}
static bool scene3d_bind_uv(Scene3dEntry* entry,NativeGlResource program,const Scene3dMaterial* material,bool base_only=false) {
    return scene3d_uniform(entry,program,"uv_rows[0]",material->uv_rows[0],3,base_only?2:SCENE_MAP_COUNT*2)&&
        scene3d_uniform(entry,program,"uv_sets[0]",material->uv_sets,1,base_only?1:SCENE_MAP_COUNT);
}
// One drawing pass: the view, or one face of an environment capture.
struct Scene3dPass {
    const Scene3dLights* lights;
    Scene3dMatrix projection, view;
    float eye[4], exposure, environment_intensity;
    unsigned tone;
    bool environment; // standard and Lambert materials take light from the prefiltered environment
    const Scene3dShadows* shadows; // the casting lights whose maps standard and Lambert materials read, if any
    uint64_t frame;   // the pass's scene uniforms are set once per variant per frame
    Scene3dMatrix clip; // the lens alone (`projection` is clip × view), for shader materials
    NativeGlResource transmission={};
    float background[3]={},viewport[2]={};
    const Scene3dFog* fog=nullptr;
};
static bool scene3d_mesh_in_view(Scene3dEntry* entry,const Scene3dMesh* mesh,const Scene3dMatrix& lens,const Scene3dMatrix& view,const float viewport[2]);
static double scene3d_lod_distance(const Scene3dLod* lod,Scene3dVec eye) {
    Scene3dVec origin=scene3d_point(lod->world,{});
    return hypot(hypot((double)origin.x-eye.x,(double)origin.y-eye.y),(double)origin.z-eye.z);
}
static bool scene3d_lod_accepts(const Scene3dMesh* mesh,Scene3dVec eye,unsigned view) {
    for(auto* branch=mesh->lod;branch;branch=branch->parent) {
        const auto* lod=branch->lod;
        unsigned selected=view==2?0:view==0?lod->choice->selected:scene3d_lod_level(lod->distances,lod->count,0,scene3d_lod_distance(lod,eye),0,false);
        if(branch->level!=selected) return false;
    }
    return true;
}
// SH1: a shader material sees the camera as separate view and projection matrices, then its own uniforms
static bool scene3d_draw_shader(Scene3dEntry* entry, const Scene3dPass& pass, Scene3dMesh* mesh) {
    const Scene3dShader* shader=mesh->material->shader;NativeGlResource program=mesh->program;
    Scene3dMatrix camera;if (!scene3d_inverse_affine(pass.view,&camera)) return false;
    if (!scene3d_uniform(entry,program,"model_matrix",mesh->world.v,16) || !scene3d_uniform(entry,program,"view_matrix",pass.view.v,16) ||
        !scene3d_uniform(entry,program,"projection_matrix",pass.clip.v,16) || !scene3d_uniform(entry,program,"camera_position",camera.v+12,3))
        return false;
    NativeGlDraw draw={program,mesh->vertices,
        mesh->geometry->index_count?mesh->geometry->index_count:mesh->geometry->vertex_count,mesh->instance_count,
        mesh->geometry->index_count!=0,scene3d_transparent(mesh),mesh->material->side,mesh->clockwise};
    draw.alpha_to_coverage=shader->alpha_to_coverage;
    if (!scene3d_bind_uniforms(entry,program,shader->uniforms,shader->uniform_count,&draw)) return false;
    return native_gl_draw(entry->graphics,&draw);
}
static bool scene3d_bind_outdoor(Scene3dEntry* entry,NativeGlResource program,const Scene3dMaterial* material) {
    const float* v=material->outdoor;float water[]={v[SCENE_WATER_AMPLITUDE],v[SCENE_WATER_FREQUENCY],v[SCENE_WATER_PHASE],v[SCENE_WATER_DIRECTION]};
    return scene3d_uniform(entry,program,"wind_params",v,4)&&scene3d_uniform(entry,program,"wind_direction",v+SCENE_WIND_DIRECTION,1)&&
        scene3d_uniform(entry,program,"foliage_params",v+SCENE_FOLIAGE_FACTOR,2)&&scene3d_uniform(entry,program,"foliage_color",material->foliage_color,3)&&
        scene3d_uniform(entry,program,"water_params",water,4);
}
static bool scene3d_tangent_compatible(const Scene3dMesh* mesh,unsigned slot) {
    const auto* material=mesh->material;const float* rows=material->uv_rows[slot];
    return mesh->geometry->has_tangents&&material->uv_sets[slot]==0&&rows[0]==1&&rows[1]==0&&rows[3]==0&&rows[4]==1;
}
static NativeGlResource scene3d_mesh_program(Scene3dEntry* entry,const Scene3dPass& pass,Scene3dMesh* mesh,uint64_t** frame) {
    Scene3dMaterial* material=mesh->material;
    bool lit=pass.environment&&(material->standard||material->lambert);
    bool shaded=pass.shadows&&pass.shadows->count&&(material->standard||material->lambert);
    unsigned layout=0;
    if (shaded) {
        for (unsigned s=0;s<pass.shadows->count;s++) if (pass.shadows->slots[s].kind==1) layout|=1u<<s;
        layout++;
    }
    unsigned variant=scene3d_variant(material->standard,pass.tone,material->alpha_cutoff>0,lit,layout,material->physical);
    unsigned mapped=0;
    if(material->physical) for(unsigned slot=SCENE_MAP_CLEARCOAT;slot<SCENE_MAP_COUNT;slot++)
        if(material->maps[slot].id) mapped|=1u<<(slot-SCENE_MAP_CLEARCOAT);
    if(material->transmission) mapped|=SCENE_TRANSMISSION_FEATURE;
    mapped|=(unsigned)mesh->geometry->topology<<SCENE_DRAWABLE_SHIFT;
    return scene3d_program(entry,variant,mapped,frame);
}
static bool scene3d_draw_mesh(Scene3dEntry* entry, const Scene3dPass& pass, Scene3dMesh* mesh) {
    Scene3dMaterial* material=mesh->material;
    if(material->shader) return scene3d_draw_shader(entry,pass,mesh);
    bool lit=pass.environment&&(material->standard||material->lambert);
    bool shaded=pass.shadows&&pass.shadows->count&&(material->standard||material->lambert);
    uint64_t* program_frame=nullptr;NativeGlResource program=scene3d_mesh_program(entry,pass,mesh,&program_frame);
    if (!program.id) return false;
    if (*program_frame!=pass.frame) {
        const Scene3dLights* l=pass.lights;float light_count=(float)l->count;
        if (!scene3d_uniform(entry,program,"projection",pass.projection.v,16) || !scene3d_uniform(entry,program,"ambient",l->ambient,3) ||
            !scene3d_uniform(entry,program,"directions[0]",l->directions[0],3,8) || !scene3d_uniform(entry,program,"lights[0]",l->lights[0],3,8) ||
            !scene3d_uniform(entry,program,"light_positions[0]",l->light_positions[0],3,8) ||
            !scene3d_uniform(entry,program,"light_params[0]",l->light_params[0],4,8) ||
            !scene3d_uniform(entry,program,"light_count",&light_count,1) || !scene3d_uniform(entry,program,"exposure",&pass.exposure,1) ||
            !scene3d_uniform(entry,program,"sky",l->sky,3) || !scene3d_uniform(entry,program,"ground",l->ground,3) ||
            !scene3d_uniform(entry,program,"hemisphere",l->hemisphere,3) ||
            !scene3d_uniform(entry,program,"environment_intensity",&pass.environment_intensity,1) ||
            !scene3d_uniform(entry,program,"eye",pass.eye,4) || !scene3d_uniform(entry,program,"view",pass.view.v,16)) return false;
        Scene3dFog empty={};const auto* fog=pass.fog?pass.fog:&empty;Scene3dMatrix inverse_view;
        if(!scene3d_inverse_affine(pass.view,&inverse_view)||!scene3d_uniform(entry,program,"fog_params",fog->params,4)||
            !scene3d_uniform(entry,program,"fog_height",fog->height,2)||!scene3d_uniform(entry,program,"fog_color",fog->color,3)||
            !scene3d_uniform(entry,program,"fog_eye",inverse_view.v+12,3)) return false;
        if(mesh->geometry->topology!=SCENE_TRIANGLES&&!scene3d_uniform(entry,program,"logical_viewport",pass.viewport,2)) return false;
        float captured=pass.transmission.id?1.f:0.f;
        if(!scene3d_uniform(entry,program,"transmission_projection",pass.projection.v,16)||
            !scene3d_uniform(entry,program,"transmission_background",pass.background,3)||
            !scene3d_uniform(entry,program,"transmission_captured",&captured,1)) return false;
        if (shaded) {
            // unused slots keep zero matrices and are never read: no light maps to them
            float matrices[SCENE_SHADOW_LIMIT][16]={},params[SCENE_SHADOW_LIMIT][4]={},sizes[SCENE_SHADOW_LIMIT]={},ranges[SCENE_SHADOW_LIMIT][2]={},cascade_ranges[SCENE_SHADOW_LIMIT][4]={},slots[8],counts[8]={};
            for (float& slot:slots) slot=-1;
            for (unsigned s=0;s<pass.shadows->count;s++) {
                const Scene3dShadow* shadow=&pass.shadows->slots[s];
                memcpy(matrices[s],shadow->matrix,sizeof(matrices[s]));memcpy(params[s],shadow->params,sizeof(params[s]));
                memcpy(ranges[s],shadow->range,sizeof(ranges[s]));sizes[s]=(float)shadow->size;
                if(slots[shadow->light]<0) slots[shadow->light]=(float)s;
                counts[shadow->light]=(float)shadow->cascades;cascade_ranges[s][0]=shadow->cascade_near;cascade_ranges[s][1]=shadow->cascade_far;cascade_ranges[s][2]=shadow->cascade_transition;
            }
            if (!scene3d_uniform(entry,program,"shadow_matrices[0]",matrices[0],16,SCENE_SHADOW_LIMIT) ||
                !scene3d_uniform(entry,program,"shadow_params[0]",params[0],4,SCENE_SHADOW_LIMIT) ||
                !scene3d_uniform(entry,program,"shadow_sizes[0]",sizes,1,SCENE_SHADOW_LIMIT) ||
                !scene3d_uniform(entry,program,"shadow_ranges[0]",ranges[0],2,SCENE_SHADOW_LIMIT) ||
                !scene3d_uniform(entry,program,"light_shadows[0]",slots,1,8)||
                !scene3d_uniform(entry,program,"light_cascades[0]",counts,1,8)||
                !scene3d_uniform(entry,program,"cascade_ranges[0]",cascade_ranges[0],4,SCENE_SHADOW_LIMIT)||
                !scene3d_uniform(entry,program,"cascade_view",pass.view.v,16)) return false;
        }
        *program_frame=pass.frame;
    }
    float lambert=material->lambert?1:0,textured=material->maps[SCENE_MAP_BASE].id?1:0,opaque=material->force_opaque?1:0;
    float maps[4]={material->maps[SCENE_MAP_NORMAL].id?1.f:0.f,material->normal_scale,material->occlusion_strength,scene3d_tangent_compatible(mesh,SCENE_MAP_NORMAL)?1.f:0.f};
    float skinned=mesh->bone_count?1:0,colored=mesh->geometry->has_colors?1:0,receive=mesh->receive_shadow?1:0,back_side=material->side==1?1:0;
    if (!scene3d_bind_outdoor(entry,program,material) || !scene3d_bind_uv(entry,program,material) || !scene3d_uniform(entry,program,"model",mesh->world.v,16) || !scene3d_uniform(entry,program,"material",material->color,4) ||
        !scene3d_uniform(entry,program,"receive",&receive,1) || !scene3d_uniform(entry,program,"back_side",&back_side,1) ||
        !scene3d_uniform(entry,program,"lambert",&lambert,1) || !scene3d_uniform(entry,program,"textured",&textured,1) ||
        !scene3d_uniform(entry,program,"opaque",&opaque,1) || !scene3d_uniform(entry,program,"emissive",material->emissive,3) ||
        !scene3d_uniform(entry,program,"pbr",material->pbr,2) || !scene3d_uniform(entry,program,"alpha_cutoff",&material->alpha_cutoff,1) ||
        !scene3d_uniform(entry,program,"maps",maps,4) || !scene3d_uniform(entry,program,"skinned",&skinned,1) ||
        !scene3d_uniform(entry,program,"colored",&colored,1) || !scene3d_uniform(entry,program,"morph_weights",mesh->morph_weights,2))
        return false;
    if(mesh->geometry->topology!=SCENE_TRIANGLES) {
        float screen=mesh->screen_size?1.f:0.f,source_uvs=mesh->geometry->source_uvs?1.f:0.f;
        if(!scene3d_uniform(entry,program,"drawable_size",mesh->drawable_size,2)||!scene3d_uniform(entry,program,"drawable_center",mesh->drawable_center,2)||
            !scene3d_uniform(entry,program,"drawable_screen",&screen,1)||!scene3d_uniform(entry,program,"drawable_source_uvs",&source_uvs,1)) return false;
    }
    if(material->physical) {
        const float* values=material->physical_values;
        float layers[]={values[SCENE_COAT_FACTOR],values[SCENE_COAT_ROUGHNESS],values[SCENE_SHEEN_FACTOR],values[SCENE_SHEEN_ROUGHNESS]};
        float normal[]={material->maps[SCENE_MAP_CLEARCOAT_NORMAL].id?1.f:0.f,values[SCENE_COAT_NORMAL_SCALE],scene3d_tangent_compatible(mesh,SCENE_MAP_CLEARCOAT_NORMAL)?1.f:0.f};
        if(!scene3d_uniform(entry,program,"layer_params",layers,4)||!scene3d_uniform(entry,program,"coat_normal_params",normal,3)||
            !scene3d_uniform(entry,program,"dielectric_reflectance",&material->dielectric,1)) return false;
        for(unsigned i=0;i<SCENE_PHYSICAL_COLORS;i++) if(!scene3d_uniform(entry,program,scene3d_physical_colors[i].sampler,material->physical_colors[i],3)) return false;
        float transmission[]={values[SCENE_TRANSMISSION],values[SCENE_IOR],values[SCENE_THICKNESS],values[SCENE_ATTENUATION_DISTANCE]};
        if(!scene3d_uniform(entry,program,"transmission_params",transmission,4)) return false;
    }
    if (mesh->bone_count && !native_gl_data_texture_update(entry->graphics,mesh->palette,mesh->bone_matrices[0].v,mesh->bone_count)) return false;
    NativeGlDraw draw={program,mesh->vertices,
        mesh->geometry->index_count?mesh->geometry->index_count:mesh->geometry->vertex_count,mesh->instance_count,
        mesh->geometry->index_count!=0,scene3d_transparent(mesh),material->side,mesh->clockwise};
    draw.depth_only=material->depth_only;
    if(material->transmission) draw.bindings[draw.binding_count++]={"opaque_color",pass.transmission.id?pass.transmission:entry->white,
        pass.transmission.id?NATIVE_GL_BIND_FLOAT:NATIVE_GL_BIND_TEXTURE};
    draw.bindings[draw.binding_count++]={"image",material->maps[SCENE_MAP_BASE].id?material->maps[SCENE_MAP_BASE]:entry->white,NATIVE_GL_BIND_TEXTURE};
    // a conditional palette sampler must never inherit a framebuffer attachment binding.
    draw.bindings[draw.binding_count++]={"palette",mesh->bone_count?mesh->palette:entry->white,NATIVE_GL_BIND_TEXTURE};
    if (material->standard) {
        NativeGlResource dfg=scene3d_dfg(entry);
        if (!dfg.id) return false;
        draw.bindings[draw.binding_count++]={"dfg",dfg,NATIVE_GL_BIND_TEXTURE};
        unsigned count=material->physical?SCENE_MAP_COUNT:SCENE_MAP_CLEARCOAT;
        for(unsigned slot=1;slot<count;slot++) draw.bindings[draw.binding_count++]={scene3d_map_specs[slot].sampler,
            material->maps[slot].id?material->maps[slot]:entry->white,NATIVE_GL_BIND_TEXTURE};
    }
    if (lit) draw.bindings[draw.binding_count++]={"environment",entry->environment_atlas,NATIVE_GL_BIND_FLOAT};
    if (shaded) {
        static const char* samplers[SCENE_SHADOW_LIMIT]={"shadow0","shadow1","shadow2","shadow3"};
        for (unsigned s=0;s<SCENE_SHADOW_LIMIT;s++)
            draw.bindings[draw.binding_count++]={samplers[s],s<pass.shadows->count?entry->shadow_maps[s]:entry->shadow_blank,NATIVE_GL_BIND_SHADOW};
    }
    return native_gl_draw(entry->graphics,&draw);
}

// Everything a capture depends on and no GPU resource identity, so a re-projection that leaves
// the environment as it was keeps its prefiltered atlas.
// A draw record's geometry, pose and material folded into `key` without GPU resource identities:
// what an environment capture or a shadow map drawn from it depends on.
static uint64_t scene3d_mesh_key(uint64_t key, const Scene3dMesh* mesh) {
    auto add=[&key](const void* data, size_t bytes) { key=hash_fnv1a_64_extend(key,data,bytes); };
    const Scene3dMaterial* m=mesh->material;
    add(&mesh->geometry->hash,sizeof(uint64_t));add(&mesh->instance_hash,sizeof(uint64_t));add(&mesh->instance_count,sizeof(unsigned));
    add(mesh->drawable_size,sizeof(mesh->drawable_size));add(mesh->drawable_center,sizeof(mesh->drawable_center));add(&mesh->screen_size,sizeof(mesh->screen_size));
    add(&mesh->authored_bounds,sizeof(bool));add(&mesh->bounds_lo,sizeof(mesh->bounds_lo));add(&mesh->bounds_hi,sizeof(mesh->bounds_hi));
    for(auto* branch=mesh->lod;branch;branch=branch->parent) {
        add(&branch->level,sizeof(branch->level));add(&branch->lod->count,sizeof(branch->lod->count));
        add(branch->lod->distances,branch->lod->count*sizeof(float));add(branch->lod->world.v,sizeof(branch->lod->world.v));
    }
    add(mesh->world.v,sizeof(mesh->world.v));add(&mesh->visible,sizeof(bool));add(mesh->morph_weights,sizeof(mesh->morph_weights));
    if (mesh->bone_count) add(mesh->bone_matrices,mesh->bone_count*sizeof(Scene3dMatrix));
    add(m->color,sizeof(m->color));add(m->emissive,sizeof(m->emissive));add(m->pbr,sizeof(m->pbr));
    add(&m->normal_scale,sizeof(float));add(&m->occlusion_strength,sizeof(float));add(&m->alpha_cutoff,sizeof(float));
    add(m->uv_rows,sizeof(m->uv_rows));add(m->uv_sets,sizeof(m->uv_sets));
    add(m->physical_values,sizeof(m->physical_values));add(m->physical_colors,sizeof(m->physical_colors));
    add(m->outdoor,sizeof(m->outdoor));add(m->foliage_color,sizeof(m->foliage_color));
    add(&m->physical,sizeof(m->physical));add(&m->texture_hash,sizeof(uint64_t));add(&m->side,sizeof(int));
    if (m->shader) {
        add(&mesh->program.id,sizeof(uint64_t));
        for (unsigned i=0;i<m->shader->uniform_count;i++) if (m->shader->uniforms[i].values)
            add(m->shader->uniforms[i].values,m->shader->uniforms[i].length*sizeof(float));
    }
    bool flags[]={m->lambert,m->standard,scene3d_transparent(mesh),m->force_opaque,m->depth_only,m->maps[SCENE_MAP_NORMAL].id!=0,mesh->cast_shadow};
    add(flags,sizeof(flags));
    return key;
}
static uint64_t scene3d_environment_key(const Scene3dProjection* p, Scene3dVec origin) {
    uint64_t key=HASH_FNV1A_64_OFFSET_BASIS;
    auto add=[&key](const void* data, size_t bytes) { key=hash_fnv1a_64_extend(key,data,bytes); };
    const Scene3dLights* l=&p->environment_lights;
    add(&origin,sizeof(origin));add(p->environment_background,sizeof(p->environment_background));
    add(&p->environment_near,sizeof(float));add(&p->environment_far,sizeof(float));
    // the light set's floats and count are contiguous; its trailing flag may be followed by padding
    add(l,offsetof(Scene3dLights,count)+sizeof(l->count));add(&l->has_hemisphere,sizeof(bool));
    add(&p->environment_imaged,sizeof(bool));if (p->environment_imaged) add(&p->environment_image_hash,sizeof(uint64_t));
    for (int i=0;i<p->mesh_count;i++) if (p->meshes[i]->layer==SCENE_LAYER_ENVIRONMENT) key=scene3d_mesh_key(key,p->meshes[i]);
    return key;
}
// face sizes of the atlas levels: halving to 16 texels, then 16-texel roughness levels
static unsigned scene3d_environment_size(unsigned lod) {
    return 1u<<(lod<SCENE_ENV_LOD_MAX-SCENE_ENV_LOD_MIN?SCENE_ENV_LOD_MAX-lod:SCENE_ENV_LOD_MIN);
}
static bool scene3d_environment_setup(Scene3dEntry* entry) {
    NativeGlContext* graphics=entry->graphics;unsigned width=3*SCENE_ENV_SIZE,height=4*SCENE_ENV_SIZE;
    if (!entry->environment_atlas.id) entry->environment_atlas=native_gl_float_target(graphics,width,height,true);
    if (!entry->environment_pingpong.id) entry->environment_pingpong=native_gl_float_target(graphics,width,height,false);
    if (!entry->environment_filter.id) {
        StrBuf* source=strbuf_new();strbuf_append_str(source,"#version 330 core\n");
        strbuf_append_str(source,scene3d_cube_uv);strbuf_append_str(source,scene3d_filter_fragment);
        entry->environment_filter=native_gl_program(graphics,scene3d_filter_vertex,source->str);strbuf_free(source);
    }
    if (!entry->environment_unwrap.id) entry->environment_unwrap=native_gl_program(graphics,scene3d_filter_vertex,scene3d_unwrap_fragment);
    if (!entry->environment_atlas.id || !entry->environment_pingpong.id || !entry->environment_filter.id || !entry->environment_unwrap.id)
        return scene3d_fail(entry,native_gl_diagnostic(graphics));
    for (unsigned lod=0;lod<SCENE_ENV_LODS;lod++) if (!entry->environment_planes[lod].id) {
        // the corners overshoot each face by one texel so the filter also fills the face's border
        float size=(float)scene3d_environment_size(lod),texel=1/(size-2),lo=-texel,hi=1+texel;
        const float uv[12]={lo,lo,hi,lo,hi,hi,lo,lo,hi,hi,lo,hi};float data[6*6*6];
        for (unsigned face=0;face<6;face++) {
            float x=(face%3)*2.f/3-1,y=face>2?0.f:-1.f;
            const float corners[12]={x,y,x+2.f/3,y,x+2.f/3,y+1,x,y,x+2.f/3,y+1,x,y+1};
            for (unsigned vertex=0;vertex<6;vertex++) {
                float u=uv[vertex*2]*2-1,v=uv[vertex*2+1]*2-1,*out=data+(face*6+vertex)*6;
                const float directions[6][3]={{1,v,u},{-u,1,-v},{-u,v,1},{-1,v,-u},{-u,-1,v},{u,v,-1}};
                out[0]=corners[vertex*2];out[1]=corners[vertex*2+1];out[2]=0;memcpy(out+3,directions[face],3*sizeof(float));
            }
        }
        NativeGlResource buffer=native_gl_buffer(graphics,data,sizeof(data),false);
        NativeGlAttribute attributes[2]={{buffer,0,3,6*sizeof(float),0,0},{buffer,1,3,6*sizeof(float),3*sizeof(float),0}};
        entry->environment_planes[lod]=buffer.id?native_gl_vertices(graphics,attributes,2,{}):NativeGlResource{};
        if (!entry->environment_planes[lod].id) return scene3d_fail(entry,native_gl_diagnostic(graphics));
    }
    return true;
}
// Capture the lighting environment from its origin into the atlas's top level, one face per
// region, then GGX-filter each level from the one above (Three.js r186 PMREMGenerator.fromScene
// with no blur). Runs only when the capture key changes, never per frame (D7.2.4).
static bool scene3d_environment_filter(Scene3dEntry* entry, uint64_t key);
static bool scene3d_environment_capture(Scene3dEntry* entry, DomElement* root) {
    Scene3dProjection* p=&entry->projection;
    if (!p->environment.address) return true;
    DomNode* node=dom_node_ref_validate(root->doc,p->environment);Scene3dMatrix world;bool visible;
    if (!node || !node->is_element() || !scene3d_world(entry,node->as_element(),&world,&visible))
        return scene3d_fail(entry,"invalid environment pose or detached environment");
    Scene3dVec origin=scene3d_point(world,{});
    uint64_t key=scene3d_environment_key(p,origin);
    if (key==entry->environment_key && entry->environment_atlas.id) return true;
    if (!scene3d_environment_setup(entry)) return false;
    NativeGlContext* graphics=entry->graphics;const float black[]={0,0,0,1};
    if (!native_gl_begin_region(graphics,entry->environment_atlas,0,0,3*SCENE_ENV_SIZE,4*SCENE_ENV_SIZE,black))
        return scene3d_fail(entry,native_gl_diagnostic(graphics));
    if (p->environment_imaged) {
        // the image fills the top level's six faces at once (_textureToCubeUV)
        NativeGlDraw draw={entry->environment_unwrap,entry->environment_planes[0],36,1,false,false,2,false};
        draw.bindings[draw.binding_count++]={"source",entry->environment_image,NATIVE_GL_BIND_TEXTURE};
        if (!native_gl_begin_region(graphics,entry->environment_atlas,0,0,3*SCENE_ENV_SIZE,2*SCENE_ENV_SIZE,nullptr) ||
            !native_gl_draw(graphics,&draw)) return scene3d_fail(entry,native_gl_diagnostic(graphics));
        return scene3d_environment_filter(entry,key);
    }
    // transparent surfaces composite farthest first as seen from the capture point
    for (int i=0;i<p->mesh_count;i++) {
        Scene3dVec offset=scene3d_sub(scene3d_point(p->meshes[i]->world,p->meshes[i]->geometry->center),origin);
        p->meshes[i]->depth=-sqrtf(scene3d_dot(offset,offset));
    }
    qsort(p->meshes,p->mesh_count,sizeof(Scene3dMesh*),scene3d_draw_compare);
    // Three.js's cube camera: +X, +Y, +Z then -X, -Y, -Z, laid out as two rows of three
    static const float up_sign[6]={1,-1,1,1,1,1},forward_sign[6]={1,1,1,-1,-1,-1};
    Scene3dMatrix lens=scene3d_perspective(90,1,p->environment_near,p->environment_far);
    for (unsigned face=0;face<6;face++) {
        unsigned column=face%3;Scene3dVec forward={},up={};
        if (column==0) { forward.x=forward_sign[face];up.y=up_sign[face]; }
        else if (column==1) { forward.y=forward_sign[face];up.z=up_sign[face]; }
        else { forward.z=forward_sign[face];up.y=up_sign[face]; }
        Scene3dPass pass={&p->environment_lights,{},{},{origin.x,origin.y,origin.z,1},1,0,0,false,nullptr,++entry->frame};
        if (!scene3d_camera(origin,{origin.x+forward.x,origin.y+forward.y,origin.z+forward.z},up,&pass.view))
            return scene3d_fail(entry,"degenerate environment capture camera");
        pass.projection=scene3d_multiply(lens,pass.view);pass.clip=lens;
        memcpy(pass.background,p->environment_background,sizeof(pass.background));pass.viewport[0]=pass.viewport[1]=SCENE_ENV_SIZE;
        if (!native_gl_begin_region(graphics,entry->environment_atlas,column*SCENE_ENV_SIZE,face>2?SCENE_ENV_SIZE:0,
            SCENE_ENV_SIZE,SCENE_ENV_SIZE,p->environment_background)) return scene3d_fail(entry,native_gl_diagnostic(graphics));
        for (int i=0;i<p->mesh_count;i++) {
            Scene3dMesh* mesh=p->meshes[i];
            if(!mesh->visible||mesh->layer!=SCENE_LAYER_ENVIRONMENT) continue;
            if(!scene3d_lod_accepts(mesh,origin,1)) {entry->stats.lod_skips++;continue;}
            if(scene3d_mesh_in_view(entry,mesh,lens,pass.view,pass.viewport)&&!scene3d_draw_mesh(entry,pass,mesh))
                return scene3d_fail(entry,native_gl_diagnostic(graphics));
        }
    }
    return scene3d_environment_filter(entry,key);
}
// GGX-filter each atlas level from the one above (Three.js r186 _applyPMREM), then record `key`.
static bool scene3d_environment_filter(Scene3dEntry* entry, uint64_t key) {
    NativeGlContext* graphics=entry->graphics;
    NativeGlUniform roughness_uniform=native_gl_uniform(graphics,entry->environment_filter,"roughness");
    NativeGlUniform mip_uniform=native_gl_uniform(graphics,entry->environment_filter,"mip_int");
    for (unsigned lod=1;lod<SCENE_ENV_LODS;lod++) {
        // each level filters the one above by the roughness it adds, widened as Three.js widens it
        float target=lod/(float)(SCENE_ENV_LODS-1),source=(lod-1)/(float)(SCENE_ENV_LODS-1);
        float roughness=sqrtf(target*target-source*source)*target*1.25f;
        unsigned size=scene3d_environment_size(lod),x=3*size*(lod>SCENE_ENV_LOD_MAX-SCENE_ENV_LOD_MIN?lod-SCENE_ENV_LOD_MAX+SCENE_ENV_LOD_MIN:0);
        unsigned y=4*(SCENE_ENV_SIZE-size);
        // filter into the ping-pong target, then copy back (roughness 0) into the same region
        NativeGlResource passes[2][2]={{entry->environment_pingpong,entry->environment_atlas},{entry->environment_atlas,entry->environment_pingpong}};
        for (unsigned step=0;step<2;step++) {
            float pass_roughness=step?0:roughness,mip=(float)((int)SCENE_ENV_LOD_MAX-(int)lod+(step?0:1));
            NativeGlDraw draw={entry->environment_filter,entry->environment_planes[lod],36,1,false,false,2,false};
            draw.bindings[draw.binding_count++]={"source",passes[step][1],NATIVE_GL_BIND_FLOAT};
            if (!native_gl_begin_region(graphics,passes[step][0],x,y,3*size,2*size,nullptr) ||
                !native_gl_uniform_set(graphics,roughness_uniform,&pass_roughness,1) || !native_gl_uniform_set(graphics,mip_uniform,&mip,1) ||
                !native_gl_draw(graphics,&draw)) return scene3d_fail(entry,native_gl_diagnostic(graphics));
        }
    }
    entry->environment_key=key;entry->stats.environment_builds++;
    return true;
}
// The extent of a draw record (every instance; every bone's reach for a skinned mesh) in `view`
// space, grown into `lo`..`hi`.
static bool scene3d_mesh_bounds(const Scene3dMesh* mesh, const Scene3dMatrix& view, Scene3dVec* lo, Scene3dVec* hi,float* maximum_scale=nullptr) {
    unsigned instances=mesh->instance_matrices?mesh->instance_count:1,poses=!mesh->authored_bounds&&mesh->bone_count?mesh->bone_count:1;
    Scene3dVec a=mesh->authored_bounds?mesh->bounds_lo:mesh->geometry->lo,b=mesh->authored_bounds?mesh->bounds_hi:mesh->geometry->hi;
    if(!mesh->authored_bounds&&mesh->material->outdoor[SCENE_WIND_STRENGTH]>0&&!mesh->bone_count&&!mesh->geometry->morph_count) {
        const float* wind=mesh->material->outdoor;float h=fmaxf(b.y,0)/wind[SCENE_WIND_HEIGHT];
        float reach=wind[SCENE_WIND_STRENGTH]*h*h;a.x-=reach;a.z-=reach;b.x+=reach;b.z+=reach;
    }
    bool valid=true;
    for (unsigned k=0;k<instances;k++) for (unsigned pose=0;pose<poses;pose++) {
        Scene3dMatrix to=scene3d_multiply(view,mesh->world);
        if (mesh->instance_matrices) { Scene3dMatrix instance;memcpy(instance.v,mesh->instance_matrices+k*16,sizeof(instance.v));to=scene3d_multiply(to,instance); }
        if (!mesh->authored_bounds&&mesh->bone_count) to=scene3d_multiply(to,mesh->bone_matrices[pose]);
        if(maximum_scale) for(unsigned c=0;c<3;c++) *maximum_scale=fmaxf(*maximum_scale,sqrtf(to.v[c*4]*to.v[c*4]+to.v[c*4+1]*to.v[c*4+1]+to.v[c*4+2]*to.v[c*4+2]));
        for (unsigned corner=0;corner<8;corner++) {
            Scene3dVec q=scene3d_point(to,{corner&1?b.x:a.x,corner&2?b.y:a.y,corner&4?b.z:a.z});
            valid&=isfinite(q.x)&&isfinite(q.y)&&isfinite(q.z);scene3d_include(q,lo,hi);
        }
    }
    return valid;
}
static bool scene3d_mesh_in_view(Scene3dEntry* entry,const Scene3dMesh* mesh,const Scene3dMatrix& lens,const Scene3dMatrix& view,const float viewport[2]) {
    if(!entry->projection.frustum_culling||!mesh->frustum_culling) return true;
    // undeformed bounds cannot prove visibility for arbitrary skin, morph or shader displacement.
    if(!mesh->authored_bounds&&(mesh->bone_count||mesh->geometry->morph_count||mesh->material->shader)) return true;
    entry->stats.visibility_tests++;
    Scene3dVec lo={FLT_MAX,FLT_MAX,FLT_MAX},hi={-FLT_MAX,-FLT_MAX,-FLT_MAX};float scale=0,mx=0,my=0;
    if(!scene3d_mesh_bounds(mesh,view,&lo,&hi,&scale)) return true;
    auto kind=mesh->geometry->topology;
    if(kind!=SCENE_TRIANGLES) {
        bool line=kind==SCENE_LINE||kind==SCENE_SEGMENTS;
        float x=line?mesh->drawable_size[0]*.5f:mesh->drawable_size[0]*mesh->geometry->maximum_point_size*fmaxf(fabsf(mesh->drawable_center[0]),fabsf(1-mesh->drawable_center[0]));
        float y=line?x:mesh->drawable_size[1]*mesh->geometry->maximum_point_size*fmaxf(fabsf(mesh->drawable_center[1]),fabsf(1-mesh->drawable_center[1]));
        if(mesh->screen_size) {mx=2*x/viewport[0];my=2*y/viewport[1];}
        else {float radius=(x+y)*scale;lo.x-=radius;lo.y-=radius;hi.x+=radius;hi.y+=radius;}
    }
    bool visible=scene3d_clip_bounds(lens,lo,hi,mx,my);if(!visible) entry->stats.culled_draws++;
    return visible;
}
static bool scene3d_casts(const Scene3dMesh* mesh) {
    // blended (glass) and depth-only surfaces cast nothing; the depth pass cannot run a shader material's vertex stage
    return mesh->layer==SCENE_LAYER_SCENE && mesh->visible && mesh->cast_shadow && !scene3d_transparent(mesh) &&
        !mesh->material->depth_only && !mesh->material->shader && scene3d_lod_accepts(mesh,{},2);
}
// The extent of the casters and receivers in `view` space; an empty scene is a point ahead.
static void scene3d_shadow_bounds(const Scene3dProjection* p, const Scene3dMatrix& view, Scene3dVec* lo, Scene3dVec* hi) {
    *lo={FLT_MAX,FLT_MAX,FLT_MAX};*hi={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for (int i=0;i<p->mesh_count;i++) {
        const Scene3dMesh* mesh=p->meshes[i];
        bool receives=mesh->layer==SCENE_LAYER_SCENE && mesh->visible && mesh->receive_shadow && (mesh->material->lambert || mesh->material->standard)&&scene3d_lod_accepts(mesh,{},2);
        if (scene3d_casts(mesh) || receives) scene3d_mesh_bounds(mesh,view,lo,hi);
    }
    if (lo->x>hi->x) *lo=*hi={0,0,-1};
}
// A point light's six faces as Three.js r186 renders them (_cubeDirections, _cubeUps): 90 degree
// views in cube-map face order, reaching `distance` or the farthest caster or receiver.
static void scene3d_point_frame(const Scene3dProjection* p, Scene3dShadow* shadow) {
    static const Scene3dVec forward[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    static const Scene3dVec up[6]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    Scene3dMatrix offset=scene3d_identity();
    offset.v[12]=-shadow->origin.x;offset.v[13]=-shadow->origin.y;offset.v[14]=-shadow->origin.z;
    Scene3dVec lo,hi;scene3d_shadow_bounds(p,offset,&lo,&hi);
    // the cube faces' depth is the offset's largest component
    float reach=fmaxf(fmaxf(fmaxf(fabsf(lo.x),fabsf(hi.x)),fmaxf(fabsf(lo.y),fabsf(hi.y))),fmaxf(fabsf(lo.z),fabsf(hi.z)));
    float far_plane=shadow->distance>0?shadow->distance:reach+fmaxf(.01f*reach,.01f),near_plane=fmaxf(far_plane*.001f,.001f);
    Scene3dMatrix lens=scene3d_perspective(90,1,near_plane,far_plane);
    for (unsigned face=0;face<6;face++) {
        Scene3dMatrix view;Scene3dVec o=shadow->origin;
        scene3d_camera(o,{o.x+forward[face].x,o.y+forward[face].y,o.z+forward[face].z},up[face],&view);
        memcpy(shadow->clip[face],scene3d_multiply(lens,view).v,sizeof(shadow->clip[face]));
    }
    memcpy(shadow->matrix,offset.v,sizeof(shadow->matrix));shadow->range[0]=near_plane;shadow->range[1]=far_plane;
}
// Fit a casting light's camera as Three.js r186 places it (at the light, looking at its target, up
// +y), its frustum around the casters and receivers unless the light gives an extent or reach.
static void scene3d_shadow_frame(const Scene3dProjection* p, Scene3dShadow* shadow) {
    if (shadow->kind==1) { scene3d_point_frame(p,shadow);return; }
    Scene3dVec z=scene3d_sub(shadow->origin,shadow->target),up={0,1,0};
    if (!scene3d_normalize(&z)) z={0,0,1};
    // Matrix4.lookAt nudges a view parallel to up
    if (fabsf(fabsf(z.y)-1)<1e-7f) { z.z+=.0001f;scene3d_normalize(&z); }
    Scene3dMatrix view;scene3d_camera(shadow->origin,scene3d_sub(shadow->origin,z),up,&view);
    Scene3dVec lo,hi;scene3d_shadow_bounds(p,view,&lo,&hi);
    // view space looks down -z; a 1% margin keeps the nearest and farthest surfaces inside
    float near_plane=-hi.z,far_plane=-lo.z,margin=fmaxf(.01f*(far_plane-near_plane),.01f);
    Scene3dMatrix lens;
    if (shadow->kind==2) {
        far_plane=shadow->distance>0?shadow->distance:far_plane+margin;
        near_plane=fminf(fmaxf(near_plane-margin,far_plane*.001f),far_plane*.999f);
        lens=scene3d_perspective(shadow->angle*2*57.29577951308232f,1,near_plane,far_plane);
    } else {
        float l=lo.x,r=hi.x,b=lo.y,t=hi.y;
        if (shadow->extent>0) { l=b=-shadow->extent;r=t=shadow->extent; }
        if (r-l<1e-4f) { l-=.5f;r+=.5f; }
        if (t-b<1e-4f) { b-=.5f;t+=.5f; }
        near_plane-=margin;far_plane+=margin;lens={};
        lens.v[0]=2/(r-l);lens.v[5]=2/(t-b);lens.v[10]=-2/(far_plane-near_plane);
        lens.v[12]=-(r+l)/(r-l);lens.v[13]=-(t+b)/(t-b);lens.v[14]=-(far_plane+near_plane)/(far_plane-near_plane);lens.v[15]=1;
    }
    Scene3dMatrix clip=scene3d_multiply(lens,view),bias=scene3d_identity();
    // clip space to the map's 0..1 texture and depth coordinates
    bias.v[0]=bias.v[5]=bias.v[10]=.5f;bias.v[12]=bias.v[13]=bias.v[14]=.5f;
    memcpy(shadow->clip[0],clip.v,sizeof(shadow->clip[0]));memcpy(shadow->matrix,scene3d_multiply(bias,clip).v,sizeof(shadow->matrix));
}
static float scene3d_cascade_split(const Scene3dProjection* p,const Scene3dShadow* shadow,unsigned split) {
    float near=p->near_plane,far=fminf(p->far_plane,shadow->cascade_distance),f=(float)split/(float)shadow->cascades;
    float linear=near+(far-near)*f;
    return near>0?linear*(1-shadow->cascade_lambda)+near*powf(far/near,f)*shadow->cascade_lambda:linear;
}
// stable spherical fits keep each camera slice's texel scale independent of camera rotation.
static bool scene3d_cascade_frame(Scene3dEntry* entry,Scene3dShadow* shadow,const Scene3dMatrix& view,const Scene3dMatrix& lens) {
    auto* p=&entry->projection;Scene3dMatrix inverse;
    if(!scene3d_inverse_affine(view,&inverse)||shadow->cascade_distance<=p->near_plane) return scene3d_fail(entry,"shadow distance must exceed camera near plane");
    shadow->cascade_near=scene3d_cascade_split(p,shadow,shadow->cascade);
    shadow->cascade_far=scene3d_cascade_split(p,shadow,shadow->cascade+1);
    float previous=shadow->cascade?scene3d_cascade_split(p,shadow,shadow->cascade-1):shadow->cascade_near;
    float near=shadow->cascade_near-(shadow->cascade_near-previous)*shadow->cascade_transition;
    Scene3dVec corners[8],center={};
    for(unsigned i=0;i<8;i++) {
        float z=-(i&4?shadow->cascade_far:near),w=lens.v[11]*z+lens.v[15];
        Scene3dVec q={(float(i&1?1:-1)*w-lens.v[8]*z-lens.v[12])/lens.v[0],
            (float(i&2?1:-1)*w-lens.v[9]*z-lens.v[13])/lens.v[5],z};
        corners[i]=scene3d_point(inverse,q);center=scene3d_add(center,corners[i]);
    }
    center=scene3d_scale(center,0.125f);float radius=0;
    for(const auto& corner:corners) {auto d=scene3d_sub(corner,center);radius=fmaxf(radius,sqrtf(scene3d_dot(d,d)));}
    // quantize roundoff in the sphere radius and reserve the PCF footprint before texel snapping.
    radius=ceilf(radius*16)/16;radius*=1+2*(shadow->params[3]+1)/(float)shadow->size;
    Scene3dVec z=scene3d_sub(shadow->origin,shadow->target);if(!scene3d_normalize(&z)) return false;
    if(fabsf(fabsf(z.y)-1)<1e-7f) {z.z+=.0001f;scene3d_normalize(&z);}
    Scene3dMatrix light_view;if(!scene3d_camera({},scene3d_scale(z,-1),{0,1,0},&light_view)) return false;
    Scene3dVec middle=scene3d_point(light_view,center),lo,hi;scene3d_shadow_bounds(p,light_view,&lo,&hi);
    float texel=2*radius/(float)shadow->size;middle.x=roundf(middle.x/texel)*texel;middle.y=roundf(middle.y/texel)*texel;
    float margin=fmaxf(.01f*(hi.z-lo.z),.01f);
    auto clip=scene3d_orthographic(2*radius,1,-hi.z-margin,-lo.z+margin);clip.v[12]=-middle.x/radius;clip.v[13]=-middle.y/radius;
    clip=scene3d_multiply(clip,light_view);auto bias=scene3d_identity();
    bias.v[0]=bias.v[5]=bias.v[10]=.5f;bias.v[12]=bias.v[13]=bias.v[14]=.5f;
    memcpy(shadow->clip[0],clip.v,sizeof(clip.v));memcpy(shadow->matrix,scene3d_multiply(bias,clip).v,sizeof(shadow->matrix));return true;
}
// The depth programs of shadow passes: the scene's vertex stage, and a fragment stage that only
// cuts out masked texels.
static const char* scene3d_shadow_fragment = R"GLSL(
in vec4 vertex_color;
uniform vec4 material;
uniform float textured, alpha_cutoff;
uniform sampler2D image;
void main() {
#ifdef MASK
    float alpha = material.a * vertex_color.a * (textured > 0.5 ? texture(image, map_uv(UV_image)).a : 1.0);
    if (alpha < alpha_cutoff) discard;
#endif
}
)GLSL";
// Render each casting light's map: casters only, their far faces as Three.js draws them, no polygon
// offset (bias is the light's). A map re-renders only when its frame or a caster changes, so camera
// moves never redraw it.
static bool scene3d_shadow_passes(Scene3dEntry* entry,const Scene3dMatrix& view,const Scene3dMatrix& lens) {
    Scene3dProjection* p=&entry->projection;NativeGlContext* graphics=entry->graphics;
    if (!p->shadows.count) return true;
    if (!entry->shadow_blank.id) {
        entry->shadow_blank=native_gl_depth_target(graphics,1,false);
        if (!entry->shadow_blank.id || !native_gl_begin_layer(graphics,entry->shadow_blank,0,0,nullptr)) return scene3d_fail(entry,native_gl_diagnostic(graphics));
    }
    for (unsigned mask=0;mask<2;mask++) if (!entry->shadow_programs[mask].id) {
        StrBuf* vertex=strbuf_new();strbuf_append_str(vertex,"#version 330 core\n");scene3d_append_vertex_source(vertex);
        StrBuf* fragment=strbuf_new();strbuf_append_str(fragment,mask?"#version 330 core\n#define MASK\n":"#version 330 core\n");
        scene3d_append_uv_source(fragment);strbuf_append_str(fragment,scene3d_shadow_fragment);
        entry->shadow_programs[mask]=native_gl_program(graphics,vertex->str,fragment->str);strbuf_free(vertex);strbuf_free(fragment);
        if (!entry->shadow_programs[mask].id) return scene3d_fail(entry,native_gl_diagnostic(graphics));
    }
    for (unsigned s=0;s<p->shadows.count;s++) {
        Scene3dShadow* shadow=&p->shadows.slots[s];
        if(shadow->cascades>1) {if(!scene3d_cascade_frame(entry,shadow,view,lens)) return false;} else scene3d_shadow_frame(p,shadow);
        bool cube=shadow->kind==1;unsigned faces=cube?6:1;
        uint64_t key=hash_fnv1a_64_extend(HASH_FNV1A_64_OFFSET_BASIS,shadow->clip,faces*sizeof(shadow->clip[0]));
        for (int i=0;i<p->mesh_count;i++) if (scene3d_casts(p->meshes[i])) key=scene3d_mesh_key(key,p->meshes[i]);
        if (!entry->shadow_maps[s].id || entry->shadow_sizes[s]!=shadow->size || entry->shadow_cubes[s]!=cube) {
            native_gl_release(graphics,entry->shadow_maps[s]);
            entry->shadow_maps[s]=native_gl_depth_target(graphics,shadow->size,cube);
            entry->shadow_sizes[s]=shadow->size;entry->shadow_cubes[s]=cube;entry->shadow_keys[s]=0;
            if (!entry->shadow_maps[s].id) return scene3d_fail(entry,native_gl_diagnostic(graphics));
        }
        if (key==entry->shadow_keys[s]) continue;
        for (unsigned face=0;face<faces;face++) {
            if (!native_gl_begin_layer(graphics,entry->shadow_maps[s],face,0,nullptr)) return scene3d_fail(entry,native_gl_diagnostic(graphics));
            for (int i=0;i<p->mesh_count;i++) {
                Scene3dMesh* mesh=p->meshes[i];Scene3dMaterial* material=mesh->material;
                if (!scene3d_casts(mesh)) continue;
                Scene3dMatrix clip;memcpy(clip.v,shadow->clip[face],sizeof(clip.v));const float viewport[]={(float)shadow->size,(float)shadow->size};
                if(!scene3d_mesh_in_view(entry,mesh,clip,scene3d_identity(),viewport)) continue;
                NativeGlResource program=entry->shadow_programs[material->alpha_cutoff>0?1:0];
                float textured=material->maps[SCENE_MAP_BASE].id?1:0,skinned=mesh->bone_count?1:0,colored=mesh->geometry->has_colors?1:0;
                if (!scene3d_bind_outdoor(entry,program,material) || !scene3d_bind_uv(entry,program,material,true) || !scene3d_uniform(entry,program,"projection",shadow->clip[face],16) || !scene3d_uniform(entry,program,"model",mesh->world.v,16) ||
                    !scene3d_uniform(entry,program,"material",material->color,4) || !scene3d_uniform(entry,program,"textured",&textured,1) ||
                    !scene3d_uniform(entry,program,"alpha_cutoff",&material->alpha_cutoff,1) || !scene3d_uniform(entry,program,"skinned",&skinned,1) ||
                    !scene3d_uniform(entry,program,"colored",&colored,1) || !scene3d_uniform(entry,program,"morph_weights",mesh->morph_weights,2) ||
                    (mesh->bone_count && !native_gl_data_texture_update(graphics,mesh->palette,mesh->bone_matrices[0].v,mesh->bone_count)))
                    return scene3d_fail(entry,native_gl_diagnostic(graphics));
                // the opposite side: front-faced materials write their far faces, so lit surfaces never shadow themselves
                NativeGlDraw draw={program,mesh->vertices,
                    mesh->geometry->index_count?mesh->geometry->index_count:mesh->geometry->vertex_count,mesh->instance_count,
                    mesh->geometry->index_count!=0,false,material->side==2?2:1-material->side,mesh->clockwise};
                draw.bindings[draw.binding_count++]={"image",material->maps[SCENE_MAP_BASE].id?material->maps[SCENE_MAP_BASE]:entry->white,NATIVE_GL_BIND_TEXTURE};
                draw.bindings[draw.binding_count++]={"palette",mesh->bone_count?mesh->palette:entry->white,NATIVE_GL_BIND_TEXTURE};
                if (!native_gl_draw(graphics,&draw)) return scene3d_fail(entry,native_gl_diagnostic(graphics));
            }
        }
        entry->shadow_keys[s]=key;entry->stats.shadow_passes++;
    }
    return true;
}
// the only display conversion: resolve linear radiance, tone-map straight color, then composite the background.
static bool scene3d_post_prepare(Scene3dEntry* entry, unsigned width, unsigned height) {
    Scene3dProjection* p=&entry->projection;
    unsigned needs=(p->linear_effects?1u:0u)|(p->display_effects?4u:0u);
    for (unsigned i=0;i<p->effect_count;i++) if (p->effects[i].kind==SCENE_EFFECT_BLOOM) needs|=2;
    unsigned programs=needs&2?7u:0u;
    for (unsigned i=0;i<p->effect_count;i++) if (p->effects[i].kind==SCENE_EFFECT_FXAA) programs|=8;
    for (unsigned i=0;i<4;i++) if (!(programs&(1u<<i)) && entry->post_programs[i].id) {
        native_gl_release(entry->graphics,entry->post_programs[i]);entry->post_programs[i]={};
    }
    if (entry->post_width==width && entry->post_height==height && entry->post_needs==needs) return true;
    NativeGlResource staged[3][2]={};bool valid=true;
    for (unsigned group=0;group<3 && valid;group++) if (needs&(1u<<group))
        for (unsigned i=0;i<2 && valid;i++) {
            staged[group][i]=group==2?native_gl_target(entry->graphics,width,height,1):
                native_gl_float_target(entry->graphics,group==1?(width+1)/2:width,group==1?(height+1)/2:height,false);
            valid=staged[group][i].id!=0;
        }
    if (!valid) {
        for (auto& group:staged) for (auto resource:group) native_gl_release(entry->graphics,resource);
        return scene3d_fail(entry,native_gl_diagnostic(entry->graphics));
    }
    NativeGlResource* groups[]={entry->post_linear,entry->post_blur,entry->post_display};
    for (unsigned group=0;group<3;group++) for (unsigned i=0;i<2;i++) {
        native_gl_release(entry->graphics,groups[group][i]);groups[group][i]=staged[group][i];
    }
    entry->post_width=width;entry->post_height=height;entry->post_needs=needs;return true;
}
static NativeGlResource scene3d_post_program(Scene3dEntry* entry, unsigned kind) {
    NativeGlResource* program=&entry->post_programs[kind];if (program->id) return *program;
    const char* bodies[]={scene3d_post_threshold,scene3d_post_blur,scene3d_post_merge,scene3d_post_fxaa};
    StrBuf* source=strbuf_create(scene3d_post_header);strbuf_append_str(source,bodies[kind]);
    *program=scene3d_fullscreen_program(entry,source->str,"#version 330 core\n");strbuf_free(source);
    if (!program->id) scene3d_fail(entry,native_gl_diagnostic(entry->graphics));return *program;
}
static bool scene3d_post_pass(Scene3dEntry* entry, NativeGlResource program, NativeGlResource input,
        NativeGlBindingKind input_kind, NativeGlResource output, unsigned width, unsigned height, bool display,
        Scene3dEffect* effect=nullptr, NativeGlResource glow={}, bool counted=true) {
    const float clear[4]={0,0,0,0};float resolution[2]={(float)width,(float)height};
    if (!program.id || !scene3d_fullscreen(entry) || !(display?native_gl_begin(entry->graphics,output,clear):
        native_gl_begin_region(entry->graphics,output,0,0,width,height,nullptr)) ||
        !scene3d_uniform(entry,program,"resolution",resolution,2)) return false;
    NativeGlDraw draw={program,entry->fullscreen_triangle,3,1,false,false,2,false};draw.depth_test=false;
    draw.bindings[draw.binding_count++]={"color_input",input,input_kind};
    if (glow.id) draw.bindings[draw.binding_count++]={"glow_input",glow,NATIVE_GL_BIND_FLOAT};
    if (effect && !scene3d_bind_uniforms(entry,program,effect->uniforms,effect->uniform_count,&draw)) return false;
    if (!native_gl_draw(entry->graphics,&draw)) return false;
    if(counted) entry->stats.postprocess_passes++;return true;
}
// transmission and image effects share the checked full-screen color-copy path.
static bool scene3d_transmission_prepare(Scene3dEntry* entry,unsigned width,unsigned height,bool needed) {
    if(!needed) {
        native_gl_release(entry->graphics,entry->transmission_target);entry->transmission_target={};
        native_gl_release(entry->graphics,entry->transmission_copy);entry->transmission_copy={};
        entry->transmission_width=entry->transmission_height=0;return true;
    }
    if(entry->transmission_target.id&&entry->transmission_width==width&&entry->transmission_height==height) return true;
    NativeGlResource target=native_gl_float_target(entry->graphics,width,height,false),program=entry->transmission_copy;
    if(target.id&&!program.id) program=scene3d_fullscreen_program(entry,
        "#version 330 core\nuniform sampler2D color_input;uniform vec2 resolution;out vec4 fragment;"
        "void main(){fragment=texture(color_input,gl_FragCoord.xy/resolution);}","#version 330 core\n");
    if(!target.id||!program.id) {
        native_gl_release(entry->graphics,target);return scene3d_fail(entry,native_gl_diagnostic(entry->graphics));
    }
    native_gl_release(entry->graphics,entry->transmission_target);entry->transmission_target=target;entry->transmission_copy=program;
    entry->transmission_width=width;entry->transmission_height=height;return true;
}
static bool scene3d_post_linear(Scene3dEntry* entry, DomElement* root, float scale, NativeGlResource* result) {
    unsigned width=entry->post_width,height=entry->post_height,next=0;
    NativeGlBindingKind kind=NATIVE_GL_BIND_TARGET;*result=entry->hdr_target;
    for (unsigned i=0;i<entry->projection.effect_count;i++) {
        Scene3dEffect* effect=&entry->projection.effects[i];if (effect->display) continue;
        NativeGlResource glow={};
        if (effect->kind==SCENE_EFFECT_BLOOM) {
            DomNode* source=dom_node_ref_validate(root->doc,effect->source);float strength,threshold,radius;
            if (!source || !source->is_element() || !scene3d_effect_numbers(entry,source->as_element(),&strength,&threshold,&radius)) return false;
            if (strength==0) continue;
            unsigned w=(width+1)/2,h=(height+1)/2;
            NativeGlResource extract=scene3d_post_program(entry,0),blur=scene3d_post_program(entry,1),merge=scene3d_post_program(entry,2);
            if (!scene3d_uniform(entry,extract,"threshold",&threshold,1) ||
                !scene3d_post_pass(entry,extract,*result,kind,entry->post_blur[0],w,h,false)) return false;
            float direction[2]={radius*scale*w/width,0};
            if (!scene3d_uniform(entry,blur,"direction",direction,2) ||
                !scene3d_post_pass(entry,blur,entry->post_blur[0],NATIVE_GL_BIND_FLOAT,entry->post_blur[1],w,h,false)) return false;
            direction[0]=0;direction[1]=radius*scale*h/height;
            if (!scene3d_uniform(entry,blur,"direction",direction,2) ||
                !scene3d_post_pass(entry,blur,entry->post_blur[1],NATIVE_GL_BIND_FLOAT,entry->post_blur[0],w,h,false) ||
                !scene3d_uniform(entry,merge,"strength",&strength,1)) return false;
            glow=entry->post_blur[0];
            if (!scene3d_post_pass(entry,merge,*result,kind,entry->post_linear[next],width,height,false,nullptr,glow)) return false;
        } else if (!scene3d_post_pass(entry,effect->program,*result,kind,entry->post_linear[next],width,height,false,effect)) return false;
        *result=entry->post_linear[next];kind=NATIVE_GL_BIND_FLOAT;next^=1;
    }
    return true;
}
static bool scene3d_post_display(Scene3dEntry* entry, NativeGlResource* result) {
    unsigned next=0;*result=entry->target;
    for (unsigned i=0;i<entry->projection.effect_count;i++) {
        Scene3dEffect* effect=&entry->projection.effects[i];if (!effect->display) continue;
        NativeGlResource program=effect->kind==SCENE_EFFECT_FXAA?scene3d_post_program(entry,3):effect->program;
        if (!scene3d_post_pass(entry,program,*result,NATIVE_GL_BIND_TARGET,entry->post_display[next],
            entry->post_width,entry->post_height,true,effect)) return false;
        *result=entry->post_display[next];next^=1;
    }
    return true;
}
static bool scene3d_output(Scene3dEntry* entry, Scene3dProjection* projection, NativeGlResource processed) {
    unsigned tone=(unsigned)projection->tone_mapping;
    bool effects=projection->linear_effects!=0;
    NativeGlResource& program=entry->output_programs[tone][effects?1:0];
    if (!program.id) {
        StrBuf* source=strbuf_new();strbuf_append_format(source,"#version 400 core\n#define TONE_MAPPING %u\n#define POSTPROCESSING %u\n",tone,effects?1u:0u);
        strbuf_append_str(source,scene3d_tone_body);strbuf_append_str(source,scene3d_output_fragment);
        program=scene3d_fullscreen_program(entry,source->str,"#version 400 core\n");strbuf_free(source);
        if (!program.id) return scene3d_fail(entry,native_gl_diagnostic(entry->graphics));
    }
    const float clear[4]={0,0,0,0};
    if (!scene3d_fullscreen(entry) || !native_gl_begin(entry->graphics,entry->target,clear) ||
        !scene3d_uniform(entry,program,"background",projection->background,4) ||
        !scene3d_uniform(entry,program,"exposure",&projection->exposure,1)) return false;
    NativeGlDraw draw={program,entry->fullscreen_triangle,3,1,false,false,2,false};
    draw.depth_test=false;draw.bindings[draw.binding_count++]={"color_input",entry->hdr_target,NATIVE_GL_BIND_MULTISAMPLE};
    if (effects) {
        draw.bindings[draw.binding_count++]={"resolved_input",entry->hdr_target,NATIVE_GL_BIND_TARGET};
        draw.bindings[draw.binding_count++]={"processed_input",processed,processed.id==entry->hdr_target.id?NATIVE_GL_BIND_TARGET:NATIVE_GL_BIND_FLOAT};
    }
    return native_gl_draw(entry->graphics,&draw);
}

ImageSurface* scene3d_snapshot(DomElement* root, UiContext* ui, float width, float height, float raster_scale) {
    if (!root || !ui) return nullptr;
    scene3d_collect(root->doc); Scene3dEntry* entry=scene3d_entry(root,true); if (!entry) return nullptr;
    SvgViewBox vb=svg_parse_viewbox(scene3d_text(root,"viewBox"));
    if (!vb.has_viewbox) vb=svg_parse_viewbox(scene3d_text(root,"viewbox"));
    if (!isfinite(width) || !isfinite(height) || !isfinite(raster_scale) || width<=0 || height<=0 || raster_scale<=0 ||
        (vb.has_viewbox && (vb.width==0 || vb.height==0))) {
        scene3d_retire_snapshot(entry);native_gl_release(entry->graphics,entry->target);entry->target={};
        native_gl_release(entry->graphics,entry->hdr_target);entry->hdr_target={};return nullptr;
    }
    float pixel_width=ceilf(width*raster_scale),pixel_height=ceilf(height*raster_scale);
    if (!isfinite(pixel_width) || !isfinite(pixel_height) || pixel_width>4096 || pixel_height>4096)
        { scene3d_fail(entry,"scene raster dimensions exceed quota");scene3d_retire_snapshot(entry);return nullptr; }
    bool changed=entry->mutation_epoch!=root->doc->mutation_epoch || entry->width!=width || entry->height!=height || entry->scale!=raster_scale ||
        entry->animated_generation!=scene3d_animation_generation(entry->animation) ||
        entry->physical_generation!=scene3d_physics_generation(entry->physics);
    if (!changed && entry->snapshot) return entry->snapshot;
    if (!entry->graphics && ui->export_native_graphics && !ui_context_enable_native_graphics(ui)) {
        scene3d_fail(entry,"native graphics host unavailable for export");ui->render_failed=true;return nullptr;
    }
    if (!entry->graphics) entry->graphics=native_gl_create(ui->window!=nullptr,entry->diagnostic,sizeof(entry->diagnostic));
    if (!entry->graphics) return nullptr;
    if (!scene3d_program(entry,0).id) return nullptr;
    if (!entry->white.id) {
        const uint8_t pixels[]={255,255,255,255};ImageSurface white={};
        white.width=white.height=1;white.pitch=4;white.alpha_mode=IMAGE_ALPHA_STRAIGHT;white.pixels=(void*)pixels;
        entry->white=native_gl_texture(entry->graphics,&white);
        if (!entry->white.id) { scene3d_fail(entry,native_gl_diagnostic(entry->graphics));return nullptr; }
    }
    uint64_t source_key=entry->source_key;
    bool reproject=false;
    if (entry->mutation_epoch!=root->doc->mutation_epoch) {
        source_key=5381;unsigned count=0;
        reproject=!scene3d_source_key(root,&source_key,&count) || !entry->projection.pool ||
            entry->mutation_epoch==UINT64_MAX || source_key!=entry->source_key;
    }
    if (reproject && !scene3d_compile(entry,root,ui)) {
        scene3d_projection_destroy(entry);scene3d_retire_snapshot(entry);return nullptr;
    }
    Scene3dProjection* p=&entry->projection;
    bool pose_changed=reproject || entry->animated_generation!=scene3d_animation_generation(entry->animation) ||
        entry->physical_generation!=scene3d_physics_generation(entry->physics);
    entry->diagnostic[0]=0;
    bool valid_pose=pose_changed?scene3d_refresh_pose(entry,root):scene3d_refresh_dynamic(entry,root,true);
    if(!valid_pose) {
        // camera-only refresh must preserve the same validation diagnostic as full projection.
        if (!entry->diagnostic[0]) scene3d_fail(entry,"invalid animated scene pose or detached binding");
        scene3d_retire_snapshot(entry);return nullptr;
    }
    // SH5: GPU simulations catch up with their (just refreshed) time before anything samples them
    if (!scene3d_simulations_advance(entry)) {
        scene3d_fail(entry,native_gl_diagnostic(entry->graphics));scene3d_retire_snapshot(entry);return nullptr;
    }
    if (!entry->target.id || entry->stats.raster_width!=(unsigned)pixel_width || entry->stats.raster_height!=(unsigned)pixel_height) {
        // stage both buffers before retiring the previous size, including their peak live bytes.
        NativeGlResource hdr=native_gl_target(entry->graphics,(unsigned)pixel_width,(unsigned)pixel_height,4,true);
        NativeGlResource output=hdr.id?native_gl_target(entry->graphics,(unsigned)pixel_width,(unsigned)pixel_height,4):NativeGlResource{};
        if (!output.id) {
            native_gl_release(entry->graphics,hdr);scene3d_fail(entry,native_gl_diagnostic(entry->graphics));scene3d_retire_snapshot(entry);return nullptr;
        }
        native_gl_release(entry->graphics,entry->target);native_gl_release(entry->graphics,entry->hdr_target);
        entry->target=output;entry->hdr_target=hdr;
    }
    bool transmission_needed=false,transmission_active=false;
    for(int i=0;i<p->mesh_count;i++) {
        auto* mesh=p->meshes[i];if(mesh->layer!=SCENE_LAYER_SCENE||!mesh->material->transmission) continue;
        transmission_needed=true;
        transmission_active|=mesh->visible&&mesh->material->physical_values[SCENE_TRANSMISSION]>0;
    }
    if (!scene3d_post_prepare(entry,(unsigned)pixel_width,(unsigned)pixel_height)||
        !scene3d_transmission_prepare(entry,(unsigned)pixel_width,(unsigned)pixel_height,transmission_needed)) { scene3d_retire_snapshot(entry);return nullptr; }
    float aspect=p->aspect>0?p->aspect:vb.has_viewbox?vb.width/vb.height:width/height;
    Scene3dVec eye=scene3d_point(p->camera_world,{});
    Scene3dVec target=p->camera_has_target?p->camera_target:scene3d_point(p->camera_world,{0,0,-1});
    Scene3dVec up=p->camera_has_target?p->camera_up:scene3d_point(p->camera_world,{0,1,0},0);
    Scene3dMatrix view;
    if (!scene3d_camera(eye,target,up,&view)) { scene3d_fail(entry,"camera orientation is degenerate");scene3d_retire_snapshot(entry);return nullptr; }
    Scene3dMatrix lens=p->orthographic?scene3d_orthographic(p->view_height,aspect,p->near_plane,p->far_plane):
        scene3d_perspective(p->fov,aspect,p->near_plane,p->far_plane);
    if (vb.has_viewbox) {
        RdtMatrix fit=svg_viewbox_transform(&vb,width,height,scene3d_text(root,"preserveAspectRatio"));
        if (!scene3d_text(root,"preserveAspectRatio")) fit=svg_viewbox_transform(&vb,width,height,scene3d_text(root,"preserveaspectratio"));
        Scene3dMatrix frame=scene3d_identity();
        frame.v[0]=fit.e11*vb.width/width;frame.v[5]=fit.e22*vb.height/height;
        frame.v[12]=fit.e11*vb.width/width+2*fit.e13/width-1;
        frame.v[13]=1-fit.e22*vb.height/height-2*fit.e23/height;
        lens=scene3d_multiply(frame,lens);
    }
    Scene3dMatrix projection=scene3d_multiply(lens,view);
    // the environment capture and shadow maps first: the capture reuses the draw-record order, which
    // the view pass then re-sorts
    bool cascaded=false;for(unsigned i=0;i<p->shadows.count;i++) cascaded|=p->shadows.slots[i].cascades>1;
    if ((pose_changed&&!scene3d_environment_capture(entry,root)) || ((pose_changed||cascaded)&&!scene3d_shadow_passes(entry,view,lens))) { scene3d_retire_snapshot(entry);return nullptr; }
    for (int i=0;i<p->mesh_count;i++) p->meshes[i]->depth=scene3d_point(view,scene3d_point(p->meshes[i]->world,p->meshes[i]->geometry->center)).z;
    qsort(p->meshes,p->mesh_count,sizeof(Scene3dMesh*),scene3d_draw_compare);
    // the view vector for standard materials: toward the camera, a fixed direction when orthographic
    Scene3dVec toward=scene3d_sub(eye,target);scene3d_normalize(&toward);
    Scene3dPass pass={&p->lights,projection,view,
        {p->orthographic?toward.x:eye.x,p->orthographic?toward.y:eye.y,p->orthographic?toward.z:eye.z,p->orthographic?0.f:1.f},
        p->exposure,p->environment_intensity,0,p->environment.address!=nullptr,&p->shadows,++entry->frame,lens};
    pass.fog=&p->fog;
    pass.viewport[0]=width;pass.viewport[1]=height;
    for(int i=0;i<p->lod_count;i++) {
        auto* lod=p->lods[i];auto* choice=lod->choice;
        choice->selected=scene3d_lod_level(lod->distances,lod->count,lod->hysteresis,scene3d_lod_distance(lod,eye),choice->selected,choice->initialized);choice->initialized=true;
    }
    transmission_active=false;
    for(int i=0;i<p->mesh_count;i++) {
        auto* mesh=p->meshes[i];mesh->view_visible=false;if(mesh->layer!=SCENE_LAYER_SCENE) continue;
        // reserve every declared level's program before a camera reveals it; geometry remains retained.
        if(!mesh->material->shader) {
            Scene3dPass warm=pass;warm.tone=scene3d_transparent(mesh)?(unsigned)p->tone_mapping:0;uint64_t* unused=nullptr;
            if(!scene3d_mesh_program(entry,warm,mesh,&unused).id) {scene3d_retire_snapshot(entry);return nullptr;}
        }
        if(!mesh->visible) continue;
        if(!scene3d_lod_accepts(mesh,eye,0)) {entry->stats.lod_skips++;continue;}
        mesh->view_visible=scene3d_mesh_in_view(entry,mesh,lens,view,pass.viewport);
        transmission_active|=mesh->view_visible&&mesh->material->transmission&&mesh->material->physical_values[SCENE_TRANSMISSION]>0;
    }
    const float clear[4]={0,0,0,0};
    bool rendered=native_gl_begin(entry->graphics,entry->hdr_target,clear);
    for (int i=0;rendered && i<p->mesh_count;i++)
        if (p->meshes[i]->view_visible && !scene3d_transparent(p->meshes[i])&&!p->meshes[i]->material->transmission)
            rendered=scene3d_draw_mesh(entry,pass,p->meshes[i]);
    if(rendered&&transmission_active) {
        rendered=scene3d_post_pass(entry,entry->transmission_copy,entry->hdr_target,NATIVE_GL_BIND_TARGET,
            entry->transmission_target,(unsigned)pixel_width,(unsigned)pixel_height,false,nullptr,{},false)&&
            native_gl_begin(entry->graphics,entry->hdr_target,nullptr);
        if(rendered) {pass.transmission=entry->transmission_target;entry->stats.transmission_captures++;}
    }
    memcpy(pass.background,p->background,sizeof(pass.background));pass.frame=++entry->frame;
    for(int i=0;rendered&&i<p->mesh_count;i++)
        if(p->meshes[i]->view_visible&&p->meshes[i]->material->transmission)
            rendered=scene3d_draw_mesh(entry,pass,p->meshes[i]);
    NativeGlResource processed={};
    if (rendered) rendered=scene3d_post_linear(entry,root,raster_scale,&processed) && scene3d_output(entry,p,processed);
    // preserve the existing after-tone-map blend contract and opaque depth for transparent surfaces.
    if (rendered) rendered=native_gl_copy_depth(entry->graphics,entry->hdr_target,entry->target);
    pass.tone=(unsigned)p->tone_mapping;pass.frame=++entry->frame;
    for (int i=0;rendered && i<p->mesh_count;i++)
        if (p->meshes[i]->view_visible && scene3d_transparent(p->meshes[i]))
            rendered=scene3d_draw_mesh(entry,pass,p->meshes[i]);
    NativeGlResource published={};
    if (rendered) rendered=scene3d_post_display(entry,&published);
    ImageSurface* snapshot=rendered?native_gl_snapshot(entry->graphics,published):nullptr;
    if (!snapshot) {
        if (!entry->diagnostic[0]) scene3d_fail(entry,native_gl_diagnostic(entry->graphics));
        scene3d_retire_snapshot(entry);return nullptr;
    }
    // retire the generation while leases keep old pixels available to in-flight/retained consumers.
    scene3d_retire_snapshot(entry);image_surface_snapshot_publish(snapshot);entry->snapshot=snapshot;entry->snapshot_generation++;
    entry->width=width;entry->height=height;entry->scale=raster_scale;entry->mutation_epoch=root->doc->mutation_epoch;
    entry->source_key=source_key;
    entry->animated_generation=scene3d_animation_generation(entry->animation);
    entry->physical_generation=scene3d_physics_generation(entry->physics);
    entry->stats.projection_generation=entry->projection_generation;entry->stats.snapshot_generation=entry->snapshot_generation;
    entry->stats.meshes=p->mesh_count;entry->stats.geometries=p->geometry_count;entry->stats.textures=p->texture_count;
    entry->stats.raster_width=(unsigned)pixel_width;entry->stats.raster_height=(unsigned)pixel_height;entry->stats.camera_aspect=aspect;
    entry->diagnostic[0]=0;return snapshot;
}

void scene3d_prepare_document(DomDocument* document, UiContext* ui, float raster_scale) {
    scene3d_collect(document);
    Scene3dRegistry* registry=scene3d_registry(document,false);if (!registry) return;
    for (Scene3dEntry* entry=registry->entries;entry;entry=entry->next) {
        DomNode* node=dom_node_ref_validate(document,entry->root);
        if (node && node->is_element()) {
            BlockBlot origin={};
            Rect rect=render_geometry_block_content_rect(&origin,(ViewBlock*)node->as_element(),1);
            if (entry->mutation_epoch!=document->mutation_epoch || entry->animated_generation!=scene3d_animation_generation(entry->animation) ||
                entry->physical_generation!=scene3d_physics_generation(entry->physics) || entry->width!=rect.width ||
                entry->height!=rect.height || entry->scale!=raster_scale)
                scene3d_snapshot(node->as_element(),ui,rect.width,rect.height,raster_scale);
        }
    }
}

void render_scene3d_content(RasterRenderContext* context, ViewBlock* view) {
    if (!context || !view || !view->is_element()) return;
    Rect rect=render_geometry_block_content_rect(&context->block,view,context->raster_scale);
    ImageSurface* image=scene3d_snapshot(view->as_element(),context->ui_context,
        rect.width/context->raster_scale,rect.height/context->raster_scale,context->raster_scale);
    if (image) render_surface_content(context,view,image);
    // an invalid scene aborts file export; live documents must remain usable for repair and diagnostics.
    else if (rect.width>0 && rect.height>0 && context->ui_context->export_native_graphics) context->ui_context->render_failed=true;
}

// Vector exports embed the same generation-owned snapshot used by live raster presentation.
bool scene3d_paint_content(PaintList* paint, UiContext* ui, ViewBlock* view, float x, float y) {
    if (!paint || !ui || !view) return false;
    BlockBlot parent={};parent.x=x-view->x;parent.y=y-view->y;
    Rect rect=render_geometry_block_content_rect(&parent,view,1);
    if (rect.width<=0 || rect.height<=0) return true;
    ImageSurface* image=scene3d_snapshot(view->as_element(),ui,rect.width,rect.height,ui_context_raster_scale(ui));
    if (!image) { ui->render_failed=true;return false; }
    paint_draw_image_resource(paint,image,rect.x,rect.y,rect.width,rect.height,255,nullptr);
    return true;
}
