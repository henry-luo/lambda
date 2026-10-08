#include "dom_webgl.h"
#include "dom.h"
#include "dom_events.h"
#include "../js/js_event_loop.h"
#include "../js/js_class.h"
#include "../module/radiant/radiant_dom_bridge.hpp"
#include "../../lib/str.h"
#include "realm/dom_realm.h"
#include "../lambda.hpp"
#include "../js/js_runtime.h"
#include "../js/js_typed_array.h"
#include "../js/js_function.hpp"
#include "../js/js_runtime_state.hpp"
#include "../runtime/lambda-root-frame.hpp"
#include "../runtime/root_vector.h"
#include "../runtime/context_capsule.h"
#include "../jube/jube.h"
#include "../jube/jube_interface.h"
#include "../jube/jube_registry.h"
#include "../module/radiant/radiant_webgl_bridge.hpp"
#include "../../lib/mem.h"
#include <math.h>
#include <string.h>

// native records hold rids only; every retained script edge is precisely rooted (D4.5.2, D5.3.3).
struct WebGlObject {
    uint64_t canvas, resource, generation, program;
    int location, kind;
    int64_t owner_slot;
    WebGlOptions options;
    bool loss_delivered, restore_allowed, invalidated;
};
struct WebGlRealm { RootVector objects; };
extern __thread EvalContext* context;
static void webgl_realm_destroy(void* data) {
    WebGlRealm* state=(WebGlRealm*)data;root_vector_destroy(&state->objects);mem_free(state);
}
static const ContextCapsuleOps webgl_realm_ops={"dom-webgl",CONTEXT_CAPSULE_LIFETIME_REALM,sizeof(WebGlRealm),nullptr,nullptr,webgl_realm_destroy};
static WebGlRealm* webgl_realm() {
    WebGlRealm* state=(WebGlRealm*)context_capsule(context,CONTEXT_CAPSULE_DOM_WEBGL);
    if(!state) {
        state=(WebGlRealm*)context_capsule_ensure(context,CONTEXT_CAPSULE_DOM_WEBGL,&webgl_realm_ops);
        if(state) root_vector_init(&state->objects,(Context*)context,"WebGL identities");
    }
    return state;
}
static void webgl_destroy(void* data) { mem_free(data); }
// order matches NativeGlKind at the graphics waist; target is an internal-only kind.
static const JubeTypeDef webgl_types[]={
    {"webgl_buffer",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_vertex_array_object",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_texture",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_program",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_internal_target",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_shader",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_framebuffer",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_renderbuffer",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_uniform_location",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl2_rendering_context",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_lose_context",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy},
    {"webgl_capability_extension",JUBE_TYPE_OWNING_NATIVE,nullptr,webgl_destroy}
};
static constexpr int WEBGL_CONTEXT_KIND=9,WEBGL_UNIFORM_KIND=8;
static WebGlObject* webgl_object(Item value,int kind=-1) {
    if(get_type_id(value)!=LMD_TYPE_VMAP||!value.vmap||!value.vmap->host_data) return nullptr;
    for(unsigned i=0;i<sizeof(webgl_types)/sizeof(webgl_types[0]);i++)
        if(value.vmap->host_type==&webgl_types[i]&&(kind<0||kind==(int)i)) return (WebGlObject*)value.vmap->host_data;
    return nullptr;
}
static Item webgl_owner(WebGlObject* object) {
    WebGlRealm* state=webgl_realm();Item* value=state&&object?root_vector_at(&state->objects,object->owner_slot):nullptr;
    return value?*value:ItemNull;
}
static Item webgl_wrap(int kind,const WebGlObject& record,Item owner) {
    WebGlRealm* state=webgl_realm();if(!state) return ItemNull;
    if(root_vector_count(&state->objects)>=32768) { radiant_webgl_error(record.canvas,0x0505);return ItemNull; }
    RootFrame roots(2);Rooted<Item> owner_root(roots,owner);Rooted<Item> value(roots,vmap_new());
    if(get_type_id(value.get())!=LMD_TYPE_VMAP) return ItemNull;
    WebGlObject* object=(WebGlObject*)mem_alloc(sizeof(WebGlObject),MEM_CAT_JS_RUNTIME);if(!object) return ItemNull;
    *object=record;object->kind=kind;object->owner_slot=root_vector_count(&state->objects);
    if(!root_vector_push(&state->objects,owner_root.get())) { mem_free(object);return ItemNull; }
    value.get().vmap->host_type=&webgl_types[kind];value.get().vmap->host_data=object;
    vmap_set_owner(value.get().vmap,owner_root.get());
    if(!root_vector_push(&state->objects,value.get())) return ItemNull;
    return value.get();
}
static Item webgl_reply_object(Item context_item,WebGlObject* context_object,const WebGlReply& reply) {
    WebGlRealm* state=webgl_realm();int kind=reply.kind==WEBGL_LOCATION?WEBGL_UNIFORM_KIND:reply.resource_kind;
    for(int64_t i=0;i<root_vector_count(&state->objects);i++) {
        Item* value=root_vector_at(&state->objects,i);WebGlObject* object=value?webgl_object(*value,kind):nullptr;
        if(object&&object->canvas==context_object->canvas&&object->resource==reply.resource&&object->program==reply.program&&
            object->generation==reply.generation&&(kind!=WEBGL_UNIFORM_KIND||object->location==reply.n[0])) return *value;
    }
    WebGlObject record={};record.canvas=context_object->canvas;record.resource=reply.resource;
    record.program=reply.program;record.generation=reply.generation;record.location=reply.n[0];
    return webgl_wrap(kind,record,context_item);
}
static Item webgl_number_arg(Item value,char conversion,double* output) {
    if(conversion=='b') { *output=js_is_truthy(value);return ItemNull; }
    Item number=js_to_number(value);if(item_is_error(number)) return number;
    double n=get_type_id(number)==LMD_TYPE_INT?it2i(number):get_type_id(number)==LMD_TYPE_INT64?it2l(number):it2d(number);
    if(conversion=='i') n=js_to_int32(n);
    else if(conversion=='u') n=(uint32_t)js_to_int32(n);
    else if(conversion=='f') n=(float)n;
    else if(conversion=='z') { if(!isfinite(n)||fabs(n)>9007199254740991.0) return dom_realm_throw_type_error("WebGL offset is outside the integer range");n=trunc(n); }
    *output=n;return ItemNull;
}
static int webgl_resource_kind(char code) {
    switch(code) { case 'B':return 0;case 'V':return 1;case 'T':return 2;case 'P':return 3;
        case 'S':return 5;case 'F':return 6;case 'R':return 7;case 'L':return 8;default:return -1; }
}
static Item webgl_plain_reply(const WebGlReply& reply) {
    RootFrame roots(2);Rooted<Item> result(roots,ItemNull);Rooted<Item> value(roots,ItemNull);
    switch(reply.kind) {
        case WEBGL_BOOLEAN:return (Item){.item=b2it(reply.n[0]!=0)};
        case WEBGL_NUMBER:return flt2it(reply.n[0]);
        case WEBGL_STRING:return js_make_string(reply.source?reply.source:reply.text);
        case WEBGL_NUMBERS:
            result.set(reply.resource_kind==2?js_array_new(0):js_typed_array_new(reply.resource_kind==1?JS_TYPED_FLOAT32:reply.resource_kind==3?JS_TYPED_UINT32:JS_TYPED_INT32,reply.count));
            for(unsigned i=0;i<reply.count;i++) {
                if(reply.resource_kind==2) js_array_push(result.get(),(Item){.item=b2it(reply.n[i]!=0)});
                else js_typed_array_set(result.get(),(Item){.item=i2it(i)},flt2it(reply.n[i]));
            }return result.get();
        case WEBGL_ACTIVE_INFO: case WEBGL_PRECISION:
            result.set(js_new_object());
            if(reply.kind==WEBGL_ACTIVE_INFO) {
                value.set(js_make_string(reply.text));dom_realm_set_name(result.get(),"name",value.get());
                dom_realm_set_name(result.get(),"size",flt2it(reply.n[0]));dom_realm_set_name(result.get(),"type",flt2it(reply.n[1]));
            } else {
                dom_realm_set_name(result.get(),"rangeMin",flt2it(reply.n[0]));dom_realm_set_name(result.get(),"rangeMax",flt2it(reply.n[1]));dom_realm_set_name(result.get(),"precision",flt2it(reply.n[2]));
            }return result.get();
        default:return ItemNull;
    }
}
static Item webgl_attributes(WebGlObject* object) {
    WebGlCommand command={};command.op=WEBGL_getContextAttributes;WebGlReply reply={};
    if(!radiant_webgl_call(object->canvas,&command,&reply)) return ItemNull;
    RootFrame roots(1);Rooted<Item> result(roots,js_new_object());
    const char* names[]={"alpha","depth","stencil","antialias","premultipliedAlpha","preserveDrawingBuffer","failIfMajorPerformanceCaveat","desynchronized","xrCompatible"};
    bool values[]={reply.n[0]!=0,reply.n[1]!=0,reply.n[2]!=0,reply.n[3]!=0,reply.n[4]!=0,reply.n[5]!=0,false,false,false};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++) dom_realm_set_name(result.get(),names[i],(Item){.item=b2it(values[i])});
    dom_realm_set_name(result.get(),"powerPreference",js_name_item("default"));return result.get();
}

// loss/restoration events use the existing host task loop; no private event-loop pumping (D7.4.2v2).
static Item webgl_loss_task(Item callee,Item,Item*,int,uint64_t*) {
    uint64_t payload=js_fn_native((JsFunction*)callee.function)->target.bits;
    WebGlRealm* state=webgl_realm();Item* cached=state?root_vector_at(&state->objects,payload>>1):nullptr;
    if(!cached) return make_js_undefined();
    RootFrame roots(5);Rooted<Item> receiver(roots,*cached);WebGlObject* object=webgl_object(receiver.get(),WEBGL_CONTEXT_KIND);
    if(!object) return make_js_undefined();
    bool restore=(payload&1)!=0;
    if(restore && (!object->loss_delivered || !object->restore_allowed || !radiant_webgl_restore(object->canvas))) return make_js_undefined();
    Rooted<Item> canvas(roots,webgl_owner(object));
    Rooted<Item> init(roots,js_new_object());
    dom_realm_set_name(init.get(),"cancelable",(Item){.item=b2it(true)});
    dom_realm_set_name(init.get(),"statusMessage",js_name_item(restore?"":"WebGL context lost"));
    Rooted<Item> type(roots,js_name_item(restore?"webglcontextrestored":"webglcontextlost"));
    Rooted<Item> event(roots,js_ctor_webgl_context_event_fn(type.get(),init.get()));
    radiant_dom_event_set_trusted(event.get(),true);
    Item result=dom_dispatch_event(canvas.get(),event.get());
    if(!restore) { object->loss_delivered=true;object->restore_allowed=radiant_dom_event_default_prevented(event.get()); }
    else { object->loss_delivered=false;object->restore_allowed=false; }
    return item_is_error(result)?result:make_js_undefined();
}
static int webgl_loss_request(Item receiver,bool restore,Item* out) {
    WebGlObject* extension=webgl_object(receiver,10);if(!extension) { *out=dom_realm_throw_type_error("Illegal invocation of WEBGL_lose_context");return 1; }
    RootFrame roots(2);Rooted<Item> owner(roots,webgl_owner(extension));Rooted<Item> callback(roots,ItemNull);
    WebGlObject* object=webgl_object(owner.get(),WEBGL_CONTEXT_KIND);*out=make_js_undefined();
    if(!object) return 1;
    if(!restore && !radiant_webgl_lose(object->canvas)) return 1;
    callback.set(js_new_native_payload_function(webgl_loss_task,((object->owner_slot+1)<<1)|(restore?1:0),0));
    js_setTimeout(callback.get(),(Item){.item=i2it(0)});return 1;
}
static int webgl_lose_context(Item receiver,Item*,int,Item* out) { return webgl_loss_request(receiver,false,out); }
static int webgl_restore_context(Item receiver,Item*,int,Item* out) { return webgl_loss_request(receiver,true,out); }
static Item webgl_extension(Item receiver,WebGlObject* object) {
    WebGlReply reply={};reply.kind=WEBGL_RESOURCE;reply.resource_kind=10;
    return webgl_reply_object(receiver,object,reply);
}

static Item webgl_invoke(Item receiver,Item* args,int argc,WebGlOp op,const char* signature) {
    RootFrame roots(argc+8);Rooted<Item> receiver_root(roots,receiver);Rooted<Item> string_root(roots,ItemNull);
    Rooted<Item> value_root(roots,ItemNull);Rooted<Item> array_root(roots,ItemNull);
    for(int i=0;i<argc;i++) { Rooted<Item> argument(roots,args[i]); }
    WebGlObject* object=webgl_object(receiver_root.get(),WEBGL_CONTEXT_KIND);
    if(!object) return dom_realm_throw_type_error("Illegal invocation of WebGL2RenderingContext");
    if(op==WEBGL_getContextAttributes) return webgl_attributes(object);
    if(op==WEBGL_getSupportedExtensions || op==WEBGL_getExtension) {
        if(op==WEBGL_getExtension) {
            string_root.set(js_to_string(argc?args[0]:make_js_undefined()));if(item_is_error(string_root.get())) return string_root.get();
        }
        WebGlCommand lost={};lost.op=WEBGL_isContextLost;WebGlReply reply={};
        if(!radiant_webgl_call(object->canvas,&lost,&reply)||reply.n[0]) return ItemNull;
        const char* names[]={"WEBGL_lose_context","EXT_color_buffer_float","OES_texture_float_linear"};
        if(op==WEBGL_getExtension) {
            const char* name=it2s(string_root.get())->chars;
            for(unsigned i=0;i<3;i++) if(!str_icmp_cstr(name,names[i])) {
                if(!i) return webgl_extension(receiver_root.get(),object);
                if(!radiant_webgl_extension_supported(object->canvas,names[i])) return ItemNull;
                WebGlReply extension={};extension.kind=WEBGL_RESOURCE;extension.resource_kind=11;extension.resource=i;
                return webgl_reply_object(receiver_root.get(),object,extension);
            }
            return ItemNull;
        }
        array_root.set(js_array_new(0));
        for(unsigned i=0;i<3;i++) if(!i||radiant_webgl_extension_supported(object->canvas,names[i])) {
            value_root.set(js_name_item(names[i]));js_array_push(array_root.get(),value_root.get());
        }
        return array_root.get();
    }
    WebGlCommand command={};command.op=op;WebGlReply reply={};
    bool explicit_image=argc==9&&(op==WEBGL_texImage2D||op==WEBGL_texSubImage2D)&&dom_unwrap_element(args[8]);
    bool image_upload=explicit_image||(op==WEBGL_texImage2D&&argc==6)||(op==WEBGL_texSubImage2D&&argc==7);
    if(image_upload) {
        bool sub=op==WEBGL_texSubImage2D;
        const unsigned image_indices[]={0,1,2,sub?3u:6u,sub?6u:7u,7};
        const char* conversions=explicit_image?(sub?"uiiiiiuu":"uiuiiiuu"):(sub?"uiiiuu":"uiuuu");
        for(unsigned i=0;conversions[i];i++) {
            Item status=webgl_number_arg(args[i],conversions[i],&command.n[explicit_image?i:image_indices[i]]);
            if(item_is_error(status)) return status;
        }
        // all coercions precede unwrapping; no borrowed image or JS storage survives this host call.
        void* source=dom_unwrap_element(args[explicit_image?8:sub?6:5]);
        if(!source) return dom_realm_throw_type_error("WebGL image source must be an HTML image or canvas");
        command.source_dimensions=explicit_image;
        radiant_webgl_image(object->canvas,&command,source,&reply);
        return make_js_undefined();
    }
    for(unsigned i=0;signature[i];i++) {
        Item value=(int)i<argc?args[i]:make_js_undefined();char conversion=signature[i];
        if(conversion=='n') continue;
        if(conversion=='s') { string_root.set(js_to_string(value));if(item_is_error(string_root.get())) return string_root.get();command.text=it2s(string_root.get())->chars;continue; }
        int kind=webgl_resource_kind(conversion);
        if(kind>=0) {
            if(get_type_id(value)==LMD_TYPE_NULL||(value.item==ITEM_JS_UNDEFINED)) continue;
            WebGlObject* resource=webgl_object(value,kind);
            if(!resource) return dom_realm_throw_type_error("Expected a WebGL resource of the declared kind");
            if(resource->canvas!=object->canvas) {
                if(!strncmp(webgl_op_name(op),"is",2)) return (Item){.item=b2it(false)};
                radiant_webgl_error(object->canvas,0x0502);return make_js_undefined();
            }
            if(resource->invalidated) {
                const char* name=webgl_op_name(op);
                if(!strncmp(name,"is",2)) return (Item){.item=b2it(false)};
                if(!strncmp(name,"delete",6)) return make_js_undefined();
                radiant_webgl_error(object->canvas,0x0502);
                return op==WEBGL_getAttribLocation?(Item){.item=i2it(-1)}:!strncmp(name,"get",3)?ItemNull:make_js_undefined();
            }
            command.resource[i]=resource->resource;
            if(kind==WEBGL_UNIFORM_KIND) { command.resource[0]=resource->program;command.resource[1]=resource->generation;command.n[0]=resource->location; }
            continue;
        }
        Item status=webgl_number_arg(value,conversion,&command.n[i]);if(item_is_error(status)) return status;
    }
    const char* bytes=nullptr;int length=0;int data_index=-1;bool uniform=false,integer=false,unsigned_integer=false;
    // upload views are acquired after every coercion which may run author code or detach their buffer.
    switch(op) {
        case WEBGL_bufferData:data_index=1;break;
        case WEBGL_getBufferSubData:case WEBGL_bufferSubData:data_index=2;break;
        case WEBGL_texImage2D:case WEBGL_texSubImage2D:data_index=8;break;
        case WEBGL_texImage3D:data_index=9;break;
        case WEBGL_texSubImage3D:data_index=10;break;
        case WEBGL_readPixels:data_index=6;break;
        case WEBGL_drawBuffers:data_index=0;integer=true;unsigned_integer=true;break;
        default:
            if(signature[0]=='L'&&strchr(signature,'n')) { uniform=true;data_index=strlen(signature)-1; }
            else if(strncmp(signature,"un",2)==0) { data_index=1;uniform=true; }
            break;
    }
    if(data_index>=0) {
        Item source=data_index<argc?args[data_index]:make_js_undefined();
        bool size_upload=op==WEBGL_bufferData&&!js_is_object_value(source);
        if(size_upload) {
            Item status=webgl_number_arg(source,'z',&command.n[1]);if(item_is_error(status)) return status;
            if(command.n[1]<0) { radiant_webgl_error(object->canvas,0x0501);return make_js_undefined(); }
            command.bytes=command.n[1];
        } else if(get_type_id(source)!=LMD_TYPE_NULL&&!(source.item==ITEM_JS_UNDEFINED)) {
            if(uniform||op==WEBGL_drawBuffers) {
                const char* method_name=webgl_op_name(op);
                size_t name_length=strlen(method_name);
                integer=integer||(name_length>=2&&method_name[name_length-2]=='i');
                unsigned_integer=unsigned_integer||(name_length>=3&&method_name[name_length-3]=='u');
                if(js_is_js_array(source)) {
                    value_root.set(dom_realm_get_name(source,"length"));double count=0;
                    Item status=webgl_number_arg(value_root.get(),'u',&count);if(item_is_error(status)) return status;
                    if(count>1048576) return dom_realm_throw_type_error("WebGL array exceeds upload quota");
                    array_root.set(js_typed_array_new(unsigned_integer?JS_TYPED_UINT32:integer?JS_TYPED_INT32:JS_TYPED_FLOAT32,count));
                    for(unsigned i=0;i<(unsigned)count;i++) {
                        value_root.set(dom_realm_get(source,(Item){.item=i2it(i)}));double number=0;
                        status=webgl_number_arg(value_root.get(),integer?(unsigned_integer?'u':'i'):'f',&number);if(item_is_error(status)) return status;
                        js_typed_array_set(array_root.get(),(Item){.item=i2it(i)},flt2it(number));
                    }source=array_root.get();
                }
                int expected=unsigned_integer?JS_TYPED_UINT32:integer?JS_TYPED_INT32:JS_TYPED_FLOAT32;
                if(!js_is_typed_array(source)||js_typed_array_element_type(source)!=expected) return dom_realm_throw_type_error("WebGL numeric array has the wrong element type");
            }
            bool buffer_operation=op==WEBGL_bufferData||op==WEBGL_bufferSubData||op==WEBGL_getBufferSubData;
            if(!js_is_typed_array(source)&&!js_is_dataview(source)&&!((op==WEBGL_bufferData||op==WEBGL_bufferSubData)&&js_is_arraybuffer(source)))
                return dom_realm_throw_type_error("WebGL data must be the declared buffer or view type");
            if(!buffer_operation&&!uniform&&op!=WEBGL_drawBuffers) {
                unsigned type=command.n[op==WEBGL_readPixels?5:op==WEBGL_texImage3D?8:op==WEBGL_texSubImage3D?9:7];
                int expected=type==0x1400?JS_TYPED_INT8:type==0x1401?JS_TYPED_UINT8:type==0x1402?JS_TYPED_INT16:
                    type==0x1403||type==0x140B?JS_TYPED_UINT16:type==0x1404?JS_TYPED_INT32:type==0x1405?JS_TYPED_UINT32:type==0x1406?JS_TYPED_FLOAT32:-1;
                if(!js_is_typed_array(source)||(js_typed_array_element_type(source)!=expected&&!(type==0x1401&&js_typed_array_element_type(source)==JS_TYPED_UINT8_CLAMPED))) {
                    radiant_webgl_error(object->canvas,0x0502);return make_js_undefined();
                }
            }
            if(js_is_dataview(source)) {
                JsDataView* view=js_get_dataview_ptr(source);
                if(!view||js_arraybuffer_detached(view->buffer)||js_arraybuffer_view_is_out_of_bounds(view))
                    return dom_realm_throw_type_error("WebGL data view is detached or out of bounds");
            }
            if(js_is_typed_array(source)&&js_typed_array_is_out_of_bounds_item(source)) return dom_realm_throw_type_error("WebGL upload view is detached or out of bounds");
            if(js_is_arraybuffer(source)&&js_arraybuffer_detached(js_get_arraybuffer_ptr_item(source))) return dom_realm_throw_type_error("WebGL upload buffer is detached");
            if(!js_item_bytes(source,&bytes,&length)) return dom_realm_throw_type_error("Expected an ArrayBuffer or ArrayBufferView");
            size_t offset=0,selected=length;
            // WebGL2's optional view ranges count elements, never underlying-buffer bytes.
            unsigned offset_index=(op==WEBGL_bufferData?3:op==WEBGL_bufferSubData?3:uniform?data_index+1:op==WEBGL_readPixels?7:data_index+1);
            if(argc>(int)offset_index) {
                double start=0;Item status=webgl_number_arg(args[offset_index],'u',&start);if(item_is_error(status)) return status;
                unsigned scalar=js_is_typed_array(source)?js_typed_array_element_size((JsTypedArrayType)js_typed_array_element_type(source)):1;
                offset=(size_t)start*scalar;
                if(offset>(size_t)length) { radiant_webgl_error(object->canvas,0x0501);return make_js_undefined(); }
                selected=length-offset;
                if(argc>(int)offset_index+1) {
                    double count=0;status=webgl_number_arg(args[offset_index+1],'u',&count);if(item_is_error(status)) return status;
                    if(count!=0) selected=(size_t)count*scalar;
                    if(selected>(size_t)length-offset) { radiant_webgl_error(object->canvas,0x0501);return make_js_undefined(); }
                }
                // coercion can detach/resize: reacquire and validate the current view before borrowing.
                if(js_is_typed_array(source)&&js_typed_array_is_out_of_bounds_item(source)) return dom_realm_throw_type_error("WebGL upload view was detached during conversion");
                if(js_is_arraybuffer(source)&&js_arraybuffer_detached(js_get_arraybuffer_ptr_item(source))) return dom_realm_throw_type_error("WebGL upload buffer was detached during conversion");
                if(js_is_dataview(source)) { JsDataView* view=js_get_dataview_ptr(source);if(js_arraybuffer_detached(view->buffer)||js_arraybuffer_view_is_out_of_bounds(view)) return dom_realm_throw_type_error("WebGL upload data view was detached during conversion"); }
                if(!js_item_bytes(source,&bytes,&length)||offset>(size_t)length||selected>(size_t)length-offset) return dom_realm_throw_type_error("WebGL upload view changed during conversion");
            }
            if((op==WEBGL_readPixels||op==WEBGL_getBufferSubData)&&js_is_typed_array(source)) {
                bytes=(const char*)js_typed_array_prepare_write_ptr(source);
                if(!bytes&&length) return dom_realm_throw_type_error("WebGL destination is not writable");
            } else if((op==WEBGL_readPixels||op==WEBGL_getBufferSubData)&&js_is_dataview(source)) {
                JsDataView* view=js_get_dataview_ptr(source);
                uint8_t* writable=js_arraybuffer_prepare_write(view->buffer);
                if(!writable&&length) return dom_realm_throw_type_error("WebGL destination is not writable");
                bytes=writable?(const char*)writable+view->byte_offset:nullptr;
            }
            command.data=bytes?bytes+offset:nullptr;command.bytes=selected;
        } else if(op==WEBGL_bufferData||op==WEBGL_bufferSubData||op==WEBGL_getBufferSubData||uniform||op==WEBGL_readPixels) return dom_realm_throw_type_error("WebGL data must be a buffer view");
    }
    bool handled=radiant_webgl_call(object->canvas,&command,&reply);
    if(!handled) {
        int created_kind=-1;
        switch(op) {
            case WEBGL_createBuffer:created_kind=0;break;case WEBGL_createVertexArray:created_kind=1;break;
            case WEBGL_createTexture:created_kind=2;break;case WEBGL_createProgram:created_kind=3;break;
            case WEBGL_createShader:created_kind=5;break;case WEBGL_createFramebuffer:created_kind=6;break;
            case WEBGL_createRenderbuffer:created_kind=7;break;default:break;
        }
        if(created_kind>=0) {
            WebGlObject record={};record.canvas=object->canvas;record.invalidated=true;
            return webgl_wrap(created_kind,record,receiver_root.get());
        }
        if(op==WEBGL_isContextLost) return (Item){.item=b2it(true)};
        if(op==WEBGL_getAttribLocation) return (Item){.item=i2it(-1)};
        if(op==WEBGL_checkFramebufferStatus) return (Item){.item=i2it(0x8CDD)};
        const char* name=webgl_op_name(op);
        if(!strncmp(name,"is",2)) return (Item){.item=b2it(false)};
        return !strncmp(name,"get",3)||!strncmp(name,"create",6)?ItemNull:make_js_undefined();
    }
    if(reply.kind==WEBGL_RESOURCE||reply.kind==WEBGL_LOCATION) return webgl_reply_object(receiver_root.get(),object,reply);
    if(reply.kind==WEBGL_VOID) {
        switch(op) { case WEBGL_getUniformLocation:case WEBGL_getActiveUniform:case WEBGL_getActiveAttrib:case WEBGL_getParameter:case WEBGL_getTexParameter:case WEBGL_getBufferParameter:case WEBGL_getShaderParameter:case WEBGL_getProgramParameter:return ItemNull;default:return make_js_undefined(); }
    }
    return webgl_plain_reply(reply);
}

#define WEBGL_METHOD(name,js,signature,arity) \
static int webgl_call_##name(Item receiver,Item* args,int argc,Item* out) { *out=webgl_invoke(receiver,args,argc,WEBGL_##js,signature);return 1; }
#include "../module/radiant/webgl_methods.def"
#undef WEBGL_METHOD
static int webgl_canvas_get(Item receiver,Item* out) { *out=webgl_owner(webgl_object(receiver,WEBGL_CONTEXT_KIND));return 1; }
static int webgl_width_get(Item receiver,Item* out) { *out=dom_realm_get_name(webgl_owner(webgl_object(receiver,WEBGL_CONTEXT_KIND)),"width");return 1; }
static int webgl_height_get(Item receiver,Item* out) { *out=dom_realm_get_name(webgl_owner(webgl_object(receiver,WEBGL_CONTEXT_KIND)),"height");return 1; }
#define WEBGL_PARAMS_0 ""
#define WEBGL_PARAMS_1 "p0: any"
#define WEBGL_PARAMS_2 "p0: any, p1: any"
#define WEBGL_PARAMS_3 "p0: any, p1: any, p2: any"
#define WEBGL_PARAMS_4 "p0: any, p1: any, p2: any, p3: any"
#define WEBGL_PARAMS_5 "p0: any, p1: any, p2: any, p3: any, p4: any"
#define WEBGL_PARAMS_6 "p0: any, p1: any, p2: any, p3: any, p4: any, p5: any"
#define WEBGL_PARAMS_7 "p0: any, p1: any, p2: any, p3: any, p4: any, p5: any, p6: any"
#define WEBGL_PARAMS_8 "p0: any, p1: any, p2: any, p3: any, p4: any, p5: any, p6: any, p7: any"
#define WEBGL_PARAMS_9 "p0: any, p1: any, p2: any, p3: any, p4: any, p5: any, p6: any, p7: any, p8: any"
#define WEBGL_PARAMS_10 "p0: any, p1: any, p2: any, p3: any, p4: any, p5: any, p6: any, p7: any, p8: any, p9: any"
#define WEBGL_PARAMS_11 "p0: any, p1: any, p2: any, p3: any, p4: any, p5: any, p6: any, p7: any, p8: any, p9: any, p10: any"
static const char webgl_interface[]=
    "type webgl_buffer {}\ntype webgl_vertex_array_object {}\ntype webgl_texture {}\ntype webgl_program {}\n"
    "type webgl_internal_target {}\ntype webgl_shader {}\ntype webgl_framebuffer {}\ntype webgl_renderbuffer {}\ntype webgl_uniform_location {}\n"
    "type webgl_lose_context {lose_context: fn() any, restore_context: fn() any}\n"
    "type webgl_capability_extension {}\n"
    "type webgl2_rendering_context {\ncanvas: any, drawing_buffer_width: int, drawing_buffer_height: int,\n"
#define WEBGL_CONSTANT(name,value) #name ": int = " #value ",\n"
#include "../module/radiant/webgl_constants.def"
#undef WEBGL_CONSTANT
#define WEBGL_METHOD(name,js,signature,arity) #name ": fn(" WEBGL_PARAMS_##arity ") any,\n"
#include "../module/radiant/webgl_methods.def"
#undef WEBGL_METHOD
    "}\n";
static const JubeMemberBind webgl_members[]={
    {"canvas",nullptr,webgl_canvas_get,nullptr,nullptr,nullptr,JUBE_MEMBER_PROTOTYPE},
    {"drawing_buffer_width","drawingBufferWidth",webgl_width_get,nullptr,nullptr,nullptr,JUBE_MEMBER_PROTOTYPE},
    {"drawing_buffer_height","drawingBufferHeight",webgl_height_get,nullptr,nullptr,nullptr,JUBE_MEMBER_PROTOTYPE},
#define WEBGL_METHOD(name,js,signature,arity) {#name,#js,nullptr,nullptr,webgl_call_##name,nullptr,JUBE_MEMBER_PROTOTYPE|JUBE_MEMBER_REQUIRED_ARGS(arity)},
#include "../module/radiant/webgl_methods.def"
#undef WEBGL_METHOD
};
static const JubeMemberBind webgl_loss_members[]={
    {"lose_context","loseContext",nullptr,nullptr,webgl_lose_context,nullptr,JUBE_MEMBER_PROTOTYPE},
    {"restore_context","restoreContext",nullptr,nullptr,webgl_restore_context,nullptr,JUBE_MEMBER_PROTOTYPE}
};
static const JubeTypeBinding webgl_bindings[]={
    {"webgl_lose_context",&webgl_types[10],webgl_loss_members,2},
    {"webgl_buffer",&webgl_types[0],nullptr,0},
    {"webgl_vertex_array_object",&webgl_types[1],nullptr,0},
    {"webgl_texture",&webgl_types[2],nullptr,0},
    {"webgl_program",&webgl_types[3],nullptr,0},
    {"webgl_internal_target",&webgl_types[4],nullptr,0},
    {"webgl_shader",&webgl_types[5],nullptr,0},
    {"webgl_framebuffer",&webgl_types[6],nullptr,0},
    {"webgl_renderbuffer",&webgl_types[7],nullptr,0},
    {"webgl_uniform_location",&webgl_types[8],nullptr,0},
    {"webgl_capability_extension",&webgl_types[11],nullptr,0},

    {"webgl2_rendering_context",&webgl_types[WEBGL_CONTEXT_KIND],webgl_members,sizeof(webgl_members)/sizeof(webgl_members[0])}
};
static const JubeModuleDef webgl_module={JUBE_ABI_VERSION,sizeof(JubeModuleDef),"webgl","0.1.0","Radiant WebGL2 host interfaces",
    webgl_types,sizeof(webgl_types)/sizeof(webgl_types[0]),nullptr,0,nullptr,0,nullptr,nullptr,webgl_interface,webgl_bindings,sizeof(webgl_bindings)/sizeof(webgl_bindings[0])};
extern "C" void dom_webgl_register_static(void) { jube_register_static_module(&webgl_module); }
static Item webgl_illegal_constructor(Item,Item,Item*,int,uint64_t*) { return dom_realm_throw_type_error("Illegal constructor"); }
static Item webgl_illegal_construct(Item,Item*,int,Item,uint64_t*) { return dom_realm_throw_type_error("Illegal constructor"); }
extern "C" void dom_webgl_install_globals(void) {
    // lazy module activation must precede publishing constructor prototype identities.
    if (!jube_find_type_by_host_type(&webgl_types[WEBGL_CONTEXT_KIND])) return;
    const char* names[]={"WebGLBuffer","WebGLVertexArrayObject","WebGLTexture","WebGLProgram",nullptr,"WebGLShader","WebGLFramebuffer","WebGLRenderbuffer","WebGLUniformLocation","WebGL2RenderingContext"};
    RootFrame roots(4);Rooted<Item> global(roots,dom_realm_global());Rooted<Item> constructor(roots,ItemNull);Rooted<Item> prototype(roots,ItemNull);
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) if(names[i]) {
        constructor.set(js_new_native_body_constructor(webgl_illegal_constructor,webgl_illegal_construct,0));
        prototype.set(jube_type_prototype(&webgl_types[i]));
        // native constructor prototypes are non-writable after materialization.
        dom_realm_init_constructor_prototype(constructor.get(),prototype.get());dom_realm_set_name(prototype.get(),"constructor",constructor.get());
        dom_realm_set_name(global.get(),names[i],constructor.get());
    }
}
Item dom_webgl_context_for(Item canvas,Item options) {
    if(!dom_is_html_canvas_element(canvas)) return ItemNull;
    RootFrame roots(3);Rooted<Item> canvas_root(roots,canvas);Rooted<Item> options_root(roots,options);Rooted<Item> value(roots,ItemNull);
    WebGlRealm* state=webgl_realm();if(!state) return ItemNull;
    for(int64_t i=0;i<root_vector_count(&state->objects);i++) {
        Item* cached=root_vector_at(&state->objects,i);WebGlObject* object=cached?webgl_object(*cached,WEBGL_CONTEXT_KIND):nullptr;
        if(object&&webgl_owner(object).item==canvas_root.get().item) return *cached;
    }
    WebGlOptions config={true,true,false,true,true,false};
    if(js_is_object_value(options_root.get())) {
        const char* names[]={"alpha","depth","stencil","antialias","premultipliedAlpha","preserveDrawingBuffer"};
        bool* fields[]={&config.alpha,&config.depth,&config.stencil,&config.antialias,&config.premultiplied_alpha,&config.preserve};
        for(unsigned i=0;i<6;i++) {
            value.set(dom_realm_get_name(options_root.get(),names[i]));if(item_is_error(value.get())) return value.get();
            if(!(value.get().item==ITEM_JS_UNDEFINED)) *fields[i]=js_is_truthy(value.get());
        }
    }
    uint64_t id=radiant_webgl_create(dom_unwrap_element(canvas_root.get()),&config);if(!id) return ItemNull;
    WebGlObject record={};record.canvas=id;record.options=config;
    return webgl_wrap(WEBGL_CONTEXT_KIND,record,canvas_root.get());
}
