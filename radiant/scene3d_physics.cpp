#include "scene3d_physics.hpp"
#include "scene3d_source.hpp"
#include "animation_value.hpp"
#include "event.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/dom/dom_events.h"
#include "../lambda/runtime/lambda-physics.hpp"
#include "../lambda/runtime/lambda-error.h"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "../lambda/dom/realm/dom_realm.h"
#include "../lambda/js/js_runtime.h"
#include "../lib/physics/physics_math.h"
#include "../lib/mem.h"

struct Scene3dPhysics {
    DomDocument* document=nullptr;
    DomNodeRef root={};
    PhysicsScene scene;
    PhysicsWorld* world=nullptr;
    PhysicsState published={};
    AnimationInstance* driver=nullptr;
    uint64_t epoch=0,generation=1;
    double previous_time=0,debt=0;
    bool automatic=false,playing=false;
};
static const char* physics_dom_tag(void* node){return ((DomElement*)node)->local_name();}
static ItemReader physics_dom_field(void* node,const char* key){
    Element* backing=dom_element_backing((DomElement*)node);return backing?ElementReader(backing).get_attr(key):ItemReader();
}
static size_t physics_dom_count(void* node){size_t count=0;for(DomNode* c=((DomElement*)node)->first_child;c;c=c->next_sibling)if(c->is_element())count++;return count;}
static void* physics_dom_child(void* node,size_t index){for(DomNode* c=((DomElement*)node)->first_child;c;c=c->next_sibling)if(c->is_element()){if(!index--)return c->as_element();}return nullptr;}
static const char* physics_dom_text(void* node,const char* key){return ((DomElement*)node)->get_attribute(key);}
static PhysicsSceneSource physics_dom_source(){return {physics_dom_tag,physics_dom_field,physics_dom_count,physics_dom_child,physics_dom_text};}
static Item physics_scene_error(const char* message){return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,message,nullptr));}
static void physics_scene_released(AnimationInstance* driver){((Scene3dPhysics*)driver->target)->driver=nullptr;}
struct PhysicsDomEvent {const PhysicsScene* scene;const PhysicsEvent* event;const char* type;};
static Item physics_scene_event(void* data) {
    auto* source=(PhysicsDomEvent*)data;RootFrame roots(2);
    Rooted<Item> event(roots,js_create_event(source->type,true,false));
    Rooted<Item> detail(roots,physics_event_value(*source->scene,*source->event));
    dom_realm_set_name(event.get(),"detail",detail.get());return event.get();
}
static bool physics_scene_step(Scene3dPhysics* state,size_t ticks,PhysicsError* error){
    uint64_t previous_tick=state->published.tick;
    bool complete=physics_step(state->world,ticks,&state->published,error);
    // a failed multi-tick call can still commit a prefix; refresh metadata with
    // the borrowed body snapshot before rendering or delivering its events.
    if(!complete)physics_read(state->world,&state->published,nullptr);
    // the shared queue copies event values now and delivers handlers after the solver has unwound.
    DomNode* root=dom_node_ref_validate(state->document,state->root);
    // another step on an already faulted world must not deliver its old events again.
    if(root&&(complete||state->published.tick!=previous_tick))for(size_t i=0;i<state->published.event_count;i++) {
        const auto& event=state->published.events[i];const char* type=event.sensor?(event.started?"physicssensorenter":"physicssensorexit"):
            (event.started?"physicscontactstart":"physicscontactend");PhysicsDomEvent data={&state->scene,&event,type};
        if(!radiant_queue_timing_event((UiContext*)state->document->js.host_ui_context,root->as_element(),physics_scene_event,&data,type)) {
            if(complete&&error)error->message="scene3d physics: cannot queue contact event";complete=false;
        }
    }
    state->generation++;if(state->document->state)doc_state_request_repaint(state->document->state);return complete;
}
static void physics_scene_tick(AnimationInstance* instance,float){
    auto* state=(Scene3dPhysics*)instance->target;double now=animation_clock_time(state->document->state->animation_scheduler,instance,0);
    double delta=now-state->previous_time;state->previous_time=now;
    DomNode* root=dom_node_ref_validate(state->document,state->root);
    if(!root||!state->playing){instance->play_state=ANIM_PLAY_FINISHED;return;}
    // resuming a background document starts a new pacing interval, not a catch-up burst.
    if(delta<0||delta>1){state->debt=0;return;}state->debt+=delta;
    size_t ticks=(size_t)floor(state->debt/state->scene.desc.timestep);
    if(ticks>state->scene.max_substeps)ticks=state->scene.max_substeps;
    PhysicsError error={};
    if(state->debt>2|| (ticks&&!physics_scene_step(state,ticks,&error))){
        log_error("scene3d physics tick: %s",error.message?error.message:"simulation pacing debt exceeded");
        state->playing=false;instance->play_state=ANIM_PLAY_FINISHED;return;
    }
    state->debt-=ticks*state->scene.desc.timestep;
}
static bool physics_scene_play(Scene3dPhysics* state,bool playing){
    state->playing=playing;
    if(!playing&&state->driver)animation_scheduler_cancel(state->document->state->animation_scheduler,state->driver);
    if(playing&&state->automatic&&!state->driver&&state->world){
        if(!state->document->state&&!state_store_create(state->document))return false;
        state->previous_time=0;state->debt=0;
        state->driver=animation_clock_driver_start(state->document->state->animation_scheduler,ANIM_TIMELINE,state,physics_scene_tick,physics_scene_released);
        return state->driver!=nullptr;
    }
    return true;
}
static bool physics_scene_animation_conflict(const PhysicsScene& scene,DomElement* node,unsigned depth=0){
    if(depth>64)return true;
    if(!strcmp(node->local_name(),"keyframe-track")){
        const char* path=scene3d_text(node,"path");
        if(path)for(size_t i=0;i<scene.bindings.size();i++){
            const auto& binding=scene.bindings[i];
            const char* visual=scene3d_text((DomElement*)binding.node,"id");size_t length=visual?strlen(visual):0;
            if(length&&!strncmp(path,visual,length)&&path[length]=='.'&&
                (!strcmp(path+length+1,"position")||!strcmp(path+length+1,"quaternion")||!strcmp(path+length+1,"rotation")))return true;
        }
    }
    for(DomNode* c=node->first_child;c;c=c->next_sibling)if(c->is_element()&&physics_scene_animation_conflict(scene,c->as_element(),depth+1))return true;
    return false;
}
Scene3dPhysics* scene3d_physics_create(DomElement* root,char* diagnostic,size_t capacity,bool automatic){
    void* memory=mem_alloc(sizeof(Scene3dPhysics),MEM_CAT_RENDER);if(!memory)return nullptr;
    auto* state=new(memory) Scene3dPhysics();state->document=root->doc;state->root=dom_node_ref(root);state->epoch=root->doc->mutation_epoch;state->automatic=automatic;
    PhysicsError error={};
    bool valid=physics_scene_compile(&state->scene,physics_dom_source(),root);
    if(valid&&physics_scene_animation_conflict(state->scene,root)){valid=false;error.message="physics body animation tracks require tick sampling support";}
    if(valid&&!state->scene.bodies.empty())valid=physics_create(&state->scene.desc,&state->world,&error)&&physics_read(state->world,&state->published,&error);
    if(valid)valid=physics_scene_play(state,state->scene.playing);
    if(!valid){const char* message=error.message?error.message:state->scene.diagnostic;strncpy(diagnostic,message,capacity-1);diagnostic[capacity-1]=0;scene3d_physics_destroy(state);return nullptr;}
    return state;
}
void scene3d_physics_destroy(Scene3dPhysics* state){
    if(!state)return;physics_scene_play(state,false);physics_destroy(state->world);state->~Scene3dPhysics();mem_free(state);
}
bool scene3d_physics_matches(Scene3dPhysics* state,DomElement* root){
    if(!state||state->document!=root->doc)return false;if(state->epoch==root->doc->mutation_epoch)return true;
    PhysicsScene current;if(!physics_scene_compile(&current,physics_dom_source(),root))return false;
    // tracks added after initial compilation must obey the same transform ownership rule.
    if(physics_scene_animation_conflict(current,root))return false;
    if(current.bodies.size()!=state->scene.bodies.size()||current.colliders.size()!=state->scene.colliders.size()||
        memcmp(current.bodies.data(),state->scene.bodies.data(),current.bodies.size()*sizeof(PhysicsBodyDesc))||
        memcmp(current.colliders.data(),state->scene.colliders.data(),current.colliders.size()*sizeof(PhysicsColliderDesc))||
        current.desc.timestep!=state->scene.desc.timestep||current.desc.iterations!=state->scene.desc.iterations||current.desc.length_scale!=state->scene.desc.length_scale||
        memcmp(&current.desc.gravity,&state->scene.desc.gravity,sizeof(PhysicsVec)))return false;
    for(size_t i=0;i<current.bindings.size();i++)if(current.bindings[i].node!=state->scene.bindings[i].node)return false;
    state->scene.max_substeps=current.max_substeps;
    if(current.playing!=state->scene.playing){state->scene.playing=current.playing;physics_scene_play(state,current.playing);}
    state->epoch=root->doc->mutation_epoch;return true;
}
uint64_t scene3d_physics_generation(Scene3dPhysics* state){return state?state->generation:0;}
bool scene3d_physics_advance(Scene3dPhysics* state,size_t ticks,PhysicsState* result,PhysicsError* error){
    if(!state||!state->world||!result)return false;
    physics_scene_play(state,false);bool complete=physics_scene_step(state,ticks,error);
    *result=state->published;return complete;
}
bool scene3d_physics_pose(Scene3dPhysics* state,DomElement* node,Scene3dMatrix* world){
    if(!state||!state->world)return false;
    for(size_t i=0;i<state->scene.bindings.size();i++)if(state->scene.bindings[i].node==node){
        const auto& pose=state->published.bodies[i].pose;double q[4]={pose.rotation.x,pose.rotation.y,pose.rotation.z,pose.rotation.w},matrix[16];
        if(!animation_quaternion_matrix(q,matrix))return false;*world=scene3d_identity();
        for(unsigned j=0;j<16;j++)world->v[j]=(float)matrix[j];
        world->v[12]=(float)pose.position.x;world->v[13]=(float)pose.position.y;world->v[14]=(float)pose.position.z;return true;
    }
    return false;
}
Item scene3d_physics_operate(Scene3dPhysics* state,Item request){
    RootFrame roots(1);Rooted<Item> rooted(roots,request);if(!state||!state->world)return physics_scene_error("scene3d physics: scene has no physics world");
    ItemReader read(rooted.get().to_const());ItemReader operation=read.asMap().get("operation");const char* op=operation.isString()?operation.asString()->chars:operation.isSymbol()?operation.asSymbol()->chars:nullptr;
    PhysicsError error={};
    if(!op)return physics_scene_error("scene3d physics: requires an operation");
    if(!strcmp(op,"play")||!strcmp(op,"pause")){if(!physics_scene_play(state,!strcmp(op,"play")))return physics_scene_error("scene3d physics: cannot start scheduler");}
    else if(!strcmp(op,"step")){
        double ticks;if(!physics_scene_real(read.asMap().get("ticks"),1,&ticks)||ticks<0||ticks>100000||floor(ticks)!=ticks)return physics_scene_error("scene3d physics: invalid tick count");
        if(!scene3d_physics_advance(state,(size_t)ticks,&state->published,&error))return physics_scene_error(error.message);
    }else if(!strcmp(op,"reset")){
        physics_scene_play(state,false);PhysicsWorld* replacement=nullptr;if(!physics_create(&state->scene.desc,&replacement,&error))return physics_scene_error(error.message);
        physics_destroy(state->world);state->world=replacement;physics_read(state->world,&state->published,&error);state->generation++;
    }else if(!strcmp(op,"apply")){
        lam::ArrayList<PhysicsCommand> commands;const char* message=nullptr;
        if(!physics_commands_read(state->scene,read.asMap().get("commands").item(),&commands,&message))return physics_scene_error(message);
        if(!physics_apply(state->world,commands.data(),commands.size(),&error))return physics_scene_error(error.message);
        physics_read(state->world,&state->published,&error);state->generation++;
    }else if(!strcmp(op,"query"))return physics_queries_value(state->scene,state->world,read.asMap().get("queries").item());
    else if(!strcmp(op,"events"))return physics_events_value(state->scene,state->published);
    else if(strcmp(op,"read"))return physics_scene_error("scene3d physics: unsupported operation");
    if(state->document->state)doc_state_request_repaint(state->document->state);
    return physics_state_value(state->scene,state->published,ItemNull);
}
