#include "scene3d.hpp"
#include "../event.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/dom_lifecycle.hpp"
#include "../../lambda/runtime/value_builder.hpp"
#include "../../lambda/runtime/lambda-number-runtime.hpp"
#include "../../lambda/runtime/lambda-error.h"
#include "../../lambda/core/mark_reader.hpp"
#include "../../lambda/core/physics_scene.hpp"
#include "../../lib/mem.h"
#include "../../lib/log.h"
#include <math.h>

struct Scene3dRuntimeService {
    void* owner;
    Scene3dTickFn tick;
    bool enabled;
};
struct Scene3dRuntime {
    DomDocument* document;
    DomNodeRef root;
    AnimationInstance* driver;
    Scene3dRuntimeService services[SCENE3D_TICK_PHASES];
    Scene3dRuntimeStats stats;
    double previous_host_time;
    double animation_ahead;
    unsigned max_substeps;
    bool sampling;
    const char* diagnostic;
};

static unsigned scene3d_runtime_active(Scene3dRuntime* runtime) {
    unsigned count=0;
    for(const auto& service:runtime->services) if(service.enabled) count++;
    return count;
}
static void scene3d_runtime_released(AnimationInstance* driver) {
    ((Scene3dRuntime*)driver->target)->driver=nullptr;
}
static bool scene3d_runtime_fail(Scene3dRuntime* runtime,const char* message) {
    runtime->diagnostic=message;runtime->stats.faulted=true;
    log_error("scene3d runtime fault: %s",message);return false;
}
static bool scene3d_runtime_run_service(Scene3dRuntime* runtime,unsigned phase,double delta,bool force) {
    auto& service=runtime->services[phase];
    if(!service.owner||(!force&&!service.enabled)) return true;
    auto result=service.tick(service.owner,delta);
    if(result==SCENE3D_TICK_FAILED) return scene3d_runtime_fail(runtime,
        phase==SCENE3D_TICK_ANIMATION?"animation stage failed":"physics stage failed");
    if(result==SCENE3D_TICK_IDLE) service.enabled=false;
    return true;
}
static bool scene3d_runtime_tick_once(Scene3dRuntime* runtime,bool all_services) {
    if(runtime->stats.ticks==UINT64_MAX||!isfinite((double)(runtime->stats.ticks+1)*runtime->stats.timestep))
        return scene3d_runtime_fail(runtime,"simulation tick count exceeded");
    // a frame-sampled animation may already own the fractional part of this fixed tick.
    double animation_delta=fmax(0,runtime->stats.timestep-runtime->animation_ahead);
    if(!scene3d_runtime_run_service(runtime,SCENE3D_TICK_ANIMATION,animation_delta,all_services)||
       !scene3d_runtime_run_service(runtime,SCENE3D_TICK_PHYSICS,runtime->stats.timestep,all_services)) return false;
    runtime->animation_ahead=0;
    runtime->stats.ticks++;
    runtime->stats.time=(double)runtime->stats.ticks*runtime->stats.timestep;
    return true;
}
bool scene3d_runtime_advance(Scene3dRuntime* runtime,double elapsed) {
    if(!runtime||runtime->sampling||runtime->stats.faulted||!isfinite(elapsed)||elapsed<0) return false;
    // repeated sampling of one host timestamp must not consume another budget of old debt (D2.11.4).
    if(elapsed==0||runtime->stats.paused||runtime->stats.time_scale==0||!scene3d_runtime_active(runtime)) return true;
    double debt=runtime->stats.debt+elapsed*runtime->stats.time_scale;
    if(!isfinite(debt)) return scene3d_runtime_fail(runtime,"nonfinite simulation time");
    if(!runtime->services[SCENE3D_TICK_PHYSICS].enabled) {
        // animation-only views retain exact virtual-time samples; no solver work needs catch-up substeps.
        double ticks=floor((debt+runtime->stats.timestep*1e-10)/runtime->stats.timestep);
        if(ticks>=(double)(UINT64_MAX-runtime->stats.ticks)) return scene3d_runtime_fail(runtime,"simulation tick count exceeded");
        runtime->sampling=true;
        bool sampled=scene3d_runtime_run_service(runtime,SCENE3D_TICK_ANIMATION,fmax(0,debt-runtime->animation_ahead),false);
        runtime->sampling=false;
        if(!sampled) return false;
        runtime->stats.ticks+=(uint64_t)ticks;runtime->stats.time=(double)runtime->stats.ticks*runtime->stats.timestep; // INT_CAST_OK: bounded finite whole tick count
        runtime->stats.debt=fmax(0,debt-ticks*runtime->stats.timestep);
        runtime->animation_ahead=runtime->stats.debt;runtime->stats.interpolation=runtime->stats.debt/runtime->stats.timestep;
        runtime->stats.presentations++;return true;
    }
    if(debt>2) return scene3d_runtime_fail(runtime,"simulation pacing debt exceeded");
    runtime->stats.debt=debt;
    if(elapsed>0) runtime->stats.presentations++;
    runtime->sampling=true;
    unsigned steps=0;
    // tolerate roundoff at an exact boundary without changing the fixed timestep.
    while(runtime->stats.debt+runtime->stats.timestep*1e-10>=runtime->stats.timestep&&
          steps<runtime->max_substeps&&scene3d_runtime_active(runtime)) {
        if(!scene3d_runtime_tick_once(runtime,false)) {runtime->sampling=false;return false;}
        runtime->stats.debt=fmax(0,runtime->stats.debt-runtime->stats.timestep);steps++;
    }
    runtime->sampling=false;
    runtime->stats.interpolation=fmin(1,runtime->stats.debt/runtime->stats.timestep);
    return true;
}
static void scene3d_runtime_tick(AnimationInstance* driver,float) {
    auto* runtime=(Scene3dRuntime*)driver->target;
    double now=animation_clock_time(runtime->document->state->animation_scheduler,driver,0);
    DomNode* root=dom_node_ref_validate(runtime->document,runtime->root);
    bool attached=false;
    for(auto* parent=root;parent;parent=parent->parent) if(parent==runtime->document->root) {attached=true;break;}
    double elapsed=now-runtime->previous_host_time;runtime->previous_host_time=now;
    if(!attached||!scene3d_runtime_advance(runtime,elapsed)||!scene3d_runtime_active(runtime)||
       runtime->stats.paused||runtime->stats.time_scale==0) driver->play_state=ANIM_PLAY_FINISHED;
}
static bool scene3d_runtime_schedule(Scene3dRuntime* runtime) {
    bool active=scene3d_runtime_active(runtime)&&!runtime->stats.paused&&!runtime->stats.faulted&&runtime->stats.time_scale>0;
    // never cancel the driver from inside its own stage callback.
    if(runtime->sampling) return true;
    if(!active&&runtime->driver) animation_scheduler_cancel(runtime->document->state->animation_scheduler,runtime->driver);
    if(active&&!runtime->driver) {
        if(!runtime->document->state&&!state_store_create(runtime->document)) return false;
        runtime->previous_host_time=0;
        runtime->driver=animation_clock_driver_start(runtime->document->state->animation_scheduler,
            ANIM_TIMELINE,runtime,scene3d_runtime_tick,scene3d_runtime_released);
        return runtime->driver!=nullptr;
    }
    return true;
}
Scene3dRuntime* scene3d_runtime_create(DomElement* root) {
    if(!root||!root->doc) return nullptr;
    auto* runtime=(Scene3dRuntime*)mem_calloc(1,sizeof(Scene3dRuntime),MEM_CAT_RENDER);
    if(!runtime) return nullptr;
    runtime->document=root->doc;runtime->root=dom_node_ref(root);
    runtime->stats.timestep=1.0/120;runtime->stats.time_scale=1;runtime->stats.generation=1;runtime->max_substeps=8;
    return runtime;
}
void scene3d_runtime_destroy(Scene3dRuntime* runtime) {
    if(!runtime) return;
    if(runtime->driver) animation_scheduler_cancel(runtime->document->state->animation_scheduler,runtime->driver);
    mem_free(runtime);
}
void scene3d_runtime_follow_rebuild(Scene3dRuntime* runtime,DomNode* old_root,DomNode* new_root) {
    if(runtime) view_state_ref_follow_rebuild(runtime->document,&runtime->root,old_root,new_root);
}
bool scene3d_runtime_bind(Scene3dRuntime* runtime,Scene3dTickPhase phase,void* owner,Scene3dTickFn tick) {
    if(!runtime||(unsigned)phase>=SCENE3D_TICK_PHASES||!owner||!tick||runtime->services[phase].owner) return false;
    runtime->services[phase]={owner,tick,false};return true;
}
bool scene3d_runtime_enable(Scene3dRuntime* runtime,Scene3dTickPhase phase,void* owner,bool enabled) {
    if(!runtime||(unsigned)phase>=SCENE3D_TICK_PHASES||runtime->services[phase].owner!=owner) return false;
    runtime->services[phase].enabled=enabled;return scene3d_runtime_schedule(runtime);
}
bool scene3d_runtime_replace(Scene3dRuntime* runtime,Scene3dTickPhase phase,void* previous,void* owner,Scene3dTickFn tick) {
    if(!runtime||runtime->sampling||!runtime->stats.paused||(unsigned)phase>=SCENE3D_TICK_PHASES||
        runtime->services[phase].owner!=previous||!owner||!tick)return false;
    // a prepared replay transfers its owner only after every sampled tick succeeded.
    runtime->services[phase]={owner,tick,false};return true;
}
void scene3d_runtime_unbind(Scene3dRuntime* runtime,Scene3dTickPhase phase,void* owner) {
    if(!runtime||(unsigned)phase>=SCENE3D_TICK_PHASES||runtime->services[phase].owner!=owner) return;
    runtime->services[phase]={};
    // removing an owner cannot restart another service while document teardown is unwinding.
    if(!scene3d_runtime_active(runtime)) scene3d_runtime_schedule(runtime);
}
bool scene3d_runtime_can_configure(const Scene3dRuntime* runtime,double timestep,unsigned max_substeps) {
    return runtime&&!runtime->sampling&&isfinite(timestep)&&timestep>0&&max_substeps;
}
bool scene3d_runtime_configure(Scene3dRuntime* runtime,double timestep,unsigned max_substeps) {
    if(!scene3d_runtime_can_configure(runtime,timestep,max_substeps)) return false;
    if(timestep!=runtime->stats.timestep) {
        // structural world replacement starts a new tick domain, not an altered old tick duration.
        runtime->stats.ticks=0;runtime->stats.time=0;runtime->stats.debt=0;runtime->stats.interpolation=0;
        runtime->animation_ahead=0;
        runtime->stats.generation++;
    }
    runtime->stats.timestep=timestep;runtime->max_substeps=max_substeps;return true;
}
bool scene3d_runtime_pause(Scene3dRuntime* runtime,bool paused) {
    if(!runtime||runtime->sampling) return false;
    runtime->stats.paused=paused;return scene3d_runtime_schedule(runtime);
}
bool scene3d_runtime_time_scale(Scene3dRuntime* runtime,double scale) {
    if(!runtime||runtime->sampling||!isfinite(scale)||scale<0) return false;
    runtime->stats.time_scale=scale;return scene3d_runtime_schedule(runtime);
}
bool scene3d_runtime_step(Scene3dRuntime* runtime,size_t ticks) {
    if(!runtime||runtime->sampling||runtime->stats.faulted||ticks>100000||!scene3d_runtime_pause(runtime,true)) return false;
    runtime->sampling=true;
    for(size_t i=0;i<ticks;i++) if(!scene3d_runtime_tick_once(runtime,true)) {runtime->sampling=false;return false;}
    runtime->sampling=false;runtime->stats.debt=0;runtime->stats.interpolation=0;return true;
}
bool scene3d_runtime_reset_clock(Scene3dRuntime* runtime,uint64_t tick) {
    if(!runtime||runtime->sampling||!runtime->stats.paused||!isfinite((double)tick*runtime->stats.timestep)) return false;
    runtime->stats.ticks=tick;runtime->stats.time=(double)tick*runtime->stats.timestep;
    runtime->stats.debt=0;runtime->stats.interpolation=0;runtime->stats.faulted=false;
    runtime->animation_ahead=0;
    runtime->stats.generation++;runtime->diagnostic=nullptr;return true;
}
bool scene3d_runtime_stats(Scene3dRuntime* runtime,Scene3dRuntimeStats* stats) {
    if(!runtime||!stats) return false;
    *stats=runtime->stats;stats->active_services=scene3d_runtime_active(runtime);return true;
}
const char* scene3d_runtime_diagnostic(Scene3dRuntime* runtime) {return runtime?runtime->diagnostic:nullptr;}

static Item scene3d_runtime_value(Scene3dRuntime* runtime,bool capabilities) {
    RootedValueBuilder result;
    Scene3dRuntimeStats stats={};scene3d_runtime_stats(runtime,&stats);
    if(capabilities) {
        const char* available[]={"fixed_step","animation","rigid_sphere_box","rigid_capsule","rigid_convex","distance_queries","mesh_environment","heightfield_environment","collision_resources","shape_overlap","shape_cast","all_hit_queries","ccd","ccd_sphere",
            "joints","joint_distance","joint_spherical","joint_fixed","joint_hinge","joint_prismatic","joint_cone_twist","joint_breaks","character_controller","character_capsule","physical_bones","ragdoll_modes","ragdoll_replay","ragdolls"};
        const char* pending[]={"navigation","ccd_convex","animation_graphs"};
        for(const char* key:available) result.put(key,Item{.item=b2it(true)});
        for(const char* key:pending) result.put(key,Item{.item=b2it(false)});
    } else {
        result.put("tick",Item{.item=i2it((int64_t)stats.ticks)});result.put("time",push_d(stats.time));
        result.put("timestep",push_d(stats.timestep));result.put("debt",push_d(stats.debt));
        result.put("time_scale",push_d(stats.time_scale));result.put("paused",Item{.item=b2it(stats.paused)});
        result.put("active_services",Item{.item=i2it(stats.active_services)});
        result.put("faulted",Item{.item=b2it(stats.faulted)});
    }
    return result.value.get();
}
Item scene3d_runtime_snapshot(Scene3dRuntime* runtime) {return scene3d_runtime_value(runtime,false);}

Item scene3d_runtime_operate(Scene3dRuntime* runtime,Item request) {
    RootFrame roots(1);Rooted<Item> rooted(roots,request);
    ItemReader read(rooted.get().to_const());const char* error=nullptr;
    if(!runtime||!read.isMap()) error="scene3d: requires a live scene and an operation map";
    auto field=[&](const char* name){return read.asMap().get(name);};
    auto number=[&](const char* key,double fallback,double* out) {
        auto value=field(key);
        return (value.isNull()||value.isInt()||value.isFloat())&&physics_scene_real(value,fallback,out);
    };
    auto name=error?ItemReader():field("operation");
    const char* op=name.isString()?name.asString()->chars:name.isSymbol()?name.asSymbol()->chars:nullptr;
    if(!error&&!op) error="scene3d: requires an operation";
    if(!error) {
        if(!strcmp(op,"pause")||!strcmp(op,"play")) {
            if(!scene3d_runtime_pause(runtime,!strcmp(op,"pause"))) error="scene3d: cannot change playback";
        } else if(!strcmp(op,"step")) {
            double ticks;
            if(!number("ticks",1,&ticks)||ticks<0||ticks>100000||floor(ticks)!=ticks||!scene3d_runtime_step(runtime,(size_t)ticks)) // INT_CAST_OK: validated bounded whole tick count
                error="scene3d: invalid tick count or faulted scene";
        } else if(!strcmp(op,"time_scale")) {
            double scale;
            if(!number("value",NAN,&scale)||!scene3d_runtime_time_scale(runtime,scale)) error="scene3d: invalid time scale";
        } else if(strcmp(op,"read")&&strcmp(op,"capabilities")) error="scene3d: unsupported operation";
    }
    if(!error&&runtime->stats.faulted&&strcmp(op,"read")&&strcmp(op,"capabilities")&&strcmp(op,"pause")) error=runtime->diagnostic;
    if(error) return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,error,nullptr));
    return scene3d_runtime_value(runtime,!strcmp(op,"capabilities"));
}
