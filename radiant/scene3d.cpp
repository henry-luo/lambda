#include "scene3d.hpp"
#include "scene3d_math.hpp"
#include "render.hpp"
#include "layout.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lib/color.h"
#include "../lib/mem.h"
#include "../lib/mem_factory.h"
#include "../lib/mem_grow.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include <float.h>

static constexpr unsigned SCENE_NODE_LIMIT = 8192;
static constexpr unsigned SCENE_VERTEX_LIMIT = 262144;
static constexpr unsigned SCENE_INSTANCE_LIMIT = 16384;

struct Scene3dVertex { float position[3], normal[3], uv[2], color[3]; };
struct Scene3dGeometry {
    NativeGlResource vertices, indices;
    unsigned vertex_count, index_count;
};
struct Scene3dMaterial {
    float color[4];
    bool lambert, transparent;
    int side;
    NativeGlResource texture;
};
struct Scene3dMesh {
    Scene3dGeometry* geometry;
    Scene3dMaterial* material;
    Scene3dMatrix world;
    NativeGlResource instances, vertices;
    unsigned instance_count;
    float depth;
    unsigned order;
};
struct Scene3dProjection {
    Pool* pool;
    Scene3dMesh** meshes;
    int mesh_count, mesh_capacity;
    NativeGlResource* resources;
    int resource_count, resource_capacity;
    Scene3dMatrix camera_world;
    Scene3dVec camera_target, camera_up;
    bool camera_has_target, camera_found;
    float fov, near_plane, far_plane, aspect;
    float background[4], ambient[3], directions[8][3], lights[8][3];
    unsigned light_count, geometry_count, texture_count;
};
struct Scene3dEntry {
    Scene3dEntry* next;
    DomNodeRef root;
    NativeGlContext* graphics;
    NativeGlResource program, target;
    Scene3dProjection projection;
    ImageSurface* snapshot;
    uint64_t mutation_epoch, projection_generation, snapshot_generation;
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
    Scene3dGeometry* geometry;
    Scene3dMaterial* material;
    NativeGlResource texture;
};
struct Scene3dBuild {
    Scene3dEntry* entry;
    UiContext* ui;
    DomElement* root;
    Scene3dDefinition* definitions;
    unsigned definition_count, nodes;
    const char* camera;
};

static bool scene3d_fail(Scene3dEntry* entry, const char* message) {
    str_copy(entry->diagnostic, sizeof(entry->diagnostic), message, strlen(message));
    log_error("scene3d projection: %s", message); return false;
}
static bool scene3d_tag(DomElement* node, const char* tag) { return strcmp(node->local_name(), tag) == 0; }
static ItemReader scene3d_value(DomElement* node, const char* key) {
    Element* backing = dom_element_backing(node);
    return backing ? ElementReader(backing).get_attr(key) : ItemReader();
}
static bool scene3d_number(DomElement* node, const char* key, float fallback, float* result) {
    const char* text = node->get_attribute(key);
    if (text) {
        char* end; float value = strtof(text, &end);
        if (end == text || *str_skip_ascii_space(end) || !isfinite(value)) return false;
        *result = value; return true;
    }
    ItemReader value = scene3d_value(node, key);
    if (value.isNull()) { *result = fallback; return true; }
    if (!value.isNumber()) return false;
    *result = value.asFloat(); return isfinite(*result);
}
static bool scene3d_flag(DomElement* node, const char* key, bool fallback, bool* result) {
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
static bool scene3d_numbers(DomElement* node, const char* key, Pool* pool,
    float** values, unsigned* count, unsigned limit) {
    *values = nullptr; *count = 0;
    const char* text = node->get_attribute(key);
    ItemReader value = scene3d_value(node, key);
    if (!text && value.isNull()) return true;
    if (!text && !value.isArray()) return false;
    unsigned length = 0;
    if (text) {
        const char* cursor = text;
        while (*cursor) {
            cursor += strspn(cursor, " ,\t\r\n[]"); if (!*cursor) break;
            char* end; float number = strtof(cursor, &end);
            if (end == cursor || !isfinite(number) || ++length > limit) return false;
            cursor = end;
        }
    } else {
        int64_t n = value.asArray().length(); if (n < 0 || (uint64_t)n > limit) return false;
        length = (unsigned)n;
    }
    if (!length) return true;
    float* data = (float*)pool_alloc(pool, length * sizeof(float)); if (!data) return false;
    if (text) {
        const char* cursor = text;
        for (unsigned i = 0; i < length; i++) {
            cursor += strspn(cursor, " ,\t\r\n[]"); char* end; data[i] = strtof(cursor, &end); cursor = end;
        }
    } else {
        ArrayReader array = value.asArray();
        for (unsigned i = 0; i < length; i++) {
            ItemReader item = array.get(i); if (!item.isNumber()) return false;
            data[i] = item.asFloat(); if (!isfinite(data[i])) return false;
        }
    }
    *values = data; *count = length; return true;
}
static bool scene3d_vector(DomElement* node, const char* key, Scene3dVec fallback, Scene3dVec* result) {
    // vectors have a fixed bounded stack copy; no script array is retained.
    const char* text = node->get_attribute(key); ItemReader value = scene3d_value(node, key);
    if (!text && value.isNull()) { *result = fallback; return true; }
    float data[3];
    if (text) {
        const char* cursor = text;
        for (unsigned i = 0; i < 3; i++) {
            cursor += strspn(cursor, " ,\t\r\n["); char* end; data[i] = strtof(cursor, &end);
            if (end == cursor || !isfinite(data[i])) return false; cursor = end;
        }
        if (*str_skip_ascii_space(cursor + strspn(cursor," ]\t\r\n"))) return false;
    } else {
        if (!value.isArray() || value.asArray().length() != 3) return false;
        for (unsigned i = 0; i < 3; i++) {
            ItemReader number = value.asArray().get(i); if (!number.isNumber()) return false;
            data[i] = number.asFloat(); if (!isfinite(data[i])) return false;
        }
    }
    *result = {data[0], data[1], data[2]}; return true;
}
static bool scene3d_local(DomElement* node, Scene3dMatrix* matrix) {
    Scene3dVec position, rotation, scale;
    if (!scene3d_vector(node,"position",{},&position) || !scene3d_vector(node,"rotation",{},&rotation) ||
        !scene3d_vector(node,"scale",{1,1,1},&scale) || scale.x == 0 || scale.y == 0 || scale.z == 0) return false;
    *matrix = scene3d_transform(position, rotation, scale); return true;
}
static float scene3d_linear(float value) {
    return value <= .04045f ? value / 12.92f : powf((value + .055f) / 1.055f, 2.4f);
}
static bool scene3d_color(DomElement* node, const char* key, const char* fallback, float color[4]) {
    const char* text = node->get_attribute(key); if (!text) text = fallback;
    if (!strcmp(text,"transparent")) { memset(color, 0, sizeof(float)*4); return true; }
    uint8_t r,g,b,a;
    if (!color_parse_hex(text,&r,&g,&b,&a)) return false;
    color[0]=scene3d_linear(r/255.0f); color[1]=scene3d_linear(g/255.0f);
    color[2]=scene3d_linear(b/255.0f); color[3]=a/255.0f; return true;
}
static bool scene3d_resource(Scene3dBuild* build, NativeGlResource resource) {
    if (!resource.id) return scene3d_fail(build->entry, native_gl_diagnostic(build->entry->graphics));
    Scene3dProjection* p = &build->entry->projection;
    if (!lam::pool_grow_array(p->pool,&p->resources,&p->resource_capacity,p->resource_count+1,32)) return false;
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
    const char* id = node->get_attribute("id");
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

static Scene3dGeometry* scene3d_geometry(Scene3dBuild* build, Scene3dDefinition* definition) {
    if (!definition) { scene3d_fail(build->entry,"missing geometry reference"); return nullptr; }
    if (definition->geometry) return definition->geometry;
    Scene3dProjection* p=&build->entry->projection; DomElement* node=definition->source;
    Scene3dGeometry* geometry=(Scene3dGeometry*)pool_calloc(p->pool,sizeof(Scene3dGeometry)); if (!geometry) return nullptr;
    Scene3dVertex* vertices=nullptr; uint32_t* indices=nullptr;
    const char* type=node->get_attribute("type");
    if (type && (!strcmp(type,"box") || !strcmp(type,"plane"))) {
        bool box=!strcmp(type,"box"); Scene3dVec size;
        if (!scene3d_vector(node,"size",{1,1,1},&size) || size.x<=0 || size.y<=0 || size.z<=0) {
            scene3d_fail(build->entry,"invalid primitive size"); return nullptr;
        }
        geometry->vertex_count=box?24:4; geometry->index_count=box?36:6;
        vertices=(Scene3dVertex*)pool_calloc(p->pool,sizeof(Scene3dVertex)*geometry->vertex_count);
        indices=(uint32_t*)pool_alloc(p->pool,sizeof(uint32_t)*geometry->index_count);
        if (!vertices || !indices) return nullptr;
        const Scene3dVec normals[]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        const Scene3dVec rights[]={{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}};
        const Scene3dVec ups[]={{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,-1},{0,0,1}};
        const float corners[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
        const unsigned triangles[]={0,1,2,0,2,3};
        for (unsigned face=0;face<(box?6u:1u);face++) {
            for (unsigned corner=0;corner<4;corner++) {
                Scene3dVertex* v=&vertices[face*4+corner];
                const Scene3dVec& n=normals[face]; const Scene3dVec& r=rights[face]; const Scene3dVec& u=ups[face];
                const float nvalues[]={n.x,n.y,n.z}, rvalues[]={r.x,r.y,r.z}, uvalues[]={u.x,u.y,u.z};
                const float sizes[]={size.x,size.y,size.z};
                for (unsigned c=0;c<3;c++) {
                    v->position[c]=((box?nvalues[c]:0)+corners[corner][0]*rvalues[c]+corners[corner][1]*uvalues[c])*sizes[c]*.5f;
                    v->normal[c]=nvalues[c]; v->color[c]=1;
                }
                v->uv[0]=(corners[corner][0]+1)*.5f; v->uv[1]=(corners[corner][1]+1)*.5f;
            }
            for (unsigned i=0;i<6;i++) indices[face*6+i]=face*4+triangles[i];
        }
    } else if (type && !strcmp(type,"buffer")) {
        float *positions,*normals,*uvs,*colors,*raw_indices; unsigned pc,nc,uc,cc,ic;
        if (!scene3d_numbers(node,"positions",p->pool,&positions,&pc,SCENE_VERTEX_LIMIT*3) ||
            !scene3d_numbers(node,"normals",p->pool,&normals,&nc,SCENE_VERTEX_LIMIT*3) ||
            !scene3d_numbers(node,"uvs",p->pool,&uvs,&uc,SCENE_VERTEX_LIMIT*2) ||
            !scene3d_numbers(node,"colors",p->pool,&colors,&cc,SCENE_VERTEX_LIMIT*3) ||
            !scene3d_numbers(node,"indices",p->pool,&raw_indices,&ic,SCENE_VERTEX_LIMIT*6) ||
            !pc || pc%3 || (nc && nc!=pc) || (uc && uc!=pc/3*2) || (cc && cc!=pc) ||
            (ic ? ic%3 : (pc/3)%3)) { scene3d_fail(build->entry,"invalid geometry component counts"); return nullptr; }
        geometry->vertex_count=pc/3; geometry->index_count=ic;
        vertices=(Scene3dVertex*)pool_calloc(p->pool,sizeof(Scene3dVertex)*geometry->vertex_count);
        indices=ic?(uint32_t*)pool_alloc(p->pool,sizeof(uint32_t)*ic):nullptr;
        if (!vertices || (ic && !indices)) return nullptr;
        for (unsigned i=0;i<ic;i++) {
            if (raw_indices[i]<0 || raw_indices[i]>=geometry->vertex_count || floorf(raw_indices[i])!=raw_indices[i]) {
                scene3d_fail(build->entry,"geometry index out of bounds"); return nullptr;
            }
            indices[i]=(uint32_t)raw_indices[i];
        }
        for (unsigned i=0;i<geometry->vertex_count;i++) {
            for (unsigned c=0;c<3;c++) {
                vertices[i].position[c]=positions[i*3+c]; vertices[i].normal[c]=nc?normals[i*3+c]:0;
                if (cc && (colors[i*3+c]<0 || colors[i*3+c]>1)) { scene3d_fail(build->entry,"vertex color out of range"); return nullptr; }
                vertices[i].color[c]=cc?scene3d_linear(colors[i*3+c]):1;
            }
            if (uc) memcpy(vertices[i].uv,uvs+i*2,sizeof(float)*2);
        }
        if (!nc) for (unsigned i=0;i<(ic?ic:geometry->vertex_count);i+=3) {
            unsigned ids[]={ic?indices[i]:i,ic?indices[i+1]:i+1,ic?indices[i+2]:i+2};
            Scene3dVec a={positions[ids[0]*3],positions[ids[0]*3+1],positions[ids[0]*3+2]};
            Scene3dVec b={positions[ids[1]*3],positions[ids[1]*3+1],positions[ids[1]*3+2]};
            Scene3dVec c={positions[ids[2]*3],positions[ids[2]*3+1],positions[ids[2]*3+2]};
            Scene3dVec n=scene3d_cross(scene3d_sub(b,a),scene3d_sub(c,a));
            for (unsigned id:ids) { vertices[id].normal[0]+=n.x; vertices[id].normal[1]+=n.y; vertices[id].normal[2]+=n.z; }
        }
        for (unsigned i=0;i<geometry->vertex_count;i++) {
            Scene3dVec n={vertices[i].normal[0],vertices[i].normal[1],vertices[i].normal[2]};
            if (!scene3d_normalize(&n)) { scene3d_fail(build->entry,"degenerate geometry normals"); return nullptr; }
            vertices[i].normal[0]=n.x; vertices[i].normal[1]=n.y; vertices[i].normal[2]=n.z;
        }
    } else { scene3d_fail(build->entry,"unknown geometry type"); return nullptr; }
    geometry->vertices=native_gl_buffer(build->entry->graphics,vertices,sizeof(Scene3dVertex)*geometry->vertex_count);
    if (!scene3d_resource(build,geometry->vertices)) return nullptr;
    if (geometry->index_count) {
        geometry->indices=native_gl_buffer(build->entry->graphics,indices,sizeof(uint32_t)*geometry->index_count);
        if (!scene3d_resource(build,geometry->indices)) return nullptr;
    }
    p->geometry_count++; definition->geometry=geometry; return geometry;
}

static NativeGlResource scene3d_texture(Scene3dBuild* build, Scene3dDefinition* definition) {
    if (!definition) { scene3d_fail(build->entry,"missing texture reference"); return {}; }
    if (definition->texture.id) return definition->texture;
    const char* source=definition->source->get_attribute("src");
    if (!source || !*source) { scene3d_fail(build->entry,"texture requires a local image source"); return {}; }
    ImageSurface* image=load_document_image(build->root->doc,build->ui,source);
    if (!image || !image->pixels || image->format==IMAGE_FORMAT_SVG || image->alpha_mode!=IMAGE_ALPHA_STRAIGHT) {
        scene3d_fail(build->entry,"texture acquisition or format failed"); return {};
    }
    NativeGlResource texture=native_gl_texture(build->entry->graphics,image);
    if (!scene3d_resource(build,texture)) return {};
    definition->texture=texture; build->entry->projection.texture_count++; return texture;
}
static Scene3dMaterial* scene3d_material(Scene3dBuild* build, Scene3dDefinition* definition) {
    if (!definition) { scene3d_fail(build->entry,"missing material reference"); return nullptr; }
    if (definition->material) return definition->material;
    DomElement* node=definition->source; Scene3dProjection* p=&build->entry->projection;
    Scene3dMaterial* material=(Scene3dMaterial*)pool_calloc(p->pool,sizeof(Scene3dMaterial)); if (!material) return nullptr;
    const char* type=node->get_attribute("type");
    if (!type || (strcmp(type,"basic") && strcmp(type,"lambert"))) { scene3d_fail(build->entry,"unknown material type"); return nullptr; }
    material->lambert=!strcmp(type,"lambert"); float opacity;
    if (!scene3d_color(node,"color","#ffffff",material->color) ||
        !scene3d_number(node,"opacity",1,&opacity) || opacity<0 || opacity>1 ||
        !scene3d_flag(node,"transparent",false,&material->transparent)) {
        scene3d_fail(build->entry,"invalid material color or opacity"); return nullptr;
    }
    material->color[3]*=opacity; material->transparent=material->transparent || material->color[3]<1;
    const char* side=node->get_attribute("side");
    if (side && strcmp(side,"front") && strcmp(side,"back") && strcmp(side,"double")) {
        scene3d_fail(build->entry,"invalid material side"); return nullptr;
    }
    material->side=side&&!strcmp(side,"double")?2:side&&!strcmp(side,"back")?1:0;
    const char* texture=node->get_attribute("texture");
    Scene3dDefinition* texture_definition=texture?scene3d_definition(build,nullptr,texture,"texture"):nullptr;
    for (DomNode* child=node->first_child;child;child=child->next_sibling) if (child->is_element()) {
        if (!scene3d_tag(child->as_element(),"texture") || texture_definition) { scene3d_fail(build->entry,"invalid material child"); return nullptr; }
        texture_definition=scene3d_definition(build,child->as_element(),nullptr,"texture");
    }
    if (texture || texture_definition) {
        material->texture=scene3d_texture(build,texture_definition); if (!material->texture.id) return nullptr;
        // image alpha participates in the same ordered transparent pass.
        material->transparent=true;
    }
    definition->material=material; return material;
}

static bool scene3d_mesh(Scene3dBuild* build, DomElement* node, const Scene3dMatrix& world) {
    Scene3dProjection* p=&build->entry->projection;
    const char* geometry_ref=node->get_attribute("geometry"); const char* material_ref=node->get_attribute("material");
    Scene3dDefinition* gd=geometry_ref?scene3d_definition(build,nullptr,geometry_ref,"geometry"):nullptr;
    Scene3dDefinition* md=material_ref?scene3d_definition(build,nullptr,material_ref,"material"):nullptr;
    for (DomNode* child=node->first_child;child;child=child->next_sibling) if (child->is_element()) {
        DomElement* element=child->as_element();
        if (scene3d_tag(element,"geometry") && !gd && !geometry_ref) gd=scene3d_definition(build,element,nullptr,"geometry");
        else if (scene3d_tag(element,"material") && !md && !material_ref) md=scene3d_definition(build,element,nullptr,"material");
        else return scene3d_fail(build->entry,"mesh requires one geometry and one material");
    }
    Scene3dMesh* mesh=(Scene3dMesh*)pool_calloc(p->pool,sizeof(Scene3dMesh)); if (!mesh) return false;
    mesh->world=world; mesh->geometry=scene3d_geometry(build,gd); mesh->material=scene3d_material(build,md);
    if (!mesh->geometry || !mesh->material) return false;
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
                ItemReader v=instance.asArray().get(c); if (!v.isNumber()) return scene3d_fail(build->entry,"invalid instance matrix");
                matrices[i*16+c]=v.asFloat(); if (!isfinite(matrices[i*16+c])) return scene3d_fail(build->entry,"invalid instance matrix");
            }
        }
    } else if (!scene3d_numbers(node,"instances",p->pool,&matrices,&count,SCENE_INSTANCE_LIMIT*16))
        return scene3d_fail(build->entry,"invalid instance data");
    Scene3dMatrix identity=scene3d_identity();
    if (!count) { matrices=identity.v; count=16; }
    if (count%16) return scene3d_fail(build->entry,"instances require 16-component matrices");
    for (unsigned i=0;i<count;i+=16) {
        const float* m=matrices+i;
        float det=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
        if (!isfinite(det) || fabsf(det)<1e-10f || m[3]!=0 || m[7]!=0 || m[11]!=0 || m[15]!=1)
            return scene3d_fail(build->entry,"instance matrix must be nonsingular and affine");
    }
    mesh->instance_count=count/16; mesh->instances=native_gl_buffer(build->entry->graphics,matrices,count*sizeof(float));
    if (!scene3d_resource(build,mesh->instances)) return false;
    NativeGlAttribute attributes[8]={};
    const unsigned components[]={3,3,2,3}; const unsigned offsets[]={0,3*sizeof(float),6*sizeof(float),8*sizeof(float)};
    for (unsigned i=0;i<4;i++) attributes[i]={mesh->geometry->vertices,i,components[i],sizeof(Scene3dVertex),offsets[i],0};
    for (unsigned i=0;i<4;i++) attributes[i+4]={mesh->instances,i+4,4,16*sizeof(float),i*4*sizeof(float),1};
    mesh->vertices=native_gl_vertices(build->entry->graphics,attributes,8,mesh->geometry->indices);
    if (!scene3d_resource(build,mesh->vertices) || !lam::pool_grow_array(p->pool,&p->meshes,&p->mesh_capacity,p->mesh_count+1,16)) return false;
    mesh->order=p->mesh_count; p->meshes[p->mesh_count++]=mesh; return true;
}

static bool scene3d_project_node(Scene3dBuild* build, DomElement* node, const Scene3dMatrix& parent, bool inherited_visible) {
    Scene3dMatrix local; bool visible;
    if (!scene3d_local(node,&local) || !scene3d_flag(node,"visible",true,&visible)) return scene3d_fail(build->entry,"invalid object transform or visibility");
    Scene3dMatrix world=scene3d_multiply(parent,local); visible=visible&&inherited_visible;
    Scene3dProjection* p=&build->entry->projection;
    if (scene3d_tag(node,"camera")) {
        const char* type=node->get_attribute("type"); float fov,near_plane,far_plane,aspect;
        if (!type || strcmp(type,"perspective") || !scene3d_number(node,"fov",50,&fov) ||
            !scene3d_number(node,"near",.1f,&near_plane) || !scene3d_number(node,"far",1000,&far_plane) ||
            !scene3d_number(node,"aspect",0,&aspect) || fov<=0 || fov>=180 || near_plane<=0 || far_plane<=near_plane || aspect<0)
            return scene3d_fail(build->entry,"invalid perspective camera");
        Scene3dVec target,up;
        if (!scene3d_vector(node,"target",{},&target) || !scene3d_vector(node,"up",{0,1,0},&up)) return scene3d_fail(build->entry,"invalid camera orientation");
        const char* id=node->get_attribute("id");
        if ((build->camera && id && !strcmp(build->camera,id)) || (!build->camera && !p->camera_found)) {
            p->camera_found=true; p->camera_world=world; p->camera_target=scene3d_point(parent,target);
            p->camera_up=scene3d_point(parent,up,0);
            p->camera_has_target=node->has_attribute("target"); p->fov=fov;p->near_plane=near_plane;p->far_plane=far_plane;p->aspect=aspect;
        }
    } else if (scene3d_tag(node,"light")) {
        const char* type=node->get_attribute("type"); float color[4],intensity;
        if (!type || (strcmp(type,"ambient") && strcmp(type,"directional")) ||
            !scene3d_color(node,"color","#ffffff",color) || !scene3d_number(node,"intensity",1,&intensity) || intensity<0)
            return scene3d_fail(build->entry,"invalid light");
        if (visible && !strcmp(type,"ambient")) for (unsigned c=0;c<3;c++) p->ambient[c]+=color[c]*intensity;
        else if (visible) {
            if (p->light_count>=8) return scene3d_fail(build->entry,"directional light quota exceeded");
            Scene3dVec target; if (!scene3d_vector(node,"target",{},&target)) return scene3d_fail(build->entry,"invalid light target");
            Scene3dVec direction=scene3d_sub(scene3d_point(world,{}),scene3d_point(parent,target));
            if (!scene3d_normalize(&direction)) return scene3d_fail(build->entry,"directional light has zero direction");
            unsigned i=p->light_count++; p->directions[i][0]=direction.x;p->directions[i][1]=direction.y;p->directions[i][2]=direction.z;
            for (unsigned c=0;c<3;c++) p->lights[i][c]=color[c]*intensity;
        }
    } else if (scene3d_tag(node,"mesh")) {
        // invisible objects still validate their geometry/material data.
        int before=p->mesh_count; if (!scene3d_mesh(build,node,world)) return false;
        if (!visible) p->mesh_count=before;
    } else if (!scene3d_tag(node,"scene3d") && !scene3d_tag(node,"group") && !scene3d_tag(node,"resources"))
        return scene3d_fail(build->entry,"unknown scene element");
    if (scene3d_tag(node,"scene3d") || scene3d_tag(node,"group") || scene3d_tag(node,"resources"))
        for (DomNode* child=node->first_child;child;child=child->next_sibling) if (child->is_element()) {
            DomElement* element=child->as_element();
            if (scene3d_tag(element,"geometry")) { if (!scene3d_geometry(build,scene3d_definition(build,element,nullptr,"geometry"))) return false; }
            else if (scene3d_tag(element,"material")) { if (!scene3d_material(build,scene3d_definition(build,element,nullptr,"material"))) return false; }
            else if (scene3d_tag(element,"texture")) { if (!scene3d_texture(build,scene3d_definition(build,element,nullptr,"texture")).id) return false; }
            else if (!scene3d_project_node(build,element,world,visible)) return false;
        }
    return true;
}

static void scene3d_projection_destroy(Scene3dEntry* entry) {
    Scene3dProjection* p=&entry->projection;
    for (int i=p->resource_count-1;i>=0;i--) native_gl_release(entry->graphics,p->resources[i]);
    mem_pool_destroy(p->pool); *p={};
}
static void scene3d_entry_destroy(Scene3dEntry* entry) {
    scene3d_projection_destroy(entry); image_surface_destroy(entry->snapshot);
    native_gl_destroy(entry->graphics); mem_free(entry);
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
void scene3d_release_subtree(DomNode* root) {
    if (!root || !root->doc) return;
    Scene3dRegistry* registry=scene3d_registry(root->doc,false); if (!registry) return;
    Scene3dEntry** link=&registry->entries;
    while (*link) {
        Scene3dEntry* entry=*link;DomNode* node=dom_node_ref_validate(root->doc,entry->root);bool contained=false;
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
    if (!create || registry->count>=8) return nullptr;
    Scene3dEntry* entry=(Scene3dEntry*)mem_calloc(1,sizeof(Scene3dEntry),MEM_CAT_RENDER); if (!entry) return nullptr;
    entry->root=ref; entry->mutation_epoch=UINT64_MAX; entry->next=registry->entries; registry->entries=entry;registry->count++;return entry;
}
const char* scene3d_diagnostic(DomElement* root) { Scene3dEntry* e=scene3d_entry(root,false);return e?e->diagnostic:"scene3d has no rendered projection"; }
bool scene3d_stats(DomElement* root, Scene3dStats* stats) {
    Scene3dEntry* e=scene3d_entry(root,false); if (!e || !stats) return false;
    *stats=e->stats; native_gl_stats(e->graphics,&stats->graphics); return true;
}
void scene3d_context_lost(DomElement* root) {
    Scene3dEntry* e=scene3d_entry(root,false); if (!e) return;
    native_gl_lose(e->graphics); scene3d_projection_destroy(e); native_gl_destroy(e->graphics);e->graphics=nullptr;
    e->program={};e->target={};image_surface_destroy(e->snapshot);e->snapshot=nullptr;e->mutation_epoch=UINT64_MAX;
}

static const char* scene3d_vertex_shader = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 color;
layout(location=4) in mat4 instance;
uniform mat4 model, projection;
out vec3 world_normal, vertex_color;
out vec2 texture_uv;
void main() {
    mat4 world = model * instance;
    world_normal = transpose(inverse(mat3(world))) * normal;
    vertex_color = color; texture_uv = uv;
    gl_Position = projection * world * vec4(position,1.0);
}
)GLSL";
static const char* scene3d_fragment_shader = R"GLSL(#version 330 core
in vec3 world_normal, vertex_color;
in vec2 texture_uv;
uniform vec4 material;
uniform float lambert, textured, light_count;
uniform vec3 ambient, directions[8], lights[8];
uniform sampler2D image;
out vec4 fragment;
void main() {
    vec4 base = material * vec4(vertex_color,1.0);
    if (textured > 0.5) base *= texture(image,texture_uv);
    vec3 irradiance = vec3(1.0);
    if (lambert > 0.5) {
        vec3 n = normalize(world_normal) * (gl_FrontFacing ? 1.0 : -1.0);
        irradiance = ambient;
        for (int i=0; i<8; i++) if (float(i)<light_count) irradiance += lights[i] * max(dot(n,directions[i]),0.0);
    }
    fragment = vec4(base.rgb * irradiance * base.a, base.a);
}
)GLSL";

static bool scene3d_compile(Scene3dEntry* entry, DomElement* root, UiContext* ui) {
    scene3d_projection_destroy(entry); entry->diagnostic[0]=0;
    Scene3dProjection* p=&entry->projection;
    p->pool=mem_pool_create((MemContext*)root->doc->services.mem_ctx,MEM_ROLE_RENDER,"scene3d.projection");
    if (!p->pool) return scene3d_fail(entry,"projection allocation failed");
    Scene3dBuild build={};build.entry=entry;build.ui=ui;build.root=root;build.camera=root->get_attribute("camera");
    build.definitions=(Scene3dDefinition*)mem_calloc(SCENE_NODE_LIMIT,sizeof(Scene3dDefinition),MEM_CAT_RENDER);
    if (!build.definitions) return scene3d_fail(entry,"definition allocation failed");
    bool valid=scene3d_color(root,"background","transparent",p->background) &&
        scene3d_collect_definitions(&build,root,0) && scene3d_project_node(&build,root,scene3d_identity(),true);
    mem_free(build.definitions);
    if (!valid) { if (!entry->diagnostic[0]) scene3d_fail(entry,"invalid scene description"); return false; }
    if (!p->camera_found) return scene3d_fail(entry,"active camera is missing");
    entry->projection_generation++; return true;
}
static int scene3d_draw_compare(const void* a, const void* b) {
    const Scene3dMesh* left=*(Scene3dMesh* const*)a;const Scene3dMesh* right=*(Scene3dMesh* const*)b;
    if (left->material->transparent!=right->material->transparent) return left->material->transparent?1:-1;
    if (left->material->transparent && left->depth!=right->depth) return left->depth<right->depth?-1:1;
    return left->order<right->order?-1:left->order>right->order?1:0;
}
static bool scene3d_uniform(Scene3dEntry* entry, const char* name, const float* values, unsigned components, unsigned count=1) {
    return native_gl_uniform_set(entry->graphics,native_gl_uniform(entry->graphics,entry->program,name),values,components,count);
}
ImageSurface* scene3d_snapshot(DomElement* root, UiContext* ui, float width, float height, float raster_scale) {
    if (!root || !ui) return nullptr;
    scene3d_collect(root->doc); Scene3dEntry* entry=scene3d_entry(root,true); if (!entry) return nullptr;
    SvgViewBox vb=svg_parse_viewbox(root->get_attribute("viewBox"));
    if (!vb.has_viewbox) vb=svg_parse_viewbox(root->get_attribute("viewbox"));
    if (!isfinite(width) || !isfinite(height) || !isfinite(raster_scale) || width<=0 || height<=0 || raster_scale<=0 ||
        (vb.has_viewbox && (vb.width==0 || vb.height==0))) {
        image_surface_destroy(entry->snapshot);entry->snapshot=nullptr;native_gl_release(entry->graphics,entry->target);entry->target={};return nullptr;
    }
    float pixel_width=ceilf(width*raster_scale),pixel_height=ceilf(height*raster_scale);
    if (!isfinite(pixel_width) || !isfinite(pixel_height) || pixel_width>4096 || pixel_height>4096)
        { scene3d_fail(entry,"scene raster dimensions exceed quota");image_surface_destroy(entry->snapshot);entry->snapshot=nullptr;return nullptr; }
    bool changed=entry->mutation_epoch!=root->doc->mutation_epoch || entry->width!=width || entry->height!=height || entry->scale!=raster_scale;
    if (!changed && entry->snapshot) return entry->snapshot;
    if (!entry->graphics) entry->graphics=native_gl_create(ui->window!=nullptr,entry->diagnostic,sizeof(entry->diagnostic));
    if (!entry->graphics) return nullptr;
    if (!entry->program.id) entry->program=native_gl_program(entry->graphics,scene3d_vertex_shader,scene3d_fragment_shader);
    if (!entry->program.id) { scene3d_fail(entry,native_gl_diagnostic(entry->graphics));return nullptr; }
    bool reproject=entry->mutation_epoch!=root->doc->mutation_epoch;
    if (reproject && !scene3d_compile(entry,root,ui)) {
        scene3d_projection_destroy(entry);image_surface_destroy(entry->snapshot);entry->snapshot=nullptr;return nullptr;
    }
    Scene3dProjection* p=&entry->projection;
    if (!entry->target.id || entry->stats.raster_width!=(unsigned)pixel_width || entry->stats.raster_height!=(unsigned)pixel_height) {
        native_gl_release(entry->graphics,entry->target);
        entry->target=native_gl_target(entry->graphics,(unsigned)pixel_width,(unsigned)pixel_height);
        if (!entry->target.id) { scene3d_fail(entry,native_gl_diagnostic(entry->graphics));image_surface_destroy(entry->snapshot);entry->snapshot=nullptr;return nullptr; }
    }
    float aspect=p->aspect>0?p->aspect:vb.has_viewbox?vb.width/vb.height:width/height;
    Scene3dVec eye=scene3d_point(p->camera_world,{});
    Scene3dVec target=p->camera_has_target?p->camera_target:scene3d_point(p->camera_world,{0,0,-1});
    Scene3dVec up=p->camera_has_target?p->camera_up:scene3d_point(p->camera_world,{0,1,0},0);
    Scene3dMatrix view;
    if (!scene3d_camera(eye,target,up,&view)) { scene3d_fail(entry,"camera orientation is degenerate");image_surface_destroy(entry->snapshot);entry->snapshot=nullptr;return nullptr; }
    Scene3dMatrix projection=scene3d_multiply(scene3d_perspective(p->fov,aspect,p->near_plane,p->far_plane),view);
    if (vb.has_viewbox) {
        RdtMatrix fit=svg_viewbox_transform(&vb,width,height,root->get_attribute("preserveAspectRatio"));
        if (!root->get_attribute("preserveAspectRatio")) fit=svg_viewbox_transform(&vb,width,height,root->get_attribute("preserveaspectratio"));
        Scene3dMatrix frame=scene3d_identity();
        frame.v[0]=fit.e11*vb.width/width;frame.v[5]=fit.e22*vb.height/height;
        frame.v[12]=fit.e11*vb.width/width+2*fit.e13/width-1;
        frame.v[13]=1-fit.e22*vb.height/height-2*fit.e23/height;
        projection=scene3d_multiply(frame,projection);
    }
    for (int i=0;i<p->mesh_count;i++) p->meshes[i]->depth=scene3d_point(view,scene3d_point(p->meshes[i]->world,{})).z;
    qsort(p->meshes,p->mesh_count,sizeof(Scene3dMesh*),scene3d_draw_compare);
    bool rendered=native_gl_begin(entry->graphics,entry->target,p->background) &&
        scene3d_uniform(entry,"projection",projection.v,16) && scene3d_uniform(entry,"ambient",p->ambient,3) &&
        scene3d_uniform(entry,"directions[0]",p->directions[0],3,8) && scene3d_uniform(entry,"lights[0]",p->lights[0],3,8);
    float light_count=p->light_count;rendered=rendered&&scene3d_uniform(entry,"light_count",&light_count,1);
    for (int i=0;rendered && i<p->mesh_count;i++) {
        Scene3dMesh* mesh=p->meshes[i];Scene3dMaterial* material=mesh->material;
        float lambert=material->lambert?1:0,textured=material->texture.id?1:0;
        rendered=scene3d_uniform(entry,"model",mesh->world.v,16) && scene3d_uniform(entry,"material",material->color,4) &&
            scene3d_uniform(entry,"lambert",&lambert,1) && scene3d_uniform(entry,"textured",&textured,1);
        NativeGlDraw draw={entry->program,mesh->vertices,material->texture,
            mesh->geometry->index_count?mesh->geometry->index_count:mesh->geometry->vertex_count,mesh->instance_count,
            mesh->geometry->index_count!=0,material->transparent,material->side};
        rendered=rendered&&native_gl_draw(entry->graphics,&draw);
    }
    ImageSurface* snapshot=rendered?native_gl_snapshot(entry->graphics,entry->target):nullptr;
    if (!snapshot) { scene3d_fail(entry,native_gl_diagnostic(entry->graphics));image_surface_destroy(entry->snapshot);entry->snapshot=nullptr;return nullptr; }
    // old retained commands fail their image generation check and re-record; in-flight readers stay pinned by ImageSurfaceReadScope.
    image_surface_destroy(entry->snapshot);entry->snapshot=snapshot;entry->snapshot_generation++;
    entry->width=width;entry->height=height;entry->scale=raster_scale;entry->mutation_epoch=root->doc->mutation_epoch;
    entry->stats.projection_generation=entry->projection_generation;entry->stats.snapshot_generation=entry->snapshot_generation;
    entry->stats.meshes=p->mesh_count;entry->stats.geometries=p->geometry_count;entry->stats.textures=p->texture_count;
    entry->stats.raster_width=(unsigned)pixel_width;entry->stats.raster_height=(unsigned)pixel_height;entry->stats.camera_aspect=aspect;
    entry->diagnostic[0]=0;return snapshot;
}

void render_scene3d_content(RasterRenderContext* context, ViewBlock* view) {
    if (!context || !view || !view->is_element()) return;
    Rect rect=render_geometry_block_content_rect(&context->block,view,context->raster_scale);
    ImageSurface* image=scene3d_snapshot(view->as_element(),context->ui_context,
        rect.width/context->raster_scale,rect.height/context->raster_scale,context->raster_scale);
    if (image) render_surface_content(context,view,image);
}
