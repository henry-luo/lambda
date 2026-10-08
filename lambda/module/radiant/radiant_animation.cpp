#include "../../../radiant/animation_mixer.hpp"
#include "../../../radiant/scene3d.hpp"
#include "../../../radiant/scene3d_animation.hpp"
#include "../../../radiant/event.hpp"
#include "../../dom/dom.h"
#include "../../dom/realm/dom_realm.h"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/dom_lifecycle.hpp"
#include "../../js/js_runtime.h"
#include "../../js/js_function.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/root_vector.h"
#include "../../jube/jube_registry.h"
#include "../../../lib/mem.h"
#include "../../../lib/str.h"
#include <math.h>
#include <string.h>

// D7.4.1v2/D7.4.4: wrappers carry identities; document resources own playback and copied tracks.
struct JsAnimationBinding {
    JsAnimationBinding* next;
    uint64_t id;
    int64_t read_slot, write_slot;
    AnimationValueType type;
    unsigned count, references;
};
struct JsAnimationClip {
    JsAnimationClip* next;
    AnimationActionState* action;
    AnimationChannelView* channels;
    unsigned count;
    size_t bytes;
};
struct JsAnimationHost : DomDocumentResourceData {
    JsAnimationHost* next;
    DomDocument* document;
    DomNodeRef target;
    AnimationMixerState* mixer;
    AnimationInstance* driver;
    RootVector roots;
    JsAnimationBinding* bindings;
    JsAnimationClip* clips;
    uint64_t id, next_binding;
    unsigned binding_count;
    size_t track_bytes;
    double previous_time;
    bool automatic, busy, closing;
};
struct JsAnimationWrapper { uint64_t id; };
static JsAnimationHost* animation_hosts;
static uint64_t animation_host_id=1;
extern __thread EvalContext* context;
static void animation_wrapper_destroy(void* data) { mem_free(data); }
static const JubeTypeDef animation_host_type={"animation_host",JUBE_TYPE_OWNING_NATIVE,nullptr,animation_wrapper_destroy};
static JsAnimationHost* animation_host(Item receiver) {
    if(get_type_id(receiver)!=LMD_TYPE_VMAP||!receiver.vmap||receiver.vmap->host_type!=&animation_host_type) return nullptr;
    auto* wrapper=(JsAnimationWrapper*)receiver.vmap->host_data;if(!wrapper) return nullptr;
    for(auto* host=animation_hosts;host;host=host->next) if(host->id==wrapper->id) return host;
    return nullptr;
}
static Item animation_root(JsAnimationHost* host,int64_t slot) {
    Item* item=root_vector_at(&host->roots,slot);return item?*item:ItemNull;
}
static void animation_root_set(JsAnimationHost* host,int64_t slot,Item value) {
    Item* item=root_vector_at(&host->roots,slot);if(item) *item=value;
}
static int64_t animation_root_add(JsAnimationHost* host,Item value) {
    for(int64_t i=2;i<root_vector_count(&host->roots);i++) if(animation_root(host,i).item==ItemNull.item) {
        animation_root_set(host,i,value);return i;
    }
    int64_t slot=root_vector_count(&host->roots);return root_vector_push(&host->roots,value)?slot:-1;
}
static bool animation_host_connected(JsAnimationHost* host) {
    DomNode* node=dom_node_ref_validate(host->document,host->target);
    for(unsigned depth=0;node&&depth<256;depth++,node=node->parent) if(node==host->document->root) return true;
    return false;
}
static bool animation_number(JsAnimationHost* host,Item value,double* number,bool finite=true) {
    Item result=js_to_number(value);if(item_is_error(result)) {animation_root_set(host,0,result);return false;}
    *number=get_type_id(result)==LMD_TYPE_INT?it2i(result):get_type_id(result)==LMD_TYPE_INT64?it2l(result):it2d(result);
    return !finite||isfinite(*number);
}
static Item animation_index(Item array,unsigned index) { return dom_realm_get(array,{.item=i2it(index)}); }
static JsAnimationBinding* animation_binding(JsAnimationHost* host,uint64_t id) {
    for(auto* binding=host->bindings;binding;binding=binding->next) if(binding->id==id) return binding;
    return nullptr;
}
static bool animation_callback_error(JsAnimationHost* host,Item result) {
    if(!item_is_error(result)) return false;
    animation_root_set(host,0,result);return true;
}
static bool animation_binding_read(void* owner,uint64_t id,AnimationValue* value) {
    auto* host=(JsAnimationHost*)owner;auto* binding=animation_binding(host,id);
    if(!binding||host->closing||!animation_host_connected(host)) return false;
    RootFrame roots(4);Rooted<Item> callback(roots,animation_root(host,binding->read_slot));
    Rooted<Item> array(roots,dom_realm_call(callback.get(),ItemNull,nullptr,0));Rooted<Item> item(roots,ItemNull);
    if(animation_callback_error(host,array.get())||host->closing) return false;
    *value={};value->type=binding->type;value->count=binding->count;
    for(unsigned i=0;i<value->count;i++) {
        item.set(animation_index(array.get(),i));if(animation_callback_error(host,item.get())) return false;
        if(value->type==ANIMATION_STRING) {
            item.set(js_to_string(item.get()));if(animation_callback_error(host,item.get())) return false;
            String* text=it2s(item.get());if(text->len>=sizeof(value->text)||memchr(text->chars,0,text->len)) return false;
            str_copy(value->text,sizeof(value->text),text->chars,text->len);
        } else if(value->type==ANIMATION_BOOLEAN) value->numbers[i]=js_is_truthy(item.get());
        else if(!animation_number(host,item.get(),&value->numbers[i])) return false;
    }
    return !host->closing;
}
static bool animation_binding_write(void* owner,uint64_t id,const AnimationValue* value) {
    auto* host=(JsAnimationHost*)owner;
    if(host->closing) return true; // teardown never enters author code or resurrects a dead target
    auto* binding=animation_binding(host,id);if(!binding||!animation_host_connected(host)) return false;
    RootFrame roots(4);Rooted<Item> callback(roots,animation_root(host,binding->write_slot));
    Rooted<Item> array(roots,js_array_new(0));Rooted<Item> item(roots,ItemNull),result(roots,ItemNull);
    for(unsigned i=0;i<value->count;i++) {
        item.set(value->type==ANIMATION_STRING?make_string_item(value->text):value->type==ANIMATION_BOOLEAN?
            Item{.item=b2it(value->numbers[i]!=0)}:js_make_number(value->numbers[i]));
        js_array_push(array.get(),item.get());
    }
    Item arg=array.get();result.set(dom_realm_call(callback.get(),ItemNull,&arg,1));
    if(animation_callback_error(host,result.get())) return false;
    if(host->document->state) doc_state_request_repaint(host->document->state);
    return !host->closing;
}
static void animation_binding_event(void* owner,uint64_t id,const char* type,double detail) {
    auto* host=(JsAnimationHost*)owner;if(host->closing) return;
    RootFrame roots(4);Rooted<Item> callback(roots,animation_root(host,1));if(!dom_realm_is_callable(callback.get())) return;
    Rooted<Item> event(roots,js_new_object()),value(roots,make_string_item(type)),result(roots,ItemNull);
    dom_realm_set_name(event.get(),"type",value.get());dom_realm_set_name(event.get(),"action",{.item=i2it(id)});
    value.set(js_make_number(detail));dom_realm_set_name(event.get(),!strcmp(type,"loop")?"loopDelta":"direction",value.get());
    Item arg=event.get();result.set(dom_realm_call(callback.get(),ItemNull,&arg,1));animation_callback_error(host,result.get());
}
static void animation_clip_free(JsAnimationClip* clip) {
    for(unsigned i=0;i<clip->count;i++) {
        const auto& track=clip->channels[i].track;
        mem_free((void*)track.times);mem_free((void*)track.values);
        if(track.strings) {for(unsigned k=0;k<track.keys;k++) mem_free((void*)track.strings[k]);mem_free((void*)track.strings);}
        mem_free((void*)track.in_tangents);mem_free((void*)track.out_tangents);
    }
    mem_free(clip->channels);mem_free(clip);
}
static void animation_host_released(AnimationInstance* instance) { ((JsAnimationHost*)instance->target)->driver=nullptr; }
static void animation_host_destroy(DomDocumentResourceData* data) {
    auto* host=(JsAnimationHost*)data;host->closing=true;
    if(host->driver&&host->document->state) animation_scheduler_cancel(host->document->state->animation_scheduler,host->driver);
    auto** link=&animation_hosts;while(*link&&*link!=host) link=&(*link)->next;if(*link) *link=host->next;
    animation_mixer_destroy(host->mixer);
    while(host->clips) {auto* clip=host->clips;host->clips=clip->next;animation_clip_free(clip);}
    while(host->bindings) {auto* binding=host->bindings;host->bindings=binding->next;mem_free(binding);}
    root_vector_destroy(&host->roots);mem_free(host);
}
static void animation_host_tick(AnimationInstance* instance,float) {
    auto* host=(JsAnimationHost*)instance->target;host->busy=true;
    double now=animation_clock_time(host->document->state->animation_scheduler,instance,0);
    if(!animation_host_connected(host)||!animation_mixer_update(host->mixer,now-host->previous_time)||!animation_mixer_active(host->mixer))
        instance->play_state=ANIM_PLAY_FINISHED;
    host->previous_time=now;host->busy=false;
    if(host->closing) dom_document_release_resource(host->document,host);
}
static bool animation_host_automatic(JsAnimationHost* host,bool automatic) {
    host->automatic=automatic;
    if(!automatic&&host->driver) animation_scheduler_cancel(host->document->state->animation_scheduler,host->driver);
    if(automatic&&!host->driver&&animation_mixer_active(host->mixer)) {
        if(!host->document->state&&!state_store_create(host->document)) return false;
        host->previous_time=0;
        host->driver=animation_clock_driver_start(host->document->state->animation_scheduler,ANIM_TIMELINE,host,
            animation_host_tick,animation_host_released);
        return host->driver!=nullptr;
    }
    return true;
}
static double* animation_copy_numbers(JsAnimationHost* host,Item array,unsigned count,bool float32,size_t* bytes) {
    if(!count) return nullptr;
    RootFrame roots(2);Rooted<Item> source(roots,array),item(roots,ItemNull);
    item.set(dom_realm_get_name(source.get(),"length"));double length;
    if(animation_callback_error(host,item.get())||!animation_number(host,item.get(),&length)||length!=count) return nullptr;
    auto* values=(double*)mem_alloc(count*sizeof(double),MEM_CAT_RENDER);if(!values) return nullptr;
    for(unsigned i=0;i<count;i++) {
        item.set(animation_index(source.get(),i));
        if(animation_callback_error(host,item.get())||!animation_number(host,item.get(),&values[i])) {mem_free(values);return nullptr;}
        if(float32) values[i]=(float)values[i];
    }
    *bytes+=count*sizeof(double);return values;
}
static AnimationActionState* animation_host_clip(JsAnimationHost* host,Item descriptor) {
    RootFrame roots(6);Rooted<Item> source(roots,descriptor),tracks(roots,dom_realm_get_name(source.get(),"tracks"));
    Rooted<Item> track(roots,ItemNull),item(roots,ItemNull),values(roots,ItemNull);
    double identity,duration,count;
    item.set(dom_realm_get_name(source.get(),"id"));if(animation_callback_error(host,item.get())||!animation_number(host,item.get(),&identity)||identity<1||identity>9007199254740991.0||floor(identity)!=identity) return nullptr;
    for(auto* clip=host->clips;clip;clip=clip->next) if(clip->action->clip.identity==(uint64_t)identity) return clip->action;
    item.set(dom_realm_get_name(source.get(),"duration"));if(animation_callback_error(host,item.get())||!animation_number(host,item.get(),&duration)||duration<0) return nullptr;
    item.set(dom_realm_get_name(tracks.get(),"length"));if(animation_callback_error(host,item.get())||!animation_number(host,item.get(),&count)||count<1||count>256||floor(count)!=count) return nullptr;
    auto* clip=(JsAnimationClip*)mem_calloc(1,sizeof(JsAnimationClip),MEM_CAT_RENDER);if(!clip) return nullptr;
    clip->count=count;clip->channels=(AnimationChannelView*)mem_calloc(clip->count,sizeof(AnimationChannelView),MEM_CAT_RENDER);
    if(!clip->channels) {mem_free(clip);return nullptr;}
    bool valid=true;
    for(unsigned i=0;i<clip->count&&valid;i++) {
        track.set(animation_index(tracks.get(),i));auto& channel=clip->channels[i];auto& view=channel.track;
        double property,keys,interpolation;
        item.set(dom_realm_get_name(track.get(),"property"));valid=!animation_callback_error(host,item.get())&&animation_number(host,item.get(),&property)&&property>=1&&property<=9007199254740991.0&&floor(property)==property;
        auto* binding=valid?animation_binding(host,property):nullptr;if(!binding) {valid=false;break;}
        channel.property=property;view.type=binding->type;view.components=binding->count;
        item.set(dom_realm_get_name(track.get(),"interpolation"));valid=!animation_callback_error(host,item.get())&&animation_number(host,item.get(),&interpolation)&&interpolation>=0&&interpolation<=3&&floor(interpolation)==interpolation;
        if(!valid) break;view.interpolation=(AnimationInterpolation)(unsigned)interpolation;
        values.set(dom_realm_get_name(track.get(),"times"));item.set(dom_realm_get_name(values.get(),"length"));
        valid=!animation_callback_error(host,item.get())&&animation_number(host,item.get(),&keys)&&keys>=1&&keys<=32768&&floor(keys)==keys;
        if(!valid) break;view.keys=keys;
        view.times=animation_copy_numbers(host,values.get(),view.keys,true,&clip->bytes);if(!view.times) {valid=false;break;}
        values.set(dom_realm_get_name(track.get(),"values"));
        if(view.type==ANIMATION_STRING) {
            item.set(dom_realm_get_name(values.get(),"length"));double length;
            if(animation_callback_error(host,item.get())||!animation_number(host,item.get(),&length)||length!=view.keys) {valid=false;break;}
            auto** strings=(const char**)mem_calloc(view.keys,sizeof(char*),MEM_CAT_RENDER);view.strings=strings;
            if(!strings) {valid=false;break;}
            for(unsigned k=0;k<view.keys&&valid;k++) {
                item.set(animation_index(values.get(),k));item.set(js_to_string(item.get()));
                if(animation_callback_error(host,item.get())) {valid=false;break;}
                String* text=it2s(item.get());if(text->len>255||memchr(text->chars,0,text->len)) {valid=false;break;}
                auto* copy=(char*)mem_alloc(text->len+1,MEM_CAT_RENDER);if(!copy) {valid=false;break;}
                str_copy(copy,text->len+1,text->chars,text->len);strings[k]=copy;clip->bytes+=text->len+1+sizeof(char*);
            }
        } else view.values=animation_copy_numbers(host,values.get(),view.keys*view.components,true,&clip->bytes);
        if(view.type!=ANIMATION_STRING&&!view.values) valid=false;
        if(view.interpolation==ANIMATION_BEZIER) {
            values.set(dom_realm_get_name(track.get(),"inTangents"));
            if(values.get().item!=ITEM_JS_UNDEFINED) {
                view.in_tangents=animation_copy_numbers(host,values.get(),view.keys*view.components*2,true,&clip->bytes);
                if(!view.in_tangents) valid=false;
            }
            values.set(dom_realm_get_name(track.get(),"outTangents"));
            if(values.get().item!=ITEM_JS_UNDEFINED) {
                view.out_tangents=animation_copy_numbers(host,values.get(),view.keys*view.components*2,true,&clip->bytes);
                if(!view.out_tangents) valid=false;
            }
        }
        valid=valid&&animation_track_validate(view)&&clip->bytes+host->track_bytes<=16u*1024u*1024u;
    }
    item.set(dom_realm_get_name(source.get(),"additive"));bool additive=js_is_truthy(item.get());
    if(valid&&!host->closing) clip->action=animation_mixer_action(host->mixer,{(uint64_t)identity,duration,clip->channels,clip->count,additive});
    if(!clip->action) {animation_clip_free(clip);return nullptr;}
    for(unsigned i=0;i<clip->count;i++) animation_binding(host,clip->channels[i].property)->references++;
    clip->next=host->clips;host->clips=clip;host->track_bytes+=clip->bytes;return clip->action;
}
static AnimationActionState* animation_host_action(JsAnimationHost* host,double id) {
    if(!isfinite(id)||floor(id)!=id||id<1||id>9007199254740991.0) return nullptr;
    for(auto* clip=host->clips;clip;clip=clip->next) if(clip->action->identity==(uint64_t)id) return clip->action;
    return nullptr;
}
static bool animation_host_action_control(JsAnimationHost* host,AnimationActionState* action,unsigned op,double a,double b,double c,double* result) {
    if(!action) return false;
    if(op==111) {*result=action->active&&action->enabled&&!action->paused&&action->time_scale!=0&&!action->scheduled;return true;}
    if(op>=100&&op<=110) {
        const double fields[]={action->time,action->time_scale,action->weight,(double)action->enabled,(double)action->paused,
            (double)action->clamp,(double)action->zero_slope_start,(double)action->zero_slope_end,action->effective_weight,action->effective_time_scale,(double)action->active};
        *result=fields[op-100];return true;
    }
    if(op>=20&&op<=27) {
        if(!isfinite(a)) return false;
        switch(op) {
            case 20:action->time=a;break;case 21:action->time_scale=a;break;
            case 22:if(a<0) return false;action->weight=a;break;
            case 23:action->enabled=a!=0;break;case 24:action->paused=a!=0;break;
            case 25:action->clamp=a!=0;break;case 26:action->zero_slope_start=a!=0;break;case 27:action->zero_slope_end=a!=0;break;
        }
        return true;
    }
    switch(op) {
        case 0:return animation_action_play(action);case 1:return animation_action_stop(action);
        case 2:animation_action_reset(action);return true;
        case 3:case 4:return animation_action_fade(action,a,op==3);
        case 5:return animation_action_warp(action,a,b,c);
        case 6:return animation_action_crossfade(animation_host_action(host,a),action,b,c!=0);
        case 7:if((a!=2200&&a!=2201&&a!=2202)||isnan(b)||b<0||(!isinf(b)&&floor(b)!=b)) return false;
            action->loop=(AnimationLoopMode)(unsigned)(a-2200);action->repetitions=b;return true;
        case 8:if(!isfinite(a)) return false;action->scheduled=true;action->scheduled_start=a;return true;
        case 9:if(!isfinite(a)||a<0) return false;action->weight=a;action->effective_weight=action->enabled?a:0;action->fade.active=false;return true;
        case 10:if(!isfinite(a)) return false;action->time_scale=a;action->effective_time_scale=action->paused?0:a;action->warp.active=false;return true;
        case 11:action->fade.active=false;return true;case 12:action->warp.active=false;return true;
    }
    return false;
}
static Item animation_host_invoke(Item receiver,Item* args,int argc) {
    if(argc>6) return dom_realm_throw_type_error("Animation operation accepts at most six arguments");
    RootFrame roots(16);Rooted<Item> self(roots,receiver),value(roots,ItemNull);
    for(int i=0;i<argc;i++) {Rooted<Item> arg(roots,args[i]);}
    JsAnimationHost* host=animation_host(self.get());
    if(!host||host->closing) return dom_realm_throw_type_error("Animation host has been disposed");
    // operation identities are primitives: coercion must not dispose a host before its busy guard.
    double operation;if(!argc||!(get_type_id(args[0])==LMD_TYPE_INT||get_type_id(args[0])==LMD_TYPE_INT64||get_type_id(args[0])==LMD_TYPE_FLOAT)||!animation_number(host,args[0],&operation)||floor(operation)!=operation||operation<0||operation>12)
        return dom_realm_throw_type_error("Invalid animation operation");
    unsigned op=operation;
    if(op==11) {
        if(host->busy) {host->closing=true;return make_js_undefined();}
        // restore base properties while author callbacks are still permitted, then release all roots.
        host->busy=true;
        for(auto* clip=host->clips;clip&&!host->closing;clip=clip->next) animation_action_stop(clip->action);
        host->busy=false;dom_document_release_resource(host->document,host);return make_js_undefined();
    }
    if(host->busy||!animation_host_connected(host)) return dom_realm_throw_type_error("Animation host is busy or its canvas is detached");
    host->busy=true;animation_root_set(host,0,ItemNull);bool valid=true;double number=0;
    Item argument=argc>1?args[1]:make_js_undefined();
    if(op==0) {
        Rooted<Item> descriptor(roots,argument),read(roots,dom_realm_get_name(argument,"read")),write(roots,dom_realm_get_name(argument,"write"));
        double type,count;value.set(dom_realm_get_name(descriptor.get(),"type"));valid=!item_is_error(value.get())&&animation_number(host,value.get(),&type)&&type>=0&&type<=5&&floor(type)==type;
        value.set(dom_realm_get_name(descriptor.get(),"count"));valid=valid&&!item_is_error(value.get())&&animation_number(host,value.get(),&count)&&count>=1&&count<=16&&floor(count)==count;
        valid=valid&&(type!=ANIMATION_QUATERNION||count==4)&&
            ((type!=ANIMATION_BOOLEAN&&type!=ANIMATION_STRING)||count==1)&&dom_realm_is_callable(read.get())&&dom_realm_is_callable(write.get())&&host->binding_count<1024;
        auto* binding=valid?(JsAnimationBinding*)mem_calloc(1,sizeof(JsAnimationBinding),MEM_CAT_RENDER):nullptr;
        if(binding) {
            binding->type=(AnimationValueType)(unsigned)type;binding->count=count;binding->id=host->next_binding++;
            binding->read_slot=animation_root_add(host,read.get());binding->write_slot=animation_root_add(host,write.get());
            valid=binding->read_slot>=0&&binding->write_slot>=0;
            if(valid) {binding->next=host->bindings;host->bindings=binding;host->binding_count++;number=binding->id;}
            else {animation_root_set(host,binding->read_slot,ItemNull);animation_root_set(host,binding->write_slot,ItemNull);mem_free(binding);}
        } else valid=false;
    } else if(op==12) {
        auto* node=dom_node_ref_validate(host->document,host->target);
        auto* scene=node&&node->is_element()?scene3d_animations(node->as_element()):nullptr;
        double command;valid=scene&&animation_number(host,argument,&command);
        if(valid&&command==0) valid=scene3d_animation_automatic(scene,false);
        else if(valid&&command==1) {
            double seconds;valid=argc>2&&animation_number(host,args[2],&seconds)&&scene3d_animation_seek(scene,seconds);
        } else if(valid&&command==2) {
            value.set(js_to_string(argc>2?args[2]:make_js_undefined()));
            auto* action=!item_is_error(value.get())?scene3d_animation_action(scene,it2s(value.get())->chars):nullptr;
            valid=action&&animation_action_play(action)&&scene3d_animation_automatic(scene,true);
        } else valid=false;
    } else if(op==2) {
        auto* action=animation_host_clip(host,argument);valid=action!=nullptr;if(action) number=action->identity;
    } else if(op==9) {
        valid=dom_realm_is_callable(argument)||argument.item==ItemNull.item;
        if(valid) animation_root_set(host,1,argument);
    } else if(op==10) valid=animation_host_automatic(host,js_is_truthy(argument));
    else if(op==7) number=animation_mixer_time(host->mixer);
    else if(!animation_number(host,argument,&number)) valid=false;
    else if((op==1||op==3||op==8)&&(number<1||number>9007199254740991.0||floor(number)!=number)) valid=false;
    else switch(op) {
        case 1: {
            auto** link=&host->bindings;while(*link&&(*link)->id!=(uint64_t)number) link=&(*link)->next;
            valid=*link&&!(*link)->references;
            if(valid) {auto* binding=*link;*link=binding->next;animation_root_set(host,binding->read_slot,ItemNull);animation_root_set(host,binding->write_slot,ItemNull);mem_free(binding);host->binding_count--;}
            break;
        }
        case 3: {
            auto** link=&host->clips;while(*link&&(*link)->action->identity!=(uint64_t)number) link=&(*link)->next;
            valid=*link&&animation_mixer_uncache(host->mixer,(*link)->action);
            if(valid) {auto* clip=*link;*link=clip->next;
                for(unsigned i=0;i<clip->count;i++) animation_binding(host,clip->channels[i].property)->references--;
                host->track_bytes-=clip->bytes;animation_clip_free(clip);}
            break;
        }
        case 4:valid=!host->automatic&&animation_mixer_update(host->mixer,number);break;
        case 5:animation_host_automatic(host,false);valid=animation_mixer_set_time(host->mixer,number);break;
        case 6:valid=animation_mixer_time_scale(host->mixer,number);break;
        case 8: {
            double command=0,a=0,b=0,c=0;
            valid=argc>2&&animation_number(host,args[2],&command)&&command>=0&&command<=111&&floor(command)==command;
            if(argc>3) valid=valid&&animation_number(host,args[3],&a,false);
            if(argc>4) valid=valid&&animation_number(host,args[4],&b,false);
            if(argc>5) valid=valid&&animation_number(host,args[5],&c,false);
            if(valid) valid=animation_host_action_control(host,animation_host_action(host,number),command,a,b,c,&number);
            break;
        }
        default:valid=false;
    }
    if(valid&&host->automatic) valid=animation_host_automatic(host,true);
    value.set(animation_root(host,0));host->busy=false;bool closing=host->closing;
    if(closing) dom_document_release_resource(host->document,host);
    if(item_is_error(value.get())) return value.get();
    if(!valid||closing) return dom_realm_throw_type_error("Invalid animation track, binding or playback operation");
    return js_make_number(number);
}
static int animation_host_operate(Item receiver,Item* args,int argc,Item* out) { *out=animation_host_invoke(receiver,args,argc);return 1; }
static Item animation_host_construct(Item,Item* args,int argc,Item,uint64_t*) {
    RootFrame roots(2);Rooted<Item> target(roots,argc?args[0]:ItemNull),wrapper(roots,ItemNull);
    auto* element=(DomElement*)dom_unwrap_element(target.get());if(!element||!element->doc||(element->tag()!=MARKUP_NAME_CANVAS&&strcmp(element->tag_name,"scene3d"))) return dom_realm_throw_type_error("Animation host requires its document canvas or scene element");
    unsigned count=0;for(auto* host=animation_hosts;host;host=host->next) count++;
    if(count>=64) return dom_realm_throw_type_error("Animation host limit exceeded");
    auto* host=(JsAnimationHost*)mem_calloc(1,sizeof(JsAnimationHost),MEM_CAT_RENDER);if(!host) return ItemNull;
    host->document=element->doc;host->target=dom_node_ref(element);host->id=animation_host_id++;host->next_binding=1;
    root_vector_init(&host->roots,(Context*)context,"native animation bindings");
    if(!root_vector_push(&host->roots,ItemNull)||!root_vector_push(&host->roots,ItemNull)) {
        root_vector_destroy(&host->roots);mem_free(host);return ItemNull;
    }
    host->mixer=animation_mixer_create(host,{animation_binding_read,animation_binding_write,animation_binding_event});
    if(!host->mixer||!dom_document_add_resource(host->document,host,animation_host_destroy)) {animation_host_destroy(host);return ItemNull;}
    host->next=animation_hosts;animation_hosts=host;
    wrapper.set(vmap_new());auto* data=(JsAnimationWrapper*)mem_alloc(sizeof(JsAnimationWrapper),MEM_CAT_JS_RUNTIME);
    if(!data||get_type_id(wrapper.get())!=LMD_TYPE_VMAP) {mem_free(data);dom_document_release_resource(host->document,host);return ItemNull;}
    data->id=host->id;wrapper.get().vmap->host_type=&animation_host_type;wrapper.get().vmap->host_data=data;vmap_set_owner(wrapper.get().vmap,target.get());
    return wrapper.get();
}
static Item animation_host_call(Item,Item,Item*,int,uint64_t*) { return dom_realm_throw_type_error("Use new RadiantAnimationHost(canvas)"); }
static const JubeMemberBind animation_host_members[]={
    {"operate",nullptr,nullptr,nullptr,animation_host_operate,nullptr,JUBE_MEMBER_PROTOTYPE|JUBE_MEMBER_REQUIRED_ARGS(1)}
};
static const JubeTypeBinding animation_host_binding={"animation_host",&animation_host_type,animation_host_members,1};
static const JubeModuleDef animation_host_module={JUBE_ABI_VERSION,sizeof(JubeModuleDef),"animation","0.1.0","Shared SVG and 3D animation bridge",
    &animation_host_type,1,nullptr,0,nullptr,0,nullptr,nullptr,
    "type animation_host { operate: fn(op: any, a: any, b: any, c: any, d: any, e: any) any }\n",
    &animation_host_binding,1};
extern "C" void radiant_animation_register_static(void) { jube_register_static_module(&animation_host_module); }
extern "C" void dom_engine_install_animation_globals(void) {
    if(!jube_find_type_by_host_type(&animation_host_type)) return;
    RootFrame roots(3);Rooted<Item> global(roots,dom_realm_global());
    Rooted<Item> constructor(roots,js_new_native_body_constructor(animation_host_call,animation_host_construct,1));
    Rooted<Item> prototype(roots,jube_type_prototype(&animation_host_type));
    dom_realm_init_constructor_prototype(constructor.get(),prototype.get());dom_realm_set_name(prototype.get(),"constructor",constructor.get());
    dom_realm_set_name(global.get(),"RadiantAnimationHost",constructor.get());
}
