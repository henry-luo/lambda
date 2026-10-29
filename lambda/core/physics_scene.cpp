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
// DOM text lives in a call-local pool; retained descriptors only contain copied numeric data.
static ItemReader physics_source_field(PhysicsScene* scene,const PhysicsSceneSource& source,void* node,const char* key,Pool* scratch) {
    ItemReader value=source.field(node,key);if(!value.isNull()||!source.text)return value;
    const char* text=source.text(node,key);if(!text)return value;
    String* copy=create_string(scratch,text);if(!copy){physics_scene_fail(scene,"physics: source allocation failed");return ItemReader();}
    return ItemReader(Item{.item=s2it(copy)}.to_const());
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
static uint64_t physics_reference(const PhysicsScene& scene,const char* name) {
    if(name)for(const auto& id:scene.ids)if(!strcmp(id.id,name))return id.identity;return 0;
}
template<class Read> static bool physics_ragdoll(PhysicsScene* scene,Read read,void* node,uint64_t identity) {
    PhysicsRagdollDesc desc={};desc.node=node;desc.identity=identity;
    ItemReader requested=read("mode");const char* mode=physics_text(requested);
    if(!requested.isNull()&&!mode)return physics_scene_fail(scene,"physics: invalid ragdoll mode");
    if(!mode||!strcmp(mode,"animated"))desc.mode=PHYSICS_RAGDOLL_ANIMATED;
    else if(!strcmp(mode,"physical"))desc.mode=PHYSICS_RAGDOLL_PHYSICAL;
    else if(!strcmp(mode,"recover"))desc.mode=PHYSICS_RAGDOLL_RECOVER;
    else return physics_scene_fail(scene,"physics: invalid ragdoll mode");
    if(!identity||scene->ragdolls.size()>=16||!physics_scene_real(read("recovery-duration"),.5,&desc.recovery_duration)||
        desc.recovery_duration<=0||desc.recovery_duration>60)return physics_scene_fail(scene,"physics: ragdoll requires an ID and recovery duration within (0,60]");
    auto append=[&](const char* name){
        uint64_t bone=physics_reference(*scene,name);
        if(!bone||desc.count>=64)return false;
        desc.bones[desc.count++]={bone,SIZE_MAX};return true;
    };
    ItemReader bones=read("bones");
    if(bones.isString()) {
        const char* cursor=bones.asString()->chars;
        while(*cursor) {
            cursor+=strspn(cursor," \t\r\n");size_t length=strcspn(cursor," \t\r\n");if(!length)break;
            char name[128];if(length>=sizeof(name))return physics_scene_fail(scene,"physics: invalid ragdoll bone ID");
            memcpy(name,cursor,length);name[length]=0;cursor+=length;
            if(!append(name))return physics_scene_fail(scene,"physics: ragdoll requires at most 64 known bones");
        }
    }else if(bones.isArray()) {
        for(int64_t i=0;i<bones.asArray().length();i++)if(!append(physics_text(bones.asArray().get(i))))
            return physics_scene_fail(scene,"physics: ragdoll requires at most 64 known bones");
    }else return physics_scene_fail(scene,"physics: ragdoll requires bone IDs");
    return desc.count&&scene->ragdolls.append(desc)||physics_scene_fail(scene,"physics: ragdoll requires bones or allocation failed");
}
// index once before compiling bodies so forward references share the existing scene ID namespace.
static bool physics_index(PhysicsScene* scene,const PhysicsSceneSource& source,void* node,const char* parent,unsigned depth,size_t* nodes,Pool* scratch) {
    if(depth>64||++*nodes>8192)return physics_scene_fail(scene,"physics: scene quota exceeded");
    auto read=[&](const char* key){return physics_source_field(scene,source,node,key,scratch);};
    const char* tag=source.tag(node),*id=physics_text(read("id"));
    if(id&&!physics_identifier(scene,id,node))return false;
    ItemReader metadata=read("physics");
    if(!metadata.isNull()&&(!metadata.isMap()||!physics_identifier(scene,physics_text(metadata.asMap().get("id")),node)))return false;
    if(!strcmp(tag,"collision-geometry")) {
        double version;const char* shape=physics_text(read("shape"));
        if(!parent||strcmp(parent,"resources")||!id||source.count(node)||
            !physics_scene_real(read("version"),NAN,&version)||version!=1||
            !shape||(strcmp(shape,"convex")&&strcmp(shape,"mesh")&&strcmp(shape,"heightfield")))
            return physics_scene_fail(scene,"physics: collision-geometry requires an ID, version 1 and convex/mesh/heightfield shape inside resources");
        const char* forbidden[]={"geometry","position","rotation","quaternion","scale","matrix"};
        for(const char* key:forbidden)if(!read(key).isNull())return physics_scene_fail(scene,"physics: collision geometry cannot alias or transform another resource");
    }
    for(size_t i=0;i<source.count(node);i++) {
        void* child=source.child(node,i);
        if(child&&!physics_index(scene,source,child,tag,depth+1,nodes,scratch))return false;
    }
    return true;
}
// one bounded numeric reader serves vertex/index triples, height rows and cell spacing.
template<class Append> static bool physics_numbers(ItemReader input,size_t stride,size_t maximum,Append append) {
    size_t count=0;auto number=[&](double value){return isfinite(value)&&count++<maximum&&append(value);};
    if(input.isString()) {
        const char* cursor=input.asString()->chars;double value;int status;
        while((status=str_next_double(&cursor,&value))==1)if(!number(value))return false;
        while(*cursor==' '||*cursor=='\t'||*cursor=='\n'||*cursor=='\r')cursor++;
        if(status<0||*cursor)return false;
    } else if(input.isArray()) {
        auto array=input.asArray();bool nested=array.length()>0&&array.get(0).isArray();
        if((size_t)array.length()>maximum)return false;
        for(int64_t i=0;i<array.length();i++) {
            ItemReader value=array.get(i);
            if(nested) {
                if(!value.isArray()||(size_t)value.asArray().length()!=stride)return false;
                for(size_t j=0;j<stride;j++){double v;if(!physics_scene_real(value.asArray().get(j),NAN,&v)||!number(v))return false;}
            } else {double v;if(!physics_scene_real(value,NAN,&v)||!number(v))return false;}
        }
    } else return false;
    return count>0&&count%stride==0;
}
static bool physics_vertices(PhysicsScene* scene,ItemReader input,size_t maximum,size_t minimum,size_t* count) {
    size_t start=scene->vertices.size();PhysicsVec point={};unsigned component=0;
    bool valid=physics_numbers(input,3,3*maximum,[&](double value){
        if(component==0)point.x=value;else if(component==1)point.y=value;else point.z=value;
        if(++component<3)return true;component=0;
        return scene->vertices.size()+scene->heights.size()<PHYSICS_WORLD_MAX_VERTICES&&scene->vertices.append(point);
    });
    *count=scene->vertices.size()-start;
    return (valid&&*count>=minimum)||physics_scene_fail(scene,"physics: invalid or oversized collider vertices");
}
template<class Read> static bool physics_geometry(PhysicsScene* scene,Read read,PhysicsColliderDesc& c,bool sequential) {
    PhysicsGeometryOffsets offsets={scene->vertices.size(),scene->indices.size(),scene->heights.size()};
    if(c.shape==PHYSICS_CONVEX||c.shape==PHYSICS_MESH) {
        if(!physics_vertices(scene,read("vertices"),c.shape==PHYSICS_CONVEX?PHYSICS_CONVEX_MAX_VERTICES:PHYSICS_WORLD_MAX_VERTICES,
            c.shape==PHYSICS_CONVEX?4:3,&c.vertex_count))return false;
    } else if(!read("vertices").isNull())return physics_scene_fail(scene,"physics: vertices require convex or mesh geometry");
    if(c.shape==PHYSICS_MESH) {
        auto index=[&](double value){
            return value>=0&&value<c.vertex_count&&floor(value)==value&&
                scene->indices.size()<3*PHYSICS_WORLD_MAX_TRIANGLES&&scene->indices.append((uint32_t)value);
        };
        if(sequential&&read("indices").isNull()) {
            if(c.vertex_count%3)return physics_scene_fail(scene,"physics: unindexed buffer geometry requires triangle triples");
            for(size_t i=0;i<c.vertex_count;i++)if(!index((double)i))return physics_scene_fail(scene,"physics: mesh index quota exceeded");
        } else if(!physics_numbers(read("indices"),3,3*PHYSICS_MESH_MAX_TRIANGLES,index))
            return physics_scene_fail(scene,"physics: mesh requires bounded zero-based index triples");
        c.index_count=scene->indices.size()-offsets.indices;
    } else if(!read("indices").isNull())return physics_scene_fail(scene,"physics: indices require a mesh collider");
    if(c.shape==PHYSICS_HEIGHTFIELD) {
        double rows,columns;double spacing[2]={1,1};size_t component=0;
        if(!physics_scene_real(read("rows"),NAN,&rows)||!physics_scene_real(read("columns"),NAN,&columns)||
            rows<2||columns<2||rows>PHYSICS_WORLD_MAX_VERTICES||columns>PHYSICS_WORLD_MAX_VERTICES||
            floor(rows)!=rows||floor(columns)!=columns||rows*columns>PHYSICS_WORLD_MAX_VERTICES)
            return physics_scene_fail(scene,"physics: invalid heightfield dimensions");
        c.rows=(size_t)rows;c.columns=(size_t)columns;
        if(!read("cell-size").isNull()&&!physics_numbers(read("cell-size"),2,2,[&](double value){spacing[component++]=value;return value>0;}))
            return physics_scene_fail(scene,"physics: heightfield cell-size requires two positive numbers");
        c.cell_width=spacing[0];c.cell_depth=spacing[1];
        if(!physics_numbers(read("heights"),c.columns,c.rows*c.columns,[&](double value){
            return scene->vertices.size()+scene->heights.size()<PHYSICS_WORLD_MAX_VERTICES&&scene->heights.append(value);
        })||scene->heights.size()-offsets.heights!=c.rows*c.columns)return physics_scene_fail(scene,"physics: heightfield sample count does not match dimensions");
    } else if(!read("heights").isNull()||!read("rows").isNull()||!read("columns").isNull()||!read("cell-size").isNull())
        return physics_scene_fail(scene,"physics: height samples require a heightfield collider");
    return (scene->geometry_offsets.append(offsets)&&scene->colliders.append(c))||physics_scene_fail(scene,"physics: collider allocation failed");
}
template<class Read> static bool physics_collider(PhysicsScene* scene,const PhysicsSceneSource& source,Pool* scratch,Read read,uint64_t body,void* node,uint64_t existing_id=0) {
    PhysicsColliderDesc c={};c.id=existing_id?existing_id:physics_identifier(scene,physics_text(read("id")),node);c.body=body;
    const char* shape=physics_text(read("shape"));
    const char* shapes[]={"sphere","box","capsule","convex","mesh","heightfield"};unsigned kind=0;
    for(;kind<sizeof(shapes)/sizeof(shapes[0]);kind++)if(shape&&!strcmp(shape,shapes[kind]))break;
    if(kind==sizeof(shapes)/sizeof(shapes[0]))return physics_scene_fail(scene,"physics: unknown collider shape");
    c.shape=(PhysicsShape)kind;
    bool sensor,environment=c.shape==PHYSICS_MESH||c.shape==PHYSICS_HEIGHTFIELD;
    if(!c.id||!physics_pose(read,&c.pose,true)||!physics_scene_vector(read("size"),{1,1,1},&c.size)||
        !physics_bool(read("sensor"),false,&sensor)||!physics_scene_real(read("radius"),0.5,&c.radius)||!physics_scene_real(read("height"),1,&c.height)||
        !physics_scene_real(read("density"),sensor||environment?0:1,&c.density)||!physics_scene_real(read("friction"),0.5,&c.friction)||
        !physics_scene_real(read("restitution"),0,&c.restitution))return physics_scene_fail(scene,"physics: invalid collider attributes");
    c.sensor=sensor;c.membership=c.filter=UINT32_MAX;ItemReader groups=read("groups");
    if(!groups.isNull()) {
        double membership,filter;
        if(!groups.isMap()||!physics_scene_real(physics_field(groups,"membership"),UINT32_MAX,&membership)||
            !physics_scene_real(physics_field(groups,"filter"),UINT32_MAX,&filter)||membership<0||membership>UINT32_MAX||floor(membership)!=membership||
            filter<0||filter>UINT32_MAX||floor(filter)!=filter)return physics_scene_fail(scene,"physics: invalid collision groups");
        c.membership=(uint32_t)membership;c.filter=(uint32_t)filter;
    }
    if(!physics_bool(read("double-sided"),false,&c.double_sided))return physics_scene_fail(scene,"physics: invalid surface sidedness");
    ItemReader reference=read("geometry");
    if(reference.isNull())return physics_geometry(scene,read,c,false);
    const char* name=physics_text(reference);void* resource=nullptr;
    if(!name||!*name||(c.shape!=PHYSICS_CONVEX&&!environment))return physics_scene_fail(scene,"physics: geometry reference requires convex, mesh or heightfield shape and a nonempty name");
    const char* fields[]={"vertices","indices","heights","rows","columns","cell-size"};
    for(const char* key:fields)if(!read(key).isNull())return physics_scene_fail(scene,"physics: referenced and inline collider geometry are exclusive");
    for(const auto& id:scene->ids)if(!strcmp(id.id,name)){resource=id.node;break;}
    if(!resource)return physics_scene_fail(scene,"physics: missing collision geometry resource");
    auto field=[&](const char* key){return physics_source_field(scene,source,resource,key,scratch);};
    const char* tag=source.tag(resource);bool visual=!strcmp(tag,"geometry");
    if(visual) {
        const char* type=physics_text(field("type"));
        if(c.shape==PHYSICS_HEIGHTFIELD||!type||strcmp(type,"buffer"))return physics_scene_fail(scene,"physics: collision references require static buffer geometry or matching collision-geometry");
        const char* animated[]={"skin-indices","skin-weights","morph-positions","morph-normals"};
        for(const char* key:animated)if(!field(key).isNull())return physics_scene_fail(scene,"physics: skinned and morphed collision geometry is not supported");
    } else {
        const char* resource_shape=physics_text(field("shape"));
        if(strcmp(tag,"collision-geometry")||!resource_shape||strcmp(resource_shape,shape))return physics_scene_fail(scene,"physics: collision geometry resource kind or shape mismatch");
    }
    auto geometry_read=[&](const char* key){
        if(!visual)return field(key);
        if(!strcmp(key,"vertices"))return field("positions");
        if(!strcmp(key,"indices")&&c.shape==PHYSICS_MESH) {
            ItemReader indices=field(key);
            // an empty index stream is unindexed in the renderer as well.
            if((indices.isArray()&&!indices.asArray().length())||
                (indices.isString()&&strspn(indices.asString()->chars," \t\r\n")==indices.asString()->len))return ItemReader();
            return indices;
        }
        return ItemReader();
    };
    return physics_geometry(scene,geometry_read,c,visual);
}


bool physics_scene_colliders_equal(const PhysicsScene& a,const PhysicsScene& b) {
    if(a.colliders.size()!=b.colliders.size())return false;
    for(size_t i=0;i<a.colliders.size();i++) {
        if(!physics_collider_equal(&a.colliders[i],&b.colliders[i]))return false;
    }return true;
}
template<class Read> static bool physics_joint(PhysicsScene* scene,Read read,uint64_t identity) {
    PhysicsJointDesc d={};d.id=identity;
    const char* type=physics_text(read("type"));const char* names[]={"distance","spherical","fixed","hinge","prismatic","cone-twist","spring"};
    unsigned kind=0;while(kind<sizeof(names)/sizeof(names[0])&&(!type||strcmp(type,names[kind])))kind++;
    if(kind==sizeof(names)/sizeof(names[0]))return physics_scene_fail(scene,"physics: unknown joint type");
    bool spring=kind==6,enabled;
    d.kind=spring?PHYSICS_JOINT_DISTANCE:(PhysicsJointKind)kind;
    d.body_a=physics_reference(*scene,physics_text(read("body-a")));d.body_b=physics_reference(*scene,physics_text(read("body-b")));
    if(!identity||!d.body_a||!d.body_b)return physics_scene_fail(scene,"physics: joint requires an ID and named body-a/body-b");
    const char* object_fields[]={"position","rotation","quaternion","scale","matrix"};
    for(const char* key:object_fields)if(!read(key).isNull())return physics_scene_fail(scene,"physics: joint uses body-local attachment frames, not object transforms");
    const char* fields[2][3]={{"anchor-a","rotation-a","quaternion-a"},{"anchor-b","rotation-b","quaternion-b"}};
    for(unsigned side=0;side<2;side++) {
        auto field=[&](const char* key){return !strcmp(key,"position")?read(fields[side][0]):!strcmp(key,"rotation")?read(fields[side][1]):
            !strcmp(key,"quaternion")?read(fields[side][2]):ItemReader();};
        if(!physics_pose(field,side?&d.frame_b:&d.frame_a,true))return physics_scene_fail(scene,"physics: invalid joint attachment frame");
    }
    if(!physics_scene_real(read("length"),0,&d.length)||!physics_scene_real(read("stiffness"),0,&d.stiffness)||
        !physics_scene_real(read("damping"),0,&d.damping)||!physics_scene_real(read("lower-limit"),0,&d.lower_limit)||
        !physics_scene_real(read("upper-limit"),0,&d.upper_limit)||!physics_scene_real(read("cone-angle"),0,&d.cone_angle)||
        !physics_scene_real(read("motor-speed"),0,&d.motor_speed)||!physics_scene_real(read("motor-max-force"),0,&d.motor_max_force)||
        !physics_scene_real(read("break-force"),0,&d.break_force)||!physics_scene_real(read("break-torque"),0,&d.break_torque)||
        !physics_bool(read("enabled"),true,&enabled)||!physics_bool(read("collide-connected"),false,&d.collide_connected)||
        !physics_bool(read("spring"),spring,&d.spring)||!physics_bool(read("limits"),false,&d.limit_enabled)||!physics_bool(read("motor"),false,&d.motor_enabled)||
        (spring&&!d.spring))return physics_scene_fail(scene,"physics: invalid joint parameters");
    d.disabled=!enabled;
    bool inferred=d.kind==PHYSICS_JOINT_DISTANCE&&read("length").isNull();
    if(!scene->joints.append(d)||!scene->joint_inferred_lengths.append(inferred))return physics_scene_fail(scene,"physics: joint allocation failed");
    return true;
}

template<class Read> static bool physics_body(PhysicsScene* scene,Read read,void* node,PhysicsPose pose,uint64_t existing_id,uint64_t* identity) {
    PhysicsBodyDesc b={};b.id=existing_id?existing_id:physics_identifier(scene,physics_text(read("id")),node);b.pose=pose;
    const char* type=physics_text(read("type"));
    if(!type||(!strcmp(type,"fixed")?false:!strcmp(type,"dynamic")?false:!strcmp(type,"kinematic-position")?false:strcmp(type,"kinematic-velocity")!=0))
        return physics_scene_fail(scene,"physics: invalid body type");
    b.motion=!strcmp(type,"fixed")?PHYSICS_FIXED:!strcmp(type,"dynamic")?PHYSICS_DYNAMIC:!strcmp(type,"kinematic-position")?PHYSICS_KINEMATIC_POSITION:PHYSICS_KINEMATIC_VELOCITY;
    if(!b.id||!physics_scene_vector(read("linear-velocity"),{},&b.velocity)||!physics_scene_vector(read("angular-velocity"),{},&b.angular_velocity)||
        !physics_scene_real(read("linear-damping"),0,&b.linear_damping)||!physics_scene_real(read("angular-damping"),0,&b.angular_damping)||
        !physics_scene_real(read("gravity-scale"),1,&b.gravity_scale)||!physics_bool(read("ccd"),false,&b.ccd))return physics_scene_fail(scene,"physics: invalid body attributes");
    PhysicsBinding binding={};strcpy(binding.id,physics_scene_id(*scene,b.id));binding.identity=b.id;binding.node=node;
    if(!scene->bodies.append(b)||!scene->bindings.append(binding))return physics_scene_fail(scene,"physics: body allocation failed");
    *identity=b.id;return true;
}
static bool physics_walk(PhysicsScene* scene,const PhysicsSceneSource& source,void* node,PhysicsPose parent,uint64_t parent_body,bool ancestors_rigid,unsigned depth,unsigned* configs,size_t* nodes,Pool* scratch) {
    if(depth>64||++*nodes>8192)return physics_scene_fail(scene,"physics: scene quota exceeded");
    const char* tag=source.tag(node);auto read=[&](const char* key){return physics_source_field(scene,source,node,key,scratch);};
    uint64_t identity=0;const char* id=physics_text(read("id"));
    if(id)for(const auto& entry:scene->ids)if(!strcmp(entry.id,id)){identity=entry.identity;break;}
    if(!strcmp(tag,"collision-geometry"))return true;
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
    if(!strcmp(tag,"joint")) {
        if(depth!=1||source.count(node))return physics_scene_fail(scene,"physics: joint must be a direct, empty scene3d child");
        return physics_joint(scene,read,identity);
    }
    if(!strcmp(tag,"ragdoll")) {
        if(depth!=1||source.count(node))return physics_scene_fail(scene,"physics: ragdoll must be a direct, empty scene3d child");
        return physics_ragdoll(scene,read,node,identity);
    }
    if(!strcmp(tag,"collider")) {
        if(!parent_body)return physics_scene_fail(scene,"physics: collider must be a direct body child");
        return physics_collider(scene,source,scratch,read,parent_body,node,identity);
    }
    PhysicsPose local;bool rigid=physics_pose(read,&local,true);if(!rigid&&!physics_pose(read,&local,false)) {
        // visual matrix nodes are opaque to physics unless they contain a body.
        local={{0,0,0},{0,0,0,1}};
    }
    PhysicsPose world=pcompose(parent,local);ItemReader metadata=read("physics");
    ItemReader physical_body=read("physics-body");
    if(!physical_body.isNull()) {
        if(strcmp(tag,"bone")||!identity||!ancestors_rigid||!rigid||!metadata.isNull())
            return physics_scene_fail(scene,"physics: bone bindings require an identified bone with rigid ancestors");
        if(scene->pose_bindings.size()>=1024)return physics_scene_fail(scene,"physics: bone binding quota exceeded");
        PhysicsPoseBinding binding={};binding.node=node;binding.identity=identity;binding.body=physics_reference(*scene,physics_text(physical_body));
        if(!binding.body||!physics_bool(read("physics-enabled"),true,&binding.enabled)||
            !physics_pose([&](const char* key){return !strcmp(key,"position")?read("physics-offset"):
                !strcmp(key,"rotation")?read("physics-rotation"):!strcmp(key,"quaternion")?read("physics-quaternion"):ItemReader();},&binding.body_in_bone,true))
            return physics_scene_fail(scene,"physics: invalid bone body reference, enabled flag or offset frame");
        if(!scene->pose_bindings.append(binding))return physics_scene_fail(scene,"physics: bone binding allocation failed");
    }
    bool body=!strcmp(tag,"rigid-body")||!metadata.isNull();uint64_t body_id=0;
    if(body) {
        if(parent_body||!ancestors_rigid||!rigid)return physics_scene_fail(scene,"physics: bodies require rigid ancestors and cannot be nested");
        if(!metadata.isNull()) {
            if((strcmp(tag,"mesh")&&strcmp(tag,"group"))||!metadata.isMap())return physics_scene_fail(scene,"physics: metadata body requires a mesh or group");
            auto field=[&](const char* key){return physics_field(metadata,key);};
            if(!physics_body(scene,field,node,world,physics_reference(*scene,physics_text(field("id"))),&body_id))return false;
            ItemReader colliders=field("colliders");if(!colliders.isArray())return physics_scene_fail(scene,"physics: metadata requires colliders array");
            for(int64_t i=0;i<colliders.asArray().length();i++) {ItemReader c=colliders.asArray().get(i);
                if(!c.isMap()||!physics_collider(scene,source,scratch,[&](const char* key){return physics_field(c,key);},body_id,node))return false;}
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
    scene->bodies.clear();scene->colliders.clear();scene->joints.clear();scene->joint_inferred_lengths.clear();scene->bindings.clear();scene->ids.clear();scene->diagnostic[0]=0;
    scene->pose_bindings.clear();scene->ragdolls.clear();
    scene->vertices.clear();scene->indices.clear();scene->heights.clear();scene->geometry_offsets.clear();
    scene->desc={};scene->desc.gravity={0,-9.81,0};scene->desc.timestep=1.0/120;scene->desc.length_scale=1;scene->desc.iterations=12;
    scene->desc.contact_limit=16384;scene->desc.event_limit=65536;scene->playing=false;scene->max_substeps=8;
    Pool* scratch=pool_create();if(!scratch)return physics_scene_fail(scene,"physics: source allocation failed");
    unsigned configs=0;size_t nodes=0;bool valid=physics_index(scene,source,root,nullptr,0,&nodes,scratch);
    nodes=0;
    if(valid)valid=physics_walk(scene,source,root,{{0,0,0},{0,0,0,1}},0,true,0,&configs,&nodes,scratch);
    pool_destroy(scratch);if(!valid||scene->diagnostic[0])return false;
    if((!scene->bodies.empty()||!scene->colliders.empty()||!scene->joints.empty())&&!configs)return physics_scene_fail(scene,"physics: scene requires a physics configuration");
    // bind views only after all point appends: ArrayList growth can move the backing buffer.
    for(size_t i=0;i<scene->colliders.size();i++) {
        auto& c=scene->colliders[i];const auto& offsets=scene->geometry_offsets[i];
        if(c.vertex_count)c.vertices=scene->vertices.data()+offsets.vertices;
        if(c.index_count)c.indices=scene->indices.data()+offsets.indices;
        if(c.rows)c.heights=scene->heights.data()+offsets.heights;
    }
    for(size_t i=0;i<scene->joints.size();i++)if(scene->joint_inferred_lengths[i]) {
        auto& joint=scene->joints[i];const PhysicsBodyDesc *a=nullptr,*b=nullptr;
        for(const auto& body:scene->bodies){if(body.id==joint.body_a)a=&body;if(body.id==joint.body_b)b=&body;}
        if(!a||!b)return physics_scene_fail(scene,"physics: joint references an unknown body");
        joint.length=plen(psub(pcompose(a->pose,joint.frame_a).position,pcompose(b->pose,joint.frame_b).position));
    }
    for(size_t i=0;i<scene->pose_bindings.size();i++) {
        auto& binding=scene->pose_bindings[i];binding.body_index=SIZE_MAX;
        for(size_t k=0;k<scene->bodies.size();k++)if(scene->bodies[k].id==binding.body)binding.body_index=k;
        if(binding.body_index==SIZE_MAX)return physics_scene_fail(scene,"physics: bone binding references a non-body ID");
        for(size_t k=0;k<i;k++)if(scene->pose_bindings[k].body==binding.body)return physics_scene_fail(scene,"physics: a body may bind only one bone");
    }
    bool claimed[1024]={};
    for(auto& ragdoll:scene->ragdolls)for(size_t i=0;i<ragdoll.count;i++) {
        auto& member=ragdoll.bones[i];
        for(size_t k=0;k<scene->pose_bindings.size();k++)if(scene->pose_bindings[k].identity==member.identity)member.binding=k;
        if(member.binding==SIZE_MAX||claimed[member.binding])return physics_scene_fail(scene,"physics: ragdoll bones require unique physical bindings");
        const auto& binding=scene->pose_bindings[member.binding];
        if(!binding.enabled||scene->bodies[binding.body_index].motion!=PHYSICS_DYNAMIC||scene->bodies[binding.body_index].ccd)
            return physics_scene_fail(scene,"physics: ragdoll bindings require enabled dynamic bodies without CCD");
        claimed[member.binding]=true;
    }
    scene->desc.joints=scene->joints.data();scene->desc.joint_count=scene->joints.size();
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
bool physics_scene_bound_pose(const PhysicsPoseBinding& binding,const PhysicsState& state,PhysicsPose* pose) {
    if(!pose||!binding.enabled||binding.body_index>=state.body_count||state.bodies[binding.body_index].id!=binding.body)return false;
    *pose=pcompose(state.bodies[binding.body_index].pose,ppose_inverse(binding.body_in_bone));return true;
}
