#include "physics_scene.hpp"
#include "../../lib/physics/physics_math.h"
#include "../../lib/str.h"
#include "../../lib/string.h"
#include <string.h>

static bool physics_scene_fail(PhysicsScene* scene,const char* message) {
    strncpy(scene->diagnostic,message,sizeof(scene->diagnostic)-1);return false;
}
static ItemReader physics_field(ItemReader item,const char* key) {return item.asMap().get(key);}
static const char* physics_text(ItemReader item) {
    return item.isString()?item.asString()->chars:item.isSymbol()?item.asSymbol()->chars:nullptr;
}
bool physics_scene_real(ItemReader item,double fallback,double* result) {
    if(item.isString()){const char* cursor=item.asString()->chars;double extra;return str_next_double(&cursor,result)==1&&str_next_double(&cursor,&extra)==0;}
    *result=item.isNull()?fallback:item.isInt()?item.asInt():item.isFloat()?item.asFloat():NAN;return isfinite(*result);
}
bool physics_scene_vector(ItemReader value,PhysicsVec fallback,PhysicsVec* result) {
    if(value.isNull()){*result=fallback;return true;}
    if(value.isString()) {
        const char* cursor=value.asString()->chars;double v[3],extra;
        for(unsigned i=0;i<3;i++)if(str_next_double(&cursor,&v[i])!=1)return false;
        if(str_next_double(&cursor,&extra)!=0)return false;*result=pv(v[0],v[1],v[2]);return true;
    }
    if(!value.isArray()||value.asArray().length()!=3)return false;
    double v[3];for(unsigned i=0;i<3;i++)if(!physics_scene_real(value.asArray().get(i),NAN,&v[i]))return false;
    *result=pv(v[0],v[1],v[2]);return true;
}
static bool physics_bool(ItemReader item,bool fallback,bool* result) {
    if(item.isString()){const char* text=item.asString()->chars;if(!strcmp(text,"true")){*result=true;return true;}if(!strcmp(text,"false")){*result=false;return true;}return false;}
    if(item.isNull()){*result=fallback;return true;}if(!item.isBool())return false;*result=item.asBool();return true;
}
template<class Read> static bool physics_pose(Read read,PhysicsPose* pose,bool rigid) {
    PhysicsVec e,scale;
    if(!physics_scene_vector(read("position"),{},&pose->position)||!physics_scene_vector(read("rotation"),{},&e)||
        !physics_scene_vector(read("scale"),{1,1,1},&scale)||!read("matrix").isNull())return false;
    if(rigid&&(scale.x!=1||scale.y!=1||scale.z!=1))return false;
    pose->rotation=peuler(e);ItemReader q=read("quaternion");
    if(!q.isNull()) {
        if(!read("rotation").isNull())return false;
        double values[4];
        if(q.isString()) {const char* cursor=q.asString()->chars;double extra;
            for(unsigned i=0;i<4;i++)if(str_next_double(&cursor,&values[i])!=1)return false;
            if(str_next_double(&cursor,&extra)!=0)return false;
        }else {if(!q.isArray()||q.asArray().length()!=4)return false;
            for(unsigned i=0;i<4;i++)if(!physics_scene_real(q.asArray().get(i),NAN,&values[i]))return false;}
        pose->rotation={values[0],values[1],values[2],values[3]};
    }
    return pqnorm(&pose->rotation);
}
static uint64_t physics_identifier(PhysicsScene* scene,const char* text,void* node) {
    if(scene->ids.size()>=8192){physics_scene_fail(scene,"physics: ID quota exceeded");return 0;}
    if(!text||!*text||strlen(text)>=128){physics_scene_fail(scene,"physics: requires a nonempty ID shorter than 128 bytes");return 0;}
    for(auto& id:scene->ids)if(!strcmp(id.id,text)){physics_scene_fail(scene,"physics: duplicate scene ID");return 0;}
    PhysicsBinding id={};strcpy(id.id,text);id.node=node;id.identity=UINT64_C(14695981039346656037);
    for(const unsigned char* p=(const unsigned char*)text;*p;p++){id.identity^=*p;id.identity*=UINT64_C(1099511628211);}
    if(!id.identity)id.identity=1;
    for(const auto& other:scene->ids)if(other.identity==id.identity){physics_scene_fail(scene,"physics: ID hash collision");return 0;}
    if(!scene->ids.append(id)){physics_scene_fail(scene,"physics: ID allocation failed");return 0;}return id.identity;
}
const char* physics_scene_id(const PhysicsScene& scene,uint64_t identity) {
    for(const auto& id:scene.ids)if(id.identity==identity)return id.id;return "";
}
template<class Read> static bool physics_collider(PhysicsScene* scene,Read read,uint64_t body,void* node,uint64_t existing_id=0) {
    PhysicsColliderDesc c={};c.id=existing_id?existing_id:physics_identifier(scene,physics_text(read("id")),node);c.body=body;
    const char* shape=physics_text(read("shape"));
    if(!shape||(!strcmp(shape,"sphere")?false:strcmp(shape,"box")!=0))return physics_scene_fail(scene,"physics: supported collider shapes are sphere and box");
    c.shape=!strcmp(shape,"sphere")?PHYSICS_SPHERE:PHYSICS_BOX;
    bool sensor;
    if(!c.id||!physics_pose(read,&c.pose,true)||!physics_scene_vector(read("size"),{1,1,1},&c.size)||
        !physics_bool(read("sensor"),false,&sensor)||!physics_scene_real(read("radius"),0.5,&c.radius)||
        !physics_scene_real(read("density"),sensor?0:1,&c.density)||!physics_scene_real(read("friction"),0.5,&c.friction)||
        !physics_scene_real(read("restitution"),0,&c.restitution))return physics_scene_fail(scene,"physics: invalid collider attributes");
    c.sensor=sensor;c.membership=c.filter=UINT32_MAX;ItemReader groups=read("groups");
    if(!groups.isNull()) {
        double membership,filter;
        if(!groups.isMap()||!physics_scene_real(physics_field(groups,"membership"),UINT32_MAX,&membership)||
            !physics_scene_real(physics_field(groups,"filter"),UINT32_MAX,&filter)||membership<0||membership>UINT32_MAX||floor(membership)!=membership||
            filter<0||filter>UINT32_MAX||floor(filter)!=filter)return physics_scene_fail(scene,"physics: invalid collision groups");
        c.membership=(uint32_t)membership;c.filter=(uint32_t)filter;
    }
    if(!read("geometry").isNull())return physics_scene_fail(scene,"physics: referenced collision geometry is not supported yet");
    return scene->colliders.append(c)||physics_scene_fail(scene,"physics: collider allocation failed");
}
template<class Read> static bool physics_body(PhysicsScene* scene,Read read,void* node,PhysicsPose pose,uint64_t existing_id,uint64_t* identity) {
    PhysicsBodyDesc b={};b.id=existing_id?existing_id:physics_identifier(scene,physics_text(read("id")),node);b.pose=pose;
    const char* type=physics_text(read("type"));
    if(!type||(!strcmp(type,"fixed")?false:!strcmp(type,"dynamic")?false:!strcmp(type,"kinematic-position")?false:strcmp(type,"kinematic-velocity")!=0))
        return physics_scene_fail(scene,"physics: invalid body type");
    b.motion=!strcmp(type,"fixed")?PHYSICS_FIXED:!strcmp(type,"dynamic")?PHYSICS_DYNAMIC:!strcmp(type,"kinematic-position")?PHYSICS_KINEMATIC_POSITION:PHYSICS_KINEMATIC_VELOCITY;
    bool ccd;
    if(!b.id||!physics_scene_vector(read("linear-velocity"),{},&b.velocity)||!physics_scene_vector(read("angular-velocity"),{},&b.angular_velocity)||
        !physics_scene_real(read("linear-damping"),0,&b.linear_damping)||!physics_scene_real(read("angular-damping"),0,&b.angular_damping)||
        !physics_scene_real(read("gravity-scale"),1,&b.gravity_scale)||!physics_bool(read("ccd"),false,&ccd))return physics_scene_fail(scene,"physics: invalid body attributes");
    if(ccd)return physics_scene_fail(scene,"physics: CCD is not supported yet");
    PhysicsBinding binding={};strcpy(binding.id,physics_scene_id(*scene,b.id));binding.identity=b.id;binding.node=node;
    if(!scene->bodies.append(b)||!scene->bindings.append(binding))return physics_scene_fail(scene,"physics: body allocation failed");
    *identity=b.id;return true;
}
static bool physics_walk(PhysicsScene* scene,const PhysicsSceneSource& source,void* node,PhysicsPose parent,uint64_t parent_body,bool ancestors_rigid,unsigned depth,unsigned* configs,size_t* nodes,Pool* scratch) {
    if(depth>64||++*nodes>8192)return physics_scene_fail(scene,"physics: scene quota exceeded");
    const char* tag=source.tag(node);auto read=[&](const char* key){
        ItemReader value=source.field(node,key);if(!value.isNull()||!source.text)return value;
        const char* text=source.text(node,key);if(!text)return value;
        String* copy=create_string(scratch,text);if(!copy){physics_scene_fail(scene,"physics: source allocation failed");return ItemReader();}
        return ItemReader(Item{.item=s2it(copy)}.to_const());
    };
    uint64_t identity=0;const char* id=physics_text(read("id"));if(id){identity=physics_identifier(scene,id,node);if(!identity)return false;}
    if(!strcmp(tag,"physics")) {
        if(depth!=1||++*configs>1||source.count(node))return physics_scene_fail(scene,"physics: configuration must be one direct child of scene3d");
        double iterations,substeps;
        if(!physics_scene_vector(read("gravity"),{0,-9.81,0},&scene->desc.gravity)||!physics_scene_real(read("timestep"),1.0/120,&scene->desc.timestep)||
            !physics_scene_real(read("length-scale"),1,&scene->desc.length_scale)||!physics_scene_real(read("solver-iterations"),12,&iterations)||
            !physics_scene_real(read("max-substeps"),8,&substeps)||iterations<1||iterations>256||floor(iterations)!=iterations||substeps<1||substeps>1024||floor(substeps)!=substeps)
            return physics_scene_fail(scene,"physics: invalid world configuration");
        const char* state=physics_text(read("state"));if(state&&strcmp(state,"paused")&&strcmp(state,"playing"))return physics_scene_fail(scene,"physics: invalid playback state");
        scene->playing=state&&!strcmp(state,"playing");scene->max_substeps=(unsigned)substeps;scene->desc.iterations=(unsigned)iterations;return true;
    }
    if(!strcmp(tag,"joint"))return physics_scene_fail(scene,"physics: joints are not supported yet");
    if(!strcmp(tag,"collider")) {
        if(!parent_body)return physics_scene_fail(scene,"physics: collider must be a direct body child");
        return physics_collider(scene,read,parent_body,node,identity);
    }
    PhysicsPose local;bool rigid=physics_pose(read,&local,true);if(!rigid&&!physics_pose(read,&local,false)) {
        // visual matrix nodes are opaque to physics unless they contain a body.
        local={{0,0,0},{0,0,0,1}};
    }
    PhysicsPose world=pcompose(parent,local);ItemReader metadata=read("physics");
    bool body=!strcmp(tag,"rigid-body")||!metadata.isNull();uint64_t body_id=0;
    if(body) {
        if(parent_body||!ancestors_rigid||!rigid)return physics_scene_fail(scene,"physics: bodies require rigid ancestors and cannot be nested");
        if(!metadata.isNull()) {
            if((strcmp(tag,"mesh")&&strcmp(tag,"group"))||!metadata.isMap())return physics_scene_fail(scene,"physics: metadata body requires a mesh or group");
            auto field=[&](const char* key){return physics_field(metadata,key);};
            if(!physics_body(scene,field,node,world,0,&body_id))return false;
            ItemReader colliders=field("colliders");if(!colliders.isArray())return physics_scene_fail(scene,"physics: metadata requires colliders array");
            for(int64_t i=0;i<colliders.asArray().length();i++) {ItemReader c=colliders.asArray().get(i);
                if(!c.isMap()||!physics_collider(scene,[&](const char* key){return physics_field(c,key);},body_id,node))return false;}
        } else if(!physics_body(scene,read,node,world,identity,&body_id))return false;
    }
    for(size_t i=0;i<source.count(node);i++) {
        void* child=source.child(node,i);if(!child)continue;
        bool direct_collider=!strcmp(source.tag(child),"collider");
        if(direct_collider&&!body)return physics_scene_fail(scene,"physics: collider must be a direct body child");
        if(!physics_walk(scene,source,child,world,body?body_id:parent_body,ancestors_rigid&&rigid,depth+1,configs,nodes,scratch))return false;
    }
    return true;
}
bool physics_scene_compile(PhysicsScene* scene,const PhysicsSceneSource& source,void* root) {
    if(!scene||!root||strcmp(source.tag(root),"scene3d"))return scene&&physics_scene_fail(scene,"physics: expected scene3d root");
    scene->bodies.clear();scene->colliders.clear();scene->bindings.clear();scene->ids.clear();scene->diagnostic[0]=0;
    scene->desc={};scene->desc.gravity={0,-9.81,0};scene->desc.timestep=1.0/120;scene->desc.length_scale=1;scene->desc.iterations=12;
    scene->desc.contact_limit=16384;scene->desc.event_limit=65536;scene->playing=false;scene->max_substeps=8;
    Pool* scratch=pool_create();if(!scratch)return physics_scene_fail(scene,"physics: source allocation failed");
    unsigned configs=0;size_t nodes=0;bool valid=physics_walk(scene,source,root,{{0,0,0},{0,0,0,1}},0,true,0,&configs,&nodes,scratch);
    pool_destroy(scratch);if(!valid||scene->diagnostic[0])return false;
    if((!scene->bodies.empty()||!scene->colliders.empty())&&!configs)return physics_scene_fail(scene,"physics: scene requires a physics configuration");
    scene->desc.bodies=scene->bodies.data();scene->desc.body_count=scene->bodies.size();scene->desc.colliders=scene->colliders.data();scene->desc.collider_count=scene->colliders.size();return true;
}
static ItemReader physics_mark_item(void* node) {return ItemReader(Item{.element=(Element*)node}.to_const());}
static const char* physics_mark_tag(void* node) {return physics_mark_item(node).asElement().tagName();}
static ItemReader physics_mark_field(void* node,const char* key) {return physics_mark_item(node).asElement().get_attr(key);}
static size_t physics_mark_count(void* node) {return (size_t)physics_mark_item(node).asElement().childCount();}
static void* physics_mark_child(void* node,size_t index) {
    ItemReader child=physics_mark_item(node).asElement().childAt((int64_t)index);return child.isElement()?child.item().element:nullptr;
}
PhysicsSceneSource physics_mark_source() {return {physics_mark_tag,physics_mark_field,physics_mark_count,physics_mark_child,nullptr};}
