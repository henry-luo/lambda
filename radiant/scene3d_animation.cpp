#include "scene3d_animation.hpp"
#include "scene3d_source.hpp"
#include "event.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lib/mem.h"
#include "../lib/mem_factory.h"
#include "../lib/str.h"
#include "../lib/hash.h"
#include "../lib/log.h"

struct Scene3dAnimationBinding {
    Scene3dAnimationBinding* next;
    DomNodeRef node;
    uint64_t identity;
    char property[32];
    AnimationValue value;
};
struct Scene3dAnimationClip {
    Scene3dAnimationClip* next;
    char name[128];
    AnimationActionState* action;
    bool autoplay, controlled;
};
// a <motion-controller> (Lambda_Pkg_Motion.md §9.2): the template owns the
// requested state and cue, the host owns playback time, fades and `next`
// transitions (S12.1.5v2)
struct Scene3dMotionState {
    char name[64];
    Scene3dAnimationClip* clip;
    AnimationLoopMode loop;
    int next;           // state index, or -1
    float fade;         // into this state; negative uses the controller's
    float time_scale;
};
struct Scene3dMotionController {
    Scene3dMotionController* next;
    DomNodeRef node;
    char id[128];
    Scene3dMotionState* states;
    unsigned count;
    float fade, cue;
    int requested, playing;
};
static constexpr unsigned SCENE_MOTION_CONTROLLER_LIMIT = 16, SCENE_MOTION_STATE_LIMIT = 64;
struct Scene3dAnimationState {
    DomDocument* document;
    DomNodeRef root;
    Pool* pool;
    AnimationMixerState* mixer;
    Scene3dAnimationBinding* bindings;
    Scene3dAnimationClip* clips;
    Scene3dMotionController* controllers;
    DomNodeRef pending[SCENE_MOTION_CONTROLLER_LIMIT];
    unsigned pending_count;
    AnimationInstance* driver;
    uint64_t epoch, generation, source_hash, number_count, text_bytes;
    unsigned binding_count, clip_count;
    double previous_host_time;
    bool automatic;
};
static bool scene3d_animation_connected(Scene3dAnimationState* state, DomNodeRef ref) {
    DomNode* node=dom_node_ref_validate(state->document,ref);
    bool in_root=false;
    for(unsigned depth=0;node&&depth<128;node=node->parent,depth++) {
        if(node==state->root.address) in_root=true;
        if(node==state->document->root) return in_root;
    }
    return false;
}
static Scene3dAnimationBinding* scene3d_animation_binding(Scene3dAnimationState* state,uint64_t identity) {
    for(auto* binding=state->bindings;binding;binding=binding->next) if(binding->identity==identity) return binding;
    return nullptr;
}
static bool scene3d_animation_initial(DomElement* node,const char* property,AnimationValue* value,Pool* pool);
static bool scene3d_animation_read(void* owner,uint64_t identity,AnimationValue* value) {
    auto* state=(Scene3dAnimationState*)owner;auto* binding=scene3d_animation_binding(state,identity);
    if(!binding||!scene3d_animation_connected(state,binding->node)) return false;
    // reactivation captures the current authored base; inactive overlays never hide later DOM edits.
    return scene3d_animation_initial(binding->node.address->as_element(),binding->property,value,state->pool);
}
static bool scene3d_animation_write(void* owner,uint64_t identity,const AnimationValue* value) {
    auto* state=(Scene3dAnimationState*)owner;auto* binding=scene3d_animation_binding(state,identity);
    if(!binding||!scene3d_animation_connected(state,binding->node)) return false;
    binding->value=*value;state->generation++;
    if(state->document->state) doc_state_request_repaint(state->document->state);
    return true;
}
bool scene3d_animation_value(Scene3dAnimationState* state,DomElement* node,const char* property,AnimationValue* value) {
    if(!state||!node||!value) return false;
    for(auto* binding=state->bindings;binding;binding=binding->next)
        if(binding->node.address==node&&binding->node.expected_id==node->DomNode::id&&!strcmp(binding->property,property)) {
            if(!animation_mixer_property_active(state->mixer,binding->identity)) return false;
            *value=binding->value;return true;
        }
    return false;
}
static DomElement* scene3d_animation_find(DomElement* node,const char* name,unsigned depth=0) {
    if(depth>64) return nullptr;
    const char* id=scene3d_text(node,"id");
    if(id&&!strcmp(id,name)) return node;
    for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) {
        DomElement* found=scene3d_animation_find(child->as_element(),name,depth+1);if(found) return found;
    }
    return nullptr;
}
static bool scene3d_animation_initial(DomElement* node,const char* property,AnimationValue* value,Pool* pool) {
    const char* tag=node->local_name();value->count=1;value->type=ANIMATION_NUMBER;
    if(!strcmp(property,"morph-weights")&&!strcmp(tag,"mesh")) {
        float* values;unsigned count;
        if(!scene3d_numbers(node,property,pool,&values,&count,2)||!count) return false;
        value->type=ANIMATION_VECTOR;value->count=count;
        for(unsigned i=0;i<count;i++) value->numbers[i]=values[i];pool_free(pool,values);return true;
    }
    if(!strcmp(property,"position")||!strcmp(property,"scale")) {
        Scene3dVec vector;
        if(!scene3d_vector(node,property,!strcmp(property,"scale")?Scene3dVec{1,1,1}:Scene3dVec{},&vector)) return false;
        value->type=ANIMATION_VECTOR;value->count=3;
        value->numbers[0]=vector.x;value->numbers[1]=vector.y;value->numbers[2]=vector.z;return true;
    }
    if(!strcmp(property,"quaternion")) {
        float quaternion[4];bool authored;
        if(!scene3d_components(node,"quaternion",quaternion,4,&authored)) return false;
        if(authored) {
            value->type=ANIMATION_QUATERNION;value->count=4;
            for(unsigned i=0;i<4;i++) value->numbers[i]=quaternion[i];
            return animation_quaternion_normalize(value->numbers);
        }
        Scene3dVec rotation;if(!scene3d_vector(node,"rotation",{},&rotation)) return false;
        double cx=cos(rotation.x*.5),sx=sin(rotation.x*.5),cy=cos(rotation.y*.5),sy=sin(rotation.y*.5),cz=cos(rotation.z*.5),sz=sin(rotation.z*.5);
        value->type=ANIMATION_QUATERNION;value->count=4;
        value->numbers[0]=sx*cy*cz+cx*sy*sz;value->numbers[1]=cx*sy*cz-sx*cy*sz;
        value->numbers[2]=cx*cy*sz+sx*sy*cz;value->numbers[3]=cx*cy*cz-sx*sy*sz;
        return animation_quaternion_normalize(value->numbers);
    }
    if(!strcmp(property,"visible")) {
        bool visible;if(!scene3d_flag(node,"visible",true,&visible)) return false;
        value->type=ANIMATION_BOOLEAN;value->numbers[0]=visible;return true;
    }
    if(!strcmp(property,"name")) {
        const char* name=scene3d_text(node,"name");if(!name) name="";
        if(strlen(name)>=sizeof(value->text)) return false;
        str_copy(value->text,sizeof(value->text),name,strlen(name));value->type=ANIMATION_STRING;return true;
    }
    if(!strcmp(property,"color")&&(!strcmp(tag,"material")||!strcmp(tag,"light"))) {
        float color[4];if(!scene3d_color(node,"color","#ffffff",color)) return false;
        value->type=ANIMATION_COLOR;value->count=3;
        for(unsigned i=0;i<3;i++) value->numbers[i]=color[i];return true;
    }
    float fallback=0;
    if(!strcmp(tag,"material")&&!strcmp(property,"opacity")) fallback=1;
    else if(!strcmp(tag,"light")&&!strcmp(property,"intensity")) fallback=1;
    else if(!strcmp(tag,"camera")&&!strcmp(property,"fov")) fallback=50;
    else if(!strcmp(tag,"camera")&&!strcmp(property,"view_height")) fallback=4;
    else if(!strcmp(tag,"camera")&&!strcmp(property,"near")) fallback=.1f;
    else if(!strcmp(tag,"camera")&&!strcmp(property,"far")) fallback=1000;
    else if(strcmp(tag,"camera")||strcmp(property,"aspect")) return false;
    float number;if(!scene3d_number(node,property,fallback,&number)) return false;
    value->numbers[0]=number;return true;
}
static Scene3dAnimationBinding* scene3d_animation_resolve(Scene3dAnimationState* state,DomElement* root,const char* path) {
    if(!path||strlen(path)>=192) return nullptr;
    const char* dot=strchr(path,'.');if(!dot||dot==path||!dot[1]) return nullptr;
    char name[128];size_t length=dot-path;if(length>=sizeof(name)) return nullptr;
    str_copy(name,sizeof(name),path,length);
    DomElement* node=scene3d_animation_find(root,name);const char* property=dot+1;
    if(node&&!strncmp(property,"material.",9)) {
        DomElement* material=nullptr;
        const char* reference=scene3d_text(node,"material");
        if(reference) material=scene3d_animation_find(root,reference);
        else for(DomNode* child=node->first_child;child;child=child->next_sibling)
            if(child->is_element()&&!strcmp(child->as_element()->local_name(),"material")) {material=child->as_element();break;}
        node=material;property+=9;
    }
    if(!node||strlen(property)>=32) return nullptr;
    for(auto* binding=state->bindings;binding;binding=binding->next)
        if(binding->node.address==node&&!strcmp(binding->property,property)) return binding;
    if(state->binding_count>=1024) return nullptr;
    auto* binding=(Scene3dAnimationBinding*)pool_calloc(state->pool,sizeof(Scene3dAnimationBinding));if(!binding) return nullptr;
    if(!scene3d_animation_initial(node,property,&binding->value,state->pool)) return nullptr;
    binding->node=dom_node_ref(node);binding->identity=++state->binding_count;
    str_copy(binding->property,sizeof(binding->property),property,strlen(property));
    binding->next=state->bindings;state->bindings=binding;return binding;
}
static bool scene3d_animation_numbers(Scene3dAnimationState* state,DomElement* node,const char* name,const double** values,unsigned* count) {
    float* source=nullptr;
    if(!scene3d_numbers(node,name,state->pool,&source,count,1048576)) return false;
    if(*count>1048576-state->number_count) return false;state->number_count+=*count;
    auto* result=(double*)pool_alloc(state->pool,*count*sizeof(double));if(*count&&!result) return false;
    for(unsigned i=0;i<*count;i++) result[i]=source[i];pool_free(state->pool,source);*values=result;return true;
}
static bool scene3d_animation_discrete_values(Scene3dAnimationState* state,DomElement* node,AnimationTrackView* track) {
    if(track->keys>32768) return false;
    Element* backing=dom_element_backing(node);
    ItemReader source=backing?ElementReader(backing).get_attr("values"):ItemReader();
    const char* text=node->get_attribute("values");
    auto* numbers=(double*)pool_calloc(state->pool,track->keys*sizeof(double));
    auto* strings=(const char**)pool_calloc(state->pool,track->keys*sizeof(char*));
    if(!numbers||!strings) return false;
    if(!text&&(!source.isArray()||(uint64_t)source.asArray().length()!=track->keys)) return false;
    const char* cursor=text;
    for(unsigned i=0;i<track->keys;i++) {
        if(text) {
            cursor=str_skip_ascii_space(cursor);const char* end=cursor;
            if(track->type==ANIMATION_STRING) {while(*end&&*end!='|') end++;}
            else {while(*end&&!strchr(" ,\t\r\n",*end)) end++;}
            size_t length=end-cursor;if(!length||length>=256||state->text_bytes+length+1>8*1024*1024) return false;
            state->text_bytes+=length+1;
            if(track->type==ANIMATION_STRING) {
                char* value=(char*)pool_alloc(state->pool,length+1);if(!value) return false;
                memcpy(value,cursor,length);value[length]=0;strings[i]=value;
            } else if((length==4&&!strncmp(cursor,"true",4))||(length==1&&*cursor=='1')) numbers[i]=1;
            else if(!((length==5&&!strncmp(cursor,"false",5))||(length==1&&*cursor=='0'))) return false;
            cursor=end;if(*cursor) cursor++;
        } else {
            ItemReader value=source.asArray().get(i);
            if(track->type==ANIMATION_BOOLEAN) {if(!value.isBool()) return false;numbers[i]=value.asBool();}
            else {
                if(!value.isString()||value.asString()->len>=256||memchr(value.asString()->chars,0,value.asString()->len)) return false;
                size_t length=value.asString()->len;if(state->text_bytes+length+1>8*1024*1024) return false;
                state->text_bytes+=length+1;char* copy=(char*)pool_alloc(state->pool,length+1);if(!copy) return false;
                memcpy(copy,value.asString()->chars,length);copy[length]=0;strings[i]=copy;
            }
        }
    }
    if(text&&*str_skip_ascii_space(cursor)) return false;
    if(track->type==ANIMATION_STRING) track->strings=strings;else track->values=numbers;
    return true;
}
static bool scene3d_animation_track(Scene3dAnimationState* state,DomElement* root,DomElement* node,AnimationChannelView* channel) {
    if(strcmp(node->local_name(),"keyframe-track")||node->first_child) return false;
    auto* binding=scene3d_animation_resolve(state,root,scene3d_text(node,"path"));if(!binding) return false;
    AnimationTrackView& track=channel->track;channel->property=binding->identity;
    track.type=binding->value.type;track.components=binding->value.count;
    const char* declared_type=scene3d_text(node,"type");
    const char* types[]={"number","vector","color","quaternion","bool","string"};
    if(declared_type&&strcmp(declared_type,types[track.type])) return false;
    const char* interpolation=scene3d_text(node,"interpolation");
    track.interpolation=!interpolation||!strcmp(interpolation,"linear")?ANIMATION_LINEAR:
        !strcmp(interpolation,"discrete")?ANIMATION_DISCRETE:!strcmp(interpolation,"smooth")?ANIMATION_SMOOTH:
        !strcmp(interpolation,"bezier")?ANIMATION_BEZIER:!strcmp(interpolation,"hermite")?ANIMATION_HERMITE:(AnimationInterpolation)99;
    if((track.type==ANIMATION_BOOLEAN||track.type==ANIMATION_STRING)&&!interpolation) track.interpolation=ANIMATION_DISCRETE;
    unsigned values=0,incoming=0,outgoing=0;
    if(!scene3d_animation_numbers(state,node,"times",&track.times,&track.keys)||!track.keys) return false;
    if(track.type==ANIMATION_BOOLEAN||track.type==ANIMATION_STRING)
        return scene3d_animation_discrete_values(state,node,&track)&&animation_track_validate(track);
    if(!scene3d_animation_numbers(state,node,"values",&track.values,&values)||values!=track.keys*track.components||
        !scene3d_animation_numbers(state,node,"in-tangents",&track.in_tangents,&incoming)||
        !scene3d_animation_numbers(state,node,"out-tangents",&track.out_tangents,&outgoing)) return false;
    unsigned tangent_values=values*(track.interpolation==ANIMATION_HERMITE?1:2);
    if((incoming&&incoming!=tangent_values)||(outgoing&&outgoing!=tangent_values)) return false;
    return animation_track_validate(track);
}
static bool scene3d_motion_enter(Scene3dAnimationState* state,Scene3dMotionController* c,int index,bool fade,bool announce);
static void scene3d_animation_event(void* owner,uint64_t identity,const char* type,double detail) {
    auto* state=(Scene3dAnimationState*)owner;
    // a finished `once` state hands over to its `next` state
    if(!strcmp(type,"finished")) for(auto* c=state->controllers;c;c=c->next) {
        if(c->playing<0) continue;
        const auto& playing=c->states[c->playing];
        if(playing.next>=0&&playing.clip->action->identity==identity) scene3d_motion_enter(state,c,playing.next,true,true);
    }
    DomNode* root=dom_node_ref_validate(state->document,state->root);
    if(!root||!root->is_element()||!scene3d_animation_connected(state,state->root)) return;
    for(auto* clip=state->clips;clip;clip=clip->next) if(clip->action->identity==identity) {
        radiant_dispatch_scene_animation_event((UiContext*)state->document->js.host_ui_context,root->as_element(),
            type,clip->name,identity,detail,animation_mixer_time(state->mixer));return;
    }
}
static bool scene3d_animation_parse(Scene3dAnimationState* state,DomElement* root,DomElement* node,unsigned depth=0) {
    if(depth>64) return false;
    if(!strcmp(node->local_name(),"motion-controller")) {
        // resolved once every clip is parsed: a hand-written controller may precede its clips
        if(state->pending_count>=SCENE_MOTION_CONTROLLER_LIMIT) return false;
        state->pending[state->pending_count++]=dom_node_ref(node);return true;
    }
    if(!strcmp(node->local_name(),"animation-clip")) {
        if(!state->pool) state->pool=mem_pool_create((MemContext*)root->doc->services.mem_ctx,MEM_ROLE_RENDER,"scene3d.animation");
        if(!state->mixer) state->mixer=animation_mixer_create(state,{scene3d_animation_read,scene3d_animation_write,scene3d_animation_event});
        if(!state->pool||!state->mixer) return false;
        const char* name=scene3d_text(node,"id");
        if(!name||!*name||strlen(name)>=128||state->clip_count>=256) return false;
        for(auto* clip=state->clips;clip;clip=clip->next) if(!strcmp(clip->name,name)) return false;
        unsigned count=0;
        for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) count++;
        // a clip may animate position, rotation and scale of every bone (1024 tracks)
        if(!count||count>1024) return false;
        auto* channels=(AnimationChannelView*)pool_calloc(state->pool,count*sizeof(AnimationChannelView));if(!channels) return false;
        double inferred=0;unsigned index=0;
        for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) {
            auto& channel=channels[index++];if(!scene3d_animation_track(state,root,child->as_element(),&channel)) return false;
            inferred=fmax(inferred,channel.track.times[channel.track.keys-1]);
        }
        float duration;bool additive,autoplay;
        if(!scene3d_number(node,"duration",-1,&duration)||!scene3d_flag(node,"additive",false,&additive)||
            !scene3d_flag(node,"autoplay",false,&autoplay)) return false;
        auto* action=animation_mixer_action(state->mixer,{++state->clip_count,duration<0?inferred:duration,channels,count,additive});
        if(!action) return false;
        auto* clip=(Scene3dAnimationClip*)pool_calloc(state->pool,sizeof(Scene3dAnimationClip));if(!clip) return false;
        str_copy(clip->name,sizeof(clip->name),name,strlen(name));clip->action=action;clip->autoplay=autoplay;
        clip->next=state->clips;state->clips=clip;
        if(autoplay) {if(!animation_action_play(action)) return false;state->automatic=true;}
        return true;
    }
    for(DomNode* child=node->first_child;child;child=child->next_sibling)
        if(child->is_element()&&!scene3d_animation_parse(state,root,child->as_element(),depth+1)) return false;
    return true;
}
static uint64_t scene3d_animation_hash_item(ItemReader item,uint64_t hash,unsigned depth=0) {
    unsigned type=get_type_id(item.item());hash=hash_djb2_add_extend(hash,&type,sizeof(type));
    if(item.isString()||item.isSymbol()) {
        const char* chars=item.isString()?item.asString()->chars:item.asSymbol()->chars;
        size_t length=item.isString()?item.asString()->len:item.asSymbol()->len;
        return hash_djb2_add_extend(hash,chars,length+1);
    }
    if(item.isArray()&&depth<4) {
        ArrayReader values=item.asArray();int64_t length=values.length();hash=hash_djb2_add_extend(hash,&length,sizeof(length));
        for(int64_t i=0;i<length;i++) hash=scene3d_animation_hash_item(values.get(i),hash,depth+1);
        return hash;
    }
    double number=0;item_try_to_double(item.item(),&number);if(item.isBool()) number=item.asBool();
    return hash_djb2_add_extend(hash,&number,sizeof(number));
}
static uint64_t scene3d_animation_source_hash(DomElement* node,uint64_t hash=5381,unsigned depth=0) {
    if(depth>64) return 0;
    static const char* const clip_names[]={"id","duration","additive","autoplay","path","type","times","values","interpolation","in-tangents","out-tangents"};
    // a controller's requested `state` and `cue` stay out: changing them transitions playback instead of rebuilding it
    static const char* const controller_names[]={"id","fade"};
    static const char* const state_names[]={"name","clip","loop","next","fade","time-scale"};
    const char* local=node->local_name();
    const char* const* names=nullptr;size_t count=0;
    if(!strcmp(local,"animation-clip")||!strcmp(local,"keyframe-track")) {names=clip_names;count=sizeof(clip_names)/sizeof(*clip_names);}
    else if(!strcmp(local,"motion-controller")) {names=controller_names;count=sizeof(controller_names)/sizeof(*controller_names);}
    else if(!strcmp(local,"motion-state")) {names=state_names;count=sizeof(state_names)/sizeof(*state_names);}
    // content, not node identity: a template re-render rebuilds equal nodes, and
    // bindings separately require their bone and mesh nodes to stay connected
    if(names) {
        Element* backing=dom_element_backing(node);
        for(size_t i=0;i<count;i++) {
            const char* text=node->get_attribute(names[i]);
            if(text) hash=hash_djb2_add_extend(hash,text,strlen(text)+1);
            else hash=scene3d_animation_hash_item(backing?ElementReader(backing).get_attr(names[i]):ItemReader(),hash);
        }
    }
    for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element())
        hash=scene3d_animation_source_hash(child->as_element(),hash,depth+1);
    return hash;
}
// ---- motion controllers -----------------------------------------------------------

static int scene3d_motion_index(const Scene3dMotionController* c,const char* name) {
    if(name) for(unsigned i=0;i<c->count;i++) if(!strcmp(c->states[i].name,name)) return (int)i;  // INT_CAST_OK: state count is capped at 64
    return -1;
}
static void scene3d_motion_announce(Scene3dAnimationState* state,const Scene3dMotionController* c,const char* from,const char* to) {
    DomNode* root=dom_node_ref_validate(state->document,state->root);
    if(!root||!root->is_element()||!scene3d_animation_connected(state,state->root)) return;
    if(!radiant_dispatch_scene_state_event((UiContext*)state->document->js.host_ui_context,root->as_element(),
            c->id,from,to,animation_mixer_time(state->mixer)))
        log_debug("scene3d motion: %s state change %s -> %s was not delivered",c->id,from,to);
}
// play state `index` from its start; with `fade`, crossfade from the playing state
static bool scene3d_motion_enter(Scene3dAnimationState* state,Scene3dMotionController* c,int index,bool fade,bool announce) {
    const auto& target=c->states[index];
    auto* action=target.clip->action;
    int previous=c->playing;
    auto* from=previous>=0?c->states[previous].clip->action:nullptr;
    animation_action_reset(action);
    action->loop=target.loop;action->clamp=target.loop==ANIMATION_LOOP_ONCE;
    action->time_scale=target.time_scale;action->weight=1;
    if(!animation_action_play(action)) return false;
    double duration=target.fade>=0?target.fade:c->fade;
    if(from&&from!=action) {
        if(fade&&duration>0) { if(!animation_action_crossfade(from,action,duration,false)) return false; }
        else if(!animation_action_stop(from)) return false;
    }
    c->playing=index;
    // a faded-out state releases its properties, so it cannot pin stale values
    for(unsigned i=0;i<c->count;i++) {
        auto* other=c->states[i].clip->action;
        if(other!=action&&other->active&&!other->enabled&&!animation_action_stop(other)) return false;
    }
    if(announce) scene3d_motion_announce(state,c,previous>=0?c->states[previous].name:"",target.name);
    return true;
}
// a controller's states must name unique clips of its own group that do not autoplay
static bool scene3d_motion_resolve(Scene3dAnimationState* state,DomElement* node) {
    DomNode* parent=node->parent;
    const char* id=scene3d_text(node,"id");
    float fade,cue;
    if(!parent||!parent->is_element()||!id||!*id||strlen(id)>=128||!scene3d_number(node,"fade",0.25f,&fade)||
        !(fade>=0)||!scene3d_number(node,"cue",0,&cue)||!isfinite(cue)) return false;
    DomElement* group=parent->as_element();
    unsigned count=0;
    for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) {
        if(strcmp(child->as_element()->local_name(),"motion-state")) return false;
        count++;
    }
    if(!count||count>SCENE_MOTION_STATE_LIMIT) return false;
    if(!state->pool) state->pool=mem_pool_create((MemContext*)node->doc->services.mem_ctx,MEM_ROLE_RENDER,"scene3d.animation");
    auto* c=state->pool?(Scene3dMotionController*)pool_calloc(state->pool,sizeof(Scene3dMotionController)):nullptr;
    if(!c||!(c->states=(Scene3dMotionState*)pool_calloc(state->pool,count*sizeof(Scene3dMotionState)))) return false;
    c->node=dom_node_ref(node);str_copy(c->id,sizeof(c->id),id,strlen(id));
    c->count=count;c->fade=fade;c->cue=cue;c->playing=-1;
    unsigned index=0;
    for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) {
        DomElement* source=child->as_element();auto& s=c->states[index++];
        const char* name=scene3d_text(source,"name"),*clip=scene3d_text(source,"clip"),*loop=scene3d_text(source,"loop");
        if(!name||!*name||strlen(name)>=sizeof(s.name)||!clip||!scene3d_number(source,"fade",-1,&s.fade)||
            !scene3d_number(source,"time-scale",1,&s.time_scale)||!isfinite(s.time_scale)) return false;
        str_copy(s.name,sizeof(s.name),name,strlen(name));
        if(!loop||!strcmp(loop,"repeat")) s.loop=ANIMATION_LOOP_REPEAT;
        else if(!strcmp(loop,"once")) s.loop=ANIMATION_LOOP_ONCE;
        else if(!strcmp(loop,"pingpong")) s.loop=ANIMATION_LOOP_PINGPONG;
        else return false;
        DomElement* clip_node=scene3d_animation_find(group,clip);
        if(!clip_node||strcmp(clip_node->local_name(),"animation-clip")) return false;
        for(auto* k=state->clips;k;k=k->next) if(!strcmp(k->name,clip)) s.clip=k;
        if(!s.clip||s.clip->autoplay||s.clip->controlled) return false;
        s.clip->controlled=true;
        for(unsigned j=0;j+1<index;j++) if(!strcmp(c->states[j].name,s.name)) return false;
    }
    index=0;
    for(DomNode* child=node->first_child;child;child=child->next_sibling) if(child->is_element()) {
        const char* next=scene3d_text(child->as_element(),"next");
        auto& s=c->states[index++];
        s.next=next?scene3d_motion_index(c,next):-1;
        if(next&&s.next<0) return false;
    }
    const char* requested=scene3d_text(node,"state");
    c->requested=requested?scene3d_motion_index(c,requested):0;
    if(c->requested<0) return false;
    c->next=state->controllers;state->controllers=c;
    return true;
}
static bool scene3d_motion_start(Scene3dAnimationState* state) {
    for(unsigned i=0;i<state->pending_count;i++) {
        DomNode* node=dom_node_ref_validate(state->document,state->pending[i]);
        if(!node||!node->is_element()||!state->mixer||!scene3d_motion_resolve(state,node->as_element())) return false;
    }
    for(auto* c=state->controllers;c;c=c->next) {
        if(!scene3d_motion_enter(state,c,c->requested,false,false)) return false;
        state->automatic=true;
    }
    return true;
}
// a changed (state, cue) request from the template always wins over the playing state
static bool scene3d_motion_reconcile(Scene3dAnimationState* state) {
    bool changed=false;
    for(auto* c=state->controllers;c;c=c->next) {
        DomNode* node=dom_node_ref_validate(state->document,c->node);
        if(!node||!node->is_element()) return false;
        const char* name=scene3d_text(node->as_element(),"state");
        int requested=name?scene3d_motion_index(c,name):0;
        float cue;
        if(requested<0||!scene3d_number(node->as_element(),"cue",0,&cue)||!isfinite(cue)) return false;
        if(requested==c->requested&&cue==c->cue) continue;
        c->requested=requested;c->cue=cue;changed=true;
        if(!scene3d_motion_enter(state,c,requested,true,true)) return false;
    }
    return !changed||scene3d_animation_automatic(state,state->automatic);
}

static void scene3d_animation_released(AnimationInstance* instance) {
    ((Scene3dAnimationState*)instance->target)->driver=nullptr;
}
static void scene3d_animation_tick(AnimationInstance* instance,float) {
    auto* state=(Scene3dAnimationState*)instance->target;
    double now=animation_clock_time(state->document->state->animation_scheduler,instance,0);
    if(!scene3d_animation_connected(state,state->root)||!animation_mixer_update(state->mixer,now-state->previous_host_time)||
        !animation_mixer_active(state->mixer)) instance->play_state=ANIM_PLAY_FINISHED;
    state->previous_host_time=now;
}
bool scene3d_animation_automatic(Scene3dAnimationState* state,bool automatic) {
    if(!state) return false;
    state->automatic=automatic;
    if(!automatic&&state->driver) animation_scheduler_cancel(state->document->state->animation_scheduler,state->driver);
    if(automatic&&!state->driver&&animation_mixer_active(state->mixer)) {
        if(!state->document->state&&!state_store_create(state->document)) return false;
        state->previous_host_time=0;
        state->driver=animation_clock_driver_start(state->document->state->animation_scheduler,ANIM_TIMELINE,state,
            scene3d_animation_tick,scene3d_animation_released);
        return state->driver!=nullptr;
    }
    return true;
}
Scene3dAnimationState* scene3d_animation_create(DomElement* root,char* diagnostic,size_t capacity) {
    auto* state=(Scene3dAnimationState*)mem_calloc(1,sizeof(Scene3dAnimationState),MEM_CAT_RENDER);if(!state) return nullptr;
    state->document=root->doc;state->root=dom_node_ref(root);state->epoch=root->doc->mutation_epoch;
    state->source_hash=scene3d_animation_source_hash(root);
    bool parsed=scene3d_animation_parse(state,root,root),controlled=parsed&&scene3d_motion_start(state);
    if(!controlled||(state->mixer&&!animation_mixer_update(state->mixer,0))||
        !scene3d_animation_automatic(state,state->automatic)) {
        const char* error=parsed&&!controlled?"invalid motion controller":"invalid scene animation clip, track or binding";
        str_copy(diagnostic,capacity,error,strlen(error));
        scene3d_animation_destroy(state);return nullptr;
    }
    return state;
}
void scene3d_animation_destroy(Scene3dAnimationState* state) {
    if(!state) return;
    scene3d_animation_automatic(state,false);animation_mixer_destroy(state->mixer);mem_pool_destroy(state->pool);mem_free(state);
}
bool scene3d_animation_matches(Scene3dAnimationState* state,DomElement* root) {
    if(!state||state->document!=root->doc) return false;
    if(state->epoch==root->doc->mutation_epoch) return true;
    // sibling UI mutations may reproject the scene, but cannot restart an unchanged clip timeline.
    for(auto* binding=state->bindings;binding;binding=binding->next) if(!scene3d_animation_connected(state,binding->node)) return false;
    if(state->source_hash!=scene3d_animation_source_hash(root)) {
        log_debug("scene3d animation: clip or controller source changed; rebuilding playback");return false;
    }
    if(!scene3d_motion_reconcile(state)) {
        log_debug("scene3d animation: motion controller request could not be applied; rebuilding playback");return false;
    }
    state->epoch=root->doc->mutation_epoch;return true;
}
uint64_t scene3d_animation_generation(Scene3dAnimationState* state) {return state?state->generation:0;}
void scene3d_animation_follow_rebuild(Scene3dAnimationState* state,DomNode* old_root,DomNode* new_root) {
    if(!state) return;
    // a rebuild leaves the document's mutation epoch alone; force the next match to re-hash and reconcile
    state->epoch=UINT64_MAX;
    view_state_ref_follow_rebuild(state->document,&state->root,old_root,new_root);
    for(auto* binding=state->bindings;binding;binding=binding->next)
        view_state_ref_follow_rebuild(state->document,&binding->node,old_root,new_root);
    for(auto* c=state->controllers;c;c=c->next) view_state_ref_follow_rebuild(state->document,&c->node,old_root,new_root);
}
AnimationActionState* scene3d_animation_action(Scene3dAnimationState* state,const char* name) {
    if(state&&name) for(auto* clip=state->clips;clip;clip=clip->next) if(!strcmp(clip->name,name)) return clip->action;
    return nullptr;
}
bool scene3d_animation_update(Scene3dAnimationState* state,double delta) {
    return state&&!state->automatic&&animation_mixer_update(state->mixer,delta);
}
bool scene3d_animation_seek(Scene3dAnimationState* state,double seconds) {
    if(!state||!scene3d_animation_automatic(state,false)) return false;
    if(!state->controllers) return animation_mixer_set_time(state->mixer,seconds);
    // controllers replay their requested state from 0 and step exactly to each
    // `once` finish, so the `next` chain starts on time and a seek is reproducible
    if(!isfinite(seconds)||seconds<0) return false;
    for(auto* c=state->controllers;c;c=c->next) {
        for(unsigned i=0;i<c->count;i++) {
            auto* action=c->states[i].clip->action;
            if(action->active&&!animation_action_stop(action)) return false;
        }
        c->playing=-1;
        if(!scene3d_motion_enter(state,c,c->requested,false,false)) return false;
    }
    if(!animation_mixer_set_time(state->mixer,0)) return false;
    double remaining=seconds;
    for(unsigned guard=0;remaining>0&&guard<4096;guard++) {
        double step=remaining;
        for(auto* c=state->controllers;c;c=c->next) if(c->playing>=0) {
            const auto& s=c->states[c->playing];auto* action=s.clip->action;
            if(s.loop!=ANIMATION_LOOP_ONCE||s.next<0||!action->enabled||action->paused||!(action->time_scale>0)) continue;
            double left=(action->clip.duration-action->time)/action->time_scale;
            if(left>0&&left<step) step=left;
        }
        if(!animation_mixer_update(state->mixer,step)) return false;
        remaining-=step;
    }
    return true;
}
