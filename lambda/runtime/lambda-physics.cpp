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
Item physics_state_value(const PhysicsScene& scene,const PhysicsState& state,Item selection) {
    RootFrame roots(1);Rooted<Item> selected(roots,selection);RootedValueBuilder frame,bodies(true);
    frame.put("tick",physics_integer(state.tick));frame.put("time",physics_number(state.time));
    for(size_t i=0;i<state.body_count;i++) {
        const auto& b=state.bodies[i];const char* id=physics_scene_id(scene,b.id);if(!physics_selected(id,selected.get()))continue;
        RootedValueBuilder body;body.put("id",physics_string(id));body.put("position",physics_vector(b.pose.position));body.put("quaternion",physics_rotation(b.pose.rotation));
        body.put("velocity",physics_vector(b.velocity));body.put("angular_velocity",physics_vector(b.angular_velocity));body.put("centre_of_mass",physics_vector(b.centre_of_mass));
        body.put("mass",physics_number(b.mass));body.put("sleeping",Item{.item=b2it(b.sleeping)});bodies.append(body.value.get());
    }
    frame.put("bodies",bodies.value.get());return frame.value.get();
}
Item physics_event_value(const PhysicsScene& scene,const PhysicsEvent& e) {
        RootedValueBuilder event;event.put("tick",physics_integer(e.tick));
        event.put("type",physics_string(e.sensor?(e.started?"sensor-enter":"sensor-exit"):(e.started?"contact-start":"contact-end")));
        event.put("collider_a",physics_string(physics_scene_id(scene,e.collider_a)));event.put("collider_b",physics_string(physics_scene_id(scene,e.collider_b)));
        event.put("body_a",physics_string(physics_scene_id(scene,e.body_a)));event.put("body_b",physics_string(physics_scene_id(scene,e.body_b)));return event.value.get();
}
Item physics_events_value(const PhysicsScene& scene,const PhysicsState& state) {
    RootedValueBuilder events(true);
    for(size_t i=0;i<state.event_count;i++)events.append(physics_event_value(scene,state.events[i]));
    return events.value.get();
}
static ItemReader physics_read_field(ItemReader object,const char* key) {return object.asMap().get(key);}
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
        ItemReader q=field("quaternion");if(!q.isNull()) {
            if(!q.isArray()||q.asArray().length()!=4){*error="physics: invalid command quaternion";return false;}
            double v[4];for(unsigned j=0;j<4;j++)if(!physics_scene_real(q.asArray().get(j),NAN,&v[j])){*error="physics: invalid command quaternion";return false;}
            c.pose.rotation={v[0],v[1],v[2],v[3]};
        }
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
Item physics_queries_value(const PhysicsScene& scene,PhysicsWorld* world,Item input) {
    RootFrame roots(1);Rooted<Item> source(roots,input);ItemReader list(source.get().to_const());
    if(!list.isArray()||list.asArray().length()>65536)return physics_error("physics: queries must be a bounded array");
    lam::ArrayList<PhysicsQuery> queries;lam::ArrayList<PhysicsHit> hits;
    for(int64_t i=0;i<list.asArray().length();i++) {
        ItemReader item=list.asArray().get(i);auto field=[&](const char* key){return item.asMap().get(key);};
        const char* type=physics_read_text(field("type"));PhysicsQuery query={};query.membership=query.filter=UINT32_MAX;
        if(!type||(!strcmp(type,"ray")?false:!strcmp(type,"point")?false:strcmp(type,"aabb")!=0))return physics_error("physics: query type must be ray, point or aabb");
        query.kind=!strcmp(type,"ray")?PHYSICS_RAY:!strcmp(type,"point")?PHYSICS_POINT:PHYSICS_OVERLAP;
        if(!physics_scene_vector(field("origin"),{},&query.origin)||!physics_scene_vector(field("direction"),{},&query.direction)||
            !physics_scene_vector(field("size"),{},&query.size)||!physics_scene_real(field("distance"),1000,&query.distance))return physics_error("physics: invalid query vector or distance");
        ItemReader sensors=field("sensors");if(!sensors.isNull()&&!sensors.isBool())return physics_error("physics: query sensors must be boolean");query.include_sensors=sensors.isBool()&&sensors.asBool();
        if(!queries.append(query)||!hits.append({}))return physics_error("physics: query allocation failed");
    }
    PhysicsError error={};if(!physics_query(world,queries.data(),queries.size(),hits.data(),&error))return physics_error(error.message);
    RootedValueBuilder result(true);
    for(const auto& hit:hits) {
        if(!hit.found){result.append(ItemNull);continue;}
        RootedValueBuilder record;record.put("body",physics_string(physics_scene_id(scene,hit.body)));record.put("collider",physics_string(physics_scene_id(scene,hit.collider)));
        record.put("distance",physics_number(hit.distance));record.put("point",physics_vector(hit.point));record.put("normal",physics_vector(hit.normal));result.append(record.value.get());
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
    if(((size_t)steps/(size_t)sample+2)*body_count>1000000)return physics_error("physics: trace body quota exceeded");
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
