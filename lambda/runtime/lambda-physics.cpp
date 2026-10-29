#include "lambda-physics.hpp"
#include "transpiler.hpp"
#include "value_builder.hpp"
#include "lambda-number-runtime.hpp"
#include "lambda-error.h"
#include "../../lib/physics/physics_math.h"

static Item physics_error(const char* message) {return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,message,NULL));}
static Item physics_string(const char* value) {return Item{.item=s2it(heap_create_name(value))};}
static Item physics_number(double value) {return push_d(value);}
static Item physics_integer(uint64_t value) {return Item{.item=i2it((int64_t)value)};}

static Item physics_vector(PhysicsVec vector) {
    RootedValueBuilder values(true);values.append(physics_number(vector.x));values.append(physics_number(vector.y));values.append(physics_number(vector.z));return values.value.get();
}
static Item physics_rotation(PhysicsQuat q) {
    RootedValueBuilder values(true);values.append(physics_number(q.x));values.append(physics_number(q.y));values.append(physics_number(q.z));values.append(physics_number(q.w));return values.value.get();
}
static const char* physics_read_text(ItemReader value) {return value.isString()?value.asString()->chars:value.isSymbol()?value.asSymbol()->chars:nullptr;}
static bool physics_selected(const char* id,Item selection) {
    ItemReader list(selection.to_const());if(list.isNull())return true;
    for(int64_t i=0;i<list.asArray().length();i++){ItemReader text=list.asArray().get(i);if(physics_read_text(text)&&!strcmp(physics_read_text(text),id))return true;}
    return false;
}
Item physics_state_value(const PhysicsScene& scene,const PhysicsState& state,Item selection,Item ragdolls) {
    RootFrame roots(2);Rooted<Item> selected(roots,selection),controls(roots,ragdolls);RootedValueBuilder frame,bodies(true);
    frame.put("tick",physics_integer(state.tick));frame.put("time",physics_number(state.time));
    for(size_t i=0;i<state.body_count;i++) {
        const auto& b=state.bodies[i];const char* id=physics_scene_id(scene,b.id);if(!physics_selected(id,selected.get()))continue;
        RootedValueBuilder body;body.put("id",physics_string(id));body.put("position",physics_vector(b.pose.position));body.put("quaternion",physics_rotation(b.pose.rotation));
        body.put("velocity",physics_vector(b.velocity));body.put("angular_velocity",physics_vector(b.angular_velocity));body.put("centre_of_mass",physics_vector(b.centre_of_mass));
        body.put("mass",physics_number(b.mass));body.put("sleeping",Item{.item=b2it(b.sleeping)});bodies.append(body.value.get());
    }
    frame.put("bodies",bodies.value.get());
    if(controls.get().item!=ItemNull.item)frame.put("ragdolls",controls.get());
    if(!scene.pose_bindings.empty()) {
        RootedValueBuilder bones(true);
        for(const auto& binding:scene.pose_bindings) {
            const char* body_id=physics_scene_id(scene,binding.body);if(!physics_selected(body_id,selected.get()))continue;
            PhysicsPose pose={};
            if(binding.enabled&&!physics_scene_bound_pose(binding,state,&pose))return physics_error("physics: bone snapshot references an invalid body");
            RootedValueBuilder bone;bone.put("id",physics_string(physics_scene_id(scene,binding.identity)));bone.put("body",physics_string(body_id));
            bone.put("enabled",Item{.item=b2it(binding.enabled)});bone.put("position",binding.enabled?physics_vector(pose.position):ItemNull);
            bone.put("quaternion",binding.enabled?physics_rotation(pose.rotation):ItemNull);bones.append(bone.value.get());
        }
        frame.put("bones",bones.value.get());
    }
    if(state.joint_count) {
        RootedValueBuilder joints(true);
        for(size_t i=0;i<state.joint_count;i++) {
            const auto& j=state.joints[i];RootedValueBuilder joint;
            joint.put("id",physics_string(physics_scene_id(scene,j.id)));joint.put("body_a",physics_string(physics_scene_id(scene,j.body_a)));
            joint.put("body_b",physics_string(physics_scene_id(scene,j.body_b)));joint.put("anchor_a",physics_vector(j.anchor_a));joint.put("anchor_b",physics_vector(j.anchor_b));
            joint.put("force",physics_vector(j.force));joint.put("torque",physics_vector(j.torque));joint.put("coordinate",j.coordinate_valid?physics_number(j.coordinate):ItemNull);
            joint.put("linear_error",physics_number(j.linear_error));joint.put("angular_error",physics_number(j.angular_error));
            joint.put("enabled",Item{.item=b2it(j.enabled)});joint.put("broken",Item{.item=b2it(j.broken)});joints.append(joint.value.get());
        }
        frame.put("joints",joints.value.get());
    }
    return frame.value.get();
}
Item physics_event_value(const PhysicsScene& scene,const PhysicsEvent& e) {
        RootedValueBuilder event;event.put("tick",physics_integer(e.tick));
        event.put("type",physics_string(e.joint?"joint-break":e.sensor?(e.started?"sensor-enter":"sensor-exit"):(e.started?"contact-start":"contact-end")));
        if(e.joint) {
            event.put("joint",physics_string(physics_scene_id(scene,e.joint)));event.put("force",physics_vector(e.force));event.put("torque",physics_vector(e.torque));
        } else {
            event.put("collider_a",physics_string(physics_scene_id(scene,e.collider_a)));event.put("collider_b",physics_string(physics_scene_id(scene,e.collider_b)));
        }
        event.put("body_a",physics_string(physics_scene_id(scene,e.body_a)));event.put("body_b",physics_string(physics_scene_id(scene,e.body_b)));return event.value.get();
}
Item physics_events_value(const PhysicsScene& scene,const PhysicsState& state) {
    RootedValueBuilder events(true);
    for(size_t i=0;i<state.event_count;i++)events.append(physics_event_value(scene,state.events[i]));
    return events.value.get();
}
static ItemReader physics_read_field(ItemReader object,const char* key) {return object.asMap().get(key);}
static bool physics_read_quaternion(ItemReader value,PhysicsQuat* out) {
    if(value.isNull())return true;
    if(!value.isArray()||value.asArray().length()!=4)return false;
    double v[4];for(unsigned i=0;i<4;i++)if(!physics_scene_real(value.asArray().get(i),NAN,&v[i]))return false;
    *out={v[0],v[1],v[2],v[3]};return true;
}
bool physics_commands_read(const PhysicsScene& scene,Item input,lam::ArrayList<PhysicsCommand>* out,const char** error) {
    ItemReader commands(input.to_const());out->clear();if(commands.isNull())return true;
    if(!commands.isArray()||commands.asArray().length()>65536){*error="physics: commands must be a bounded array";return false;}
    for(int64_t i=0;i<commands.asArray().length();i++) {
        ItemReader item=commands.asArray().get(i);auto field=[&](const char* name){return physics_read_field(item,name);};
        const char *kind=physics_read_text(field("type")),*id=physics_read_text(field("body"));PhysicsCommand c={};c.pose.rotation.w=1;
        const char* names[]={"impulse","force","torque","teleport","velocity","wake","target","gravity","angular_velocity"};unsigned index=0;
        const unsigned count=sizeof(names)/sizeof(names[0]);
        for(;index<count;index++)if(kind&&!strcmp(kind,names[index]))break;
        if(index==count){*error="physics: unsupported command type";return false;}c.kind=(PhysicsCommandKind)index;
        if(c.kind!=PHYSICS_GRAVITY){for(const auto& binding:scene.bindings)if(id&&!strcmp(id,binding.id))c.body=binding.identity;
            if(!c.body){*error="physics: command references an unknown body";return false;}}
        if(!physics_scene_vector(field("value"),{},&c.value)||!physics_scene_vector(field("point"),{},&c.point)||
            !physics_scene_vector(field("position"),{},&c.pose.position)){*error="physics: invalid command vector";return false;}
        c.at_point=!field("point").isNull();ItemReader local=field("local");
        if(!local.isNull()&&!local.isBool()){*error="physics: local must be boolean";return false;}c.local=local.isBool()&&local.asBool();
        PhysicsVec rotation;if(!physics_scene_vector(field("rotation"),{},&rotation)){*error="physics: invalid command rotation";return false;}c.pose.rotation=peuler(rotation);
        if(!physics_read_quaternion(field("quaternion"),&c.pose.rotation)){*error="physics: invalid command quaternion";return false;}
        if(!out->append(c)){*error="physics: command allocation failed";return false;}
    }
    return true;
}
struct PhysicsWorldOwner {PhysicsWorld* world=nullptr;~PhysicsWorldOwner(){physics_destroy(world);}};
extern "C" Item fn_physics_validate(Item input) {
    GUARD_ERROR1(input);RootFrame roots(1);Rooted<Item> source(roots,input);
    if(get_type_id(input)!=LMD_TYPE_ELEMENT)return physics_error("physics: expected scene3d element");
    PhysicsScene scene;if(!physics_scene_compile(&scene,physics_mark_source(),source.get().element))return physics_error(scene.diagnostic);
    PhysicsWorldOwner owner;PhysicsError error={};if(!physics_create(&scene.desc,&owner.world,&error))return physics_error(error.message);
    return Item{.item=b2it(true)};
}
static Item physics_hit_value(const PhysicsScene& scene,const PhysicsQuery& query,const PhysicsHit& hit) {
    if(!hit.found)return ItemNull;
    RootedValueBuilder record;record.put("body",physics_string(physics_scene_id(scene,hit.body)));record.put("collider",physics_string(physics_scene_id(scene,hit.collider)));
    record.put("distance",physics_number(hit.distance));record.put("point",physics_vector(hit.point));record.put("normal",physics_vector(hit.normal));
    if(query.kind==PHYSICS_DISTANCE||query.kind==PHYSICS_SHAPE_OVERLAP) {
        record.put("point_b",physics_vector(hit.point_b));record.put("overlapping",Item{.item=b2it(hit.overlapping)});
    } else if(query.kind==PHYSICS_SHAPE_CAST) {
        record.put("point_b",physics_vector(hit.point_b));record.put("fraction",physics_number(hit.fraction));
        record.put("initial_overlap",Item{.item=b2it(hit.overlapping)});record.put("iterations",physics_integer(hit.iterations));
    }
    return record.value.get();
}
static bool physics_character_read(const PhysicsScene& scene,ItemReader item,PhysicsCharacterMove* out) {
    auto field=[&](const char* key){return item.asMap().get(key);};
    PhysicsCharacterMove move=physics_character_defaults(scene.desc.length_scale);
    const char* id=physics_read_text(field("body"));
    for(const auto& binding:scene.bindings)if(id&&!strcmp(id,binding.id))move.body=binding.identity;
    if(!move.body||!physics_scene_vector(field("displacement"),{},&move.displacement)||
        !physics_scene_vector(field("up"),move.up,&move.up))return false;
    struct RealField {const char* name;double* value;};
    RealField reals[]={{"skin",&move.skin},{"slope_angle",&move.slope_angle},{"step_height",&move.step_height},{"snap_distance",&move.snap_distance}};
    for(auto& real:reals)if(!physics_scene_real(field(real.name),*real.value,real.value))return false;
    struct BudgetField {const char* name;unsigned* value;unsigned maximum;};
    BudgetField budgets[]={{"max_slides",&move.max_slides,32},{"recovery_iterations",&move.recovery_iterations,32},
        {"max_iterations",&move.cast_iterations,PHYSICS_CAST_MAX_ITERATIONS},{"geometry_budget",&move.geometry_budget,65536}};
    for(auto& budget:budgets){double value;if(!physics_scene_real(field(budget.name),*budget.value,&value)||value<1||value>budget.maximum||floor(value)!=value)return false;
        *budget.value=(unsigned)value;}
    if(!field("platform").isNull()) {
        const char* platform=physics_read_text(field("platform"));
        for(const auto& binding:scene.ids)if(platform&&!strcmp(platform,binding.id))move.platform=binding.identity;
        ItemReader pose=field("platform_pose");
        if(!move.platform||!pose.isMap()||!physics_scene_vector(pose.asMap().get("position"),{},&move.platform_pose.position)||
            pose.asMap().get("quaternion").isNull()||!physics_read_quaternion(pose.asMap().get("quaternion"),&move.platform_pose.rotation))return false;
    } else if(!field("platform_pose").isNull())return false;
    *out=move;return true;
}
static Item physics_character_value(const PhysicsScene& scene,const PhysicsCharacterState& state) {
    RootedValueBuilder record,contacts(true);
    record.put("position",physics_vector(state.pose.position));record.put("quaternion",physics_rotation(state.pose.rotation));
    record.put("requested",physics_vector(state.requested));record.put("achieved",physics_vector(state.achieved));
    record.put("recovery",physics_vector(state.recovery));record.put("platform_motion",physics_vector(state.platform_motion));
    record.put("ground_normal",physics_vector(state.ground_normal));record.put("platform_velocity",physics_vector(state.platform_velocity));
    record.put("grounded",Item{.item=b2it(state.grounded)});record.put("wall",Item{.item=b2it(state.wall)});
    record.put("ceiling",Item{.item=b2it(state.ceiling)});record.put("stepped",Item{.item=b2it(state.stepped)});
    record.put("ground_body",state.grounded?physics_string(physics_scene_id(scene,state.ground_body)):ItemNull);
    record.put("platform",state.grounded?physics_string(physics_scene_id(scene,state.platform)):ItemNull);
    if(state.grounded) {
        RootedValueBuilder pose;pose.put("position",physics_vector(state.platform_pose.position));pose.put("quaternion",physics_rotation(state.platform_pose.rotation));
        record.put("platform_pose",pose.value.get());
    } else record.put("platform_pose",ItemNull);
    for(size_t i=0;i<state.contact_count;i++) {
        const auto& contact=state.contacts[i];RootedValueBuilder value;
        value.put("body",physics_string(physics_scene_id(scene,contact.body)));value.put("collider",physics_string(physics_scene_id(scene,contact.collider)));
        value.put("point",physics_vector(contact.point));value.put("normal",physics_vector(contact.normal));contacts.append(value.value.get());
    }
    record.put("contacts",contacts.value.get());record.put("geometry_tests",physics_integer(state.geometry_tests));
    record.put("triangle_tests",physics_integer(state.triangle_tests));return record.value.get();
}
Item physics_move_value(const PhysicsScene& scene,PhysicsWorld* world,Item request) {
    RootFrame roots(1);Rooted<Item> source(roots,request);PhysicsCharacterMove move;
    if(!physics_character_read(scene,ItemReader(source.get().to_const()),&move))return physics_error("physics: invalid character movement request");
    PhysicsCharacterState moved={};PhysicsError error={};
    if(!physics_character_move(world,&move,&moved,&error))return physics_error(error.message);
    // query and target commit share one host operation; an async caller cannot apply a stale sampled target.
    PhysicsCommand command={};command.kind=PHYSICS_TARGET;command.body=move.body;command.pose=moved.pose;
    if(!physics_apply(world,&command,1,&error))return physics_error(error.message);
    return physics_character_value(scene,moved);
}
Item physics_queries_value(const PhysicsScene& scene,PhysicsWorld* world,Item input) {
    RootFrame roots(1);Rooted<Item> source(roots,input);ItemReader list(source.get().to_const());
    if(!list.isArray()||list.asArray().length()>65536)return physics_error("physics: queries must be a bounded array");
    struct QueryMode {bool all;size_t capacity,character;};
    lam::ArrayList<PhysicsCharacterMove> characters;
    lam::ArrayList<PhysicsQuery> queries;lam::ArrayList<PhysicsHit> hits,all_hits;lam::ArrayList<QueryMode> modes;bool individual=false;
    for(int64_t i=0;i<list.asArray().length();i++) {
        ItemReader item=list.asArray().get(i);auto field=[&](const char* key){return item.asMap().get(key);};
        const char* type=physics_read_text(field("type"));PhysicsQuery query={};query.membership=query.filter=UINT32_MAX;
        if(type&&!strcmp(type,"move")) {
            PhysicsCharacterMove move;if(!physics_character_read(scene,item,&move))return physics_error("physics: invalid character movement request");
            size_t index=characters.size();
            if(!characters.append(move)||!queries.append({})||!hits.append({})||!modes.append({false,0,index}))return physics_error("physics: movement allocation failed");
            individual=true;continue;
        }
        const char* types[]={"ray","point","aabb","distance","overlap","cast"};unsigned kind=0;
        for(;kind<sizeof(types)/sizeof(types[0]);kind++)if(type&&!strcmp(type,types[kind]))break;
        if(kind==sizeof(types)/sizeof(types[0]))return physics_error("physics: query type must be ray, point, aabb, distance, overlap, cast or move");
        query.kind=(PhysicsQueryKind)kind;bool shape=query.kind==PHYSICS_SHAPE_OVERLAP||query.kind==PHYSICS_SHAPE_CAST;
        if(query.kind==PHYSICS_DISTANCE||shape) {
            const char *a=physics_read_text(field("collider_a")),*b=physics_read_text(field("collider_b"));
            for(const auto& id:scene.ids){if(a&&!strcmp(a,id.id))query.collider_a=id.identity;if(b&&!strcmp(b,id.id))query.collider_b=id.identity;}
            if(!query.collider_a||(query.kind==PHYSICS_DISTANCE&&!query.collider_b))return physics_error("physics: shape/distance query requires known collider IDs");
        }
        if(!physics_scene_vector(field(shape?"offset":"origin"),{},&query.origin)||!physics_scene_vector(field("direction"),{},&query.direction)||
            !physics_scene_vector(field("size"),{},&query.size)||!physics_scene_real(field("distance"),1000,&query.distance))return physics_error("physics: invalid query vector or distance");
        ItemReader sensors=field("sensors"),all=field("all");
        if((!sensors.isNull()&&!sensors.isBool())||(!all.isNull()&&!all.isBool()))return physics_error("physics: query sensors and all must be boolean");
        query.include_sensors=sensors.isBool()&&sensors.asBool();QueryMode mode={all.isBool()&&all.asBool(),0,SIZE_MAX};individual|=mode.all;
        double capacity,iterations;
        if(!physics_scene_real(field("max_hits"),65536,&capacity)||capacity<1||capacity>65536||floor(capacity)!=capacity||
            !physics_scene_real(field("max_iterations"),PHYSICS_CAST_MAX_ITERATIONS,&iterations)||iterations<1||iterations>PHYSICS_CAST_MAX_ITERATIONS||floor(iterations)!=iterations)
            return physics_error("physics: invalid query result or iteration budget");
        query.cast_iterations=(unsigned)iterations;mode.capacity=(size_t)capacity;
        if(mode.capacity>scene.colliders.size())mode.capacity=scene.colliders.size();
        auto groups=field("groups");double membership,filter;
        if(!groups.isNull()) {
            if(!groups.isMap()||!physics_scene_real(groups.asMap().get("membership"),UINT32_MAX,&membership)||
                !physics_scene_real(groups.asMap().get("filter"),UINT32_MAX,&filter)||membership<0||membership>UINT32_MAX||floor(membership)!=membership||
                filter<0||filter>UINT32_MAX||floor(filter)!=filter)return physics_error("physics: invalid query collision groups");
            query.membership=(uint32_t)membership;query.filter=(uint32_t)filter;
        }
        if(!queries.append(query)||!hits.append({})||!modes.append(mode))return physics_error("physics: query allocation failed");
    }
    PhysicsError error={};
    // retain the one-pass native batch for the common single-hit form.
    if(!individual&&!physics_query(world,queries.data(),queries.size(),hits.data(),&error))return physics_error(error.message);
    RootedValueBuilder result(true);size_t total_hits=0;
    for(size_t i=0;i<queries.size();i++) {
        if(modes[i].character!=SIZE_MAX) {
            PhysicsCharacterState moved={};if(!physics_character_move(world,&characters[modes[i].character],&moved,&error))return physics_error(error.message);
            if(moved.contact_count>65536-total_hits)return physics_error("physics: combined query result quota exceeded");total_hits+=moved.contact_count;
            result.append(physics_character_value(scene,moved));
        } else if(modes[i].all) {
            while(all_hits.size()<modes[i].capacity)if(!all_hits.append({}))return physics_error("physics: query allocation failed");
            size_t count=0;
            if(!physics_query_hits(world,&queries[i],all_hits.data(),modes[i].capacity,&count,&error))return physics_error(error.message);
            if(count>65536-total_hits)return physics_error("physics: combined query result quota exceeded");total_hits+=count;
            RootedValueBuilder records(true);
            for(size_t j=0;j<count;j++)records.append(physics_hit_value(scene,queries[i],all_hits[j]));
            result.append(records.value.get());
        } else {
            if(individual&&!physics_query(world,&queries[i],1,&hits[i],&error))return physics_error(error.message);
            if(hits[i].found&&total_hits++==65536)return physics_error("physics: combined query result quota exceeded");
            result.append(physics_hit_value(scene,queries[i],hits[i]));
        }
    }
    return result.value.get();
}
extern "C" Item fn_physics_query(Item input,Item queries) {
    GUARD_ERROR2(input,queries);RootFrame roots(2);Rooted<Item> source(roots,input),requests(roots,queries);
    if(get_type_id(input)!=LMD_TYPE_ELEMENT)return physics_error("physics: expected scene3d element");
    PhysicsScene scene;if(!physics_scene_compile(&scene,physics_mark_source(),source.get().element))return physics_error(scene.diagnostic);
    PhysicsWorldOwner owner;PhysicsError error={};if(!physics_create(&scene.desc,&owner.world,&error))return physics_error(error.message);
    return physics_queries_value(scene,owner.world,requests.get());
}
extern "C" Item fn_physics_simulate(Item input,Item options) {
    GUARD_ERROR2(input,options);
    RootFrame roots(3);Rooted<Item> source(roots,input),settings(roots,options),selected(roots,ItemNull);
    if(get_type_id(input)!=LMD_TYPE_ELEMENT)return physics_error("physics: expected scene3d element");
    PhysicsScene scene;if(!physics_scene_compile(&scene,physics_mark_source(),source.get().element))return physics_error(scene.diagnostic);
    for(const auto& rig:scene.ragdolls)if(rig.mode!=PHYSICS_RAGDOLL_PHYSICAL)
        return physics_error("physics: animated ragdoll simulation requires the live scene clock");
    PhysicsWorldOwner owner;PhysicsError error={};if(!physics_create(&scene.desc,&owner.world,&error))return physics_error(error.message);
    ItemReader opts(settings.get().to_const());if(!opts.isNull()&&!opts.isMap())return physics_error("physics: options must be a map");
    double steps,sample;
    if(!physics_scene_real(opts.asMap().get("steps"),120,&steps)||steps<0||steps>100000||floor(steps)!=steps||
        !physics_scene_real(opts.asMap().get("sample_every"),1,&sample)||sample<1||sample>100000||floor(sample)!=sample)
        return physics_error("physics: invalid steps or sample_every");
    selected.set(opts.asMap().get("bodies").item());ItemReader selection(selected.get().to_const());
    if(!selection.isNull()) {
        if(!selection.isArray())return physics_error("physics: bodies selection must be an array");
        for(int64_t i=0;i<selection.asArray().length();i++){const char* id=physics_read_text(selection.asArray().get(i));bool found=false;
            for(const auto& binding:scene.bindings)if(id&&!strcmp(id,binding.id))found=true;
            if(!found)return physics_error("physics: unknown selected body");}
    }
    size_t body_count=selection.isNull()?scene.bodies.size():(size_t)selection.asArray().length();
    if(((size_t)steps/(size_t)sample+2)*(body_count+scene.joints.size()+scene.pose_bindings.size())>1000000)return physics_error("physics: trace body/joint/bone quota exceeded");
    // scheduled commands are copied before allocations can invalidate borrowed readers.
    struct Scheduled {uint64_t tick;PhysicsCommand command;};lam::ArrayList<Scheduled> schedule;
    ItemReader entries=opts.asMap().get("commands");if(!entries.isNull()) {
        if(!entries.isArray()||entries.asArray().length()>65536)return physics_error("physics: scheduled commands must be a bounded array");
        for(int64_t i=0;i<entries.asArray().length();i++) {
            ItemReader entry=entries.asArray().get(i);double tick;lam::ArrayList<PhysicsCommand> commands;const char* message=nullptr;
            if(!entry.isMap()||!physics_scene_real(entry.asMap().get("tick"),NAN,&tick)||tick<0||tick>=steps||floor(tick)!=tick||
                !physics_commands_read(scene,entry.asMap().get("commands").item(),&commands,&message))return physics_error(message?message:"physics: invalid scheduled command");
            for(const auto& c:commands)if(schedule.size()>=65536||!schedule.append({(uint64_t)tick,c}))return physics_error("physics: schedule allocation failed");
        }
    }
    RootedValueBuilder trace,frames(true),events(true);PhysicsState state={};physics_read(owner.world,&state,&error);
    frames.append(physics_state_value(scene,state,selected.get()));
    for(uint64_t tick=0;tick<(uint64_t)steps;tick++) {
        for(const auto& entry:schedule)if(entry.tick==tick&&!physics_apply(owner.world,&entry.command,1,&error))return physics_error(error.message);
        if(!physics_step(owner.world,1,&state,&error))return physics_error(error.message);
        if(state.event_count){RootFrame event_roots(1);Rooted<Item> batch(event_roots,physics_events_value(scene,state));
            for(int64_t j=0;j<vector_length(batch.get());j++){if(vector_length(events.value.get())>=65536)return physics_error("physics: trace event quota exceeded");events.append(vector_get(batch.get(),j));}}
        if(state.tick%(uint64_t)sample==0||state.tick==(uint64_t)steps)frames.append(physics_state_value(scene,state,selected.get()));
    }
    trace.put("engine",physics_string("lambda-physics-1"));trace.put("timestep",physics_number(scene.desc.timestep));trace.put("ticks",physics_integer(state.tick));
    trace.put("frames",frames.value.get());trace.put("events",events.value.get());return trace.value.get();
}
