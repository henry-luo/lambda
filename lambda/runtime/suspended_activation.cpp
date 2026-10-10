#include "suspended_activation.hpp"
#include "gc/gc_heap.h"
#include <assert.h>

extern "C" void js_iterator_map_gc_trace(Map* map, gc_heap_t* gc) {
    if (!map || !gc || map->type_id != LMD_TYPE_MAP || map->map_kind != MAP_KIND_ITERATOR) return;
    gc_mark_item(gc, ((JsIteratorMapCarrier*)map)->payload.source.item);
}
extern "C" void js_iterator_map_heap_destroy(Map* map) {
    if (!map || map->map_kind != MAP_KIND_ITERATOR) return;
    JsIterData* data = &((JsIteratorMapCarrier*)map)->payload;
    if (!data->ordered_projection || data->source.item == ITEM_JS_UNDEFINED) return;
    // the immutable source predates this nonmoving carrier; newest-first sweep releases the lease first.
    OrderedMap* source = (OrderedMap*)data->source.map;
    assert(source->map_kind == MAP_KIND_ORDERED && source->cursors > 0);
    source->cursors--; data->source.item = ITEM_JS_UNDEFINED;
}

static void js_suspended_activation_trace_environment(void* context,
        void* environment) {
    gc_mark_object_ptr((gc_heap_t*)context, environment);
}

void js_suspended_activation_gc_trace(
        const JsSuspendedActivation* activation, gc_heap_t* gc) {
    if (!activation || !gc) return;
    durable_activation_visit_environment(activation, gc,
        js_suspended_activation_trace_environment);
    if (activation->with_env) gc_mark_object_ptr(gc, activation->with_env);
    gc_mark_item(gc, activation->ast_function.item);
    gc_mark_item(gc, activation->ast_arguments.item);
    // A parked interpreted body's frames live in its activation's segment.
    activation_trace(activation->activation, gc);
    if (activation->ast_function_env) {
        gc_mark_object_ptr(gc, activation->ast_function_env);
    }
    if (activation->ast_body_env) {
        gc_mark_object_ptr(gc, activation->ast_body_env);
    }
}

extern "C" void js_generator_map_gc_trace(Map* map, gc_heap_t* gc) {
    if (!map || !gc || map->map_kind != MAP_KIND_GENERATOR) return;
    JsGeneratorStateRecord* gen = &((JsGeneratorMapCarrier*)map)->state;
    js_suspended_activation_gc_trace(gen, gc);
    gc_mark_item(gc, gen->private_home_class.item);
    gc_mark_item(gc, gen->delegate.item);
    gc_mark_item(gc, gen->ast_this.item);
}

// A generator collected before it finishes abandons its parked activation:
// the stack is released and none of its code runs again (RA7).
extern "C" void js_generator_map_heap_destroy(Map* map) {
    if (!map || map->map_kind != MAP_KIND_GENERATOR) return;
    JsGeneratorStateRecord* gen = &((JsGeneratorMapCarrier*)map)->state;
    activation_destroy(gen->activation);
    gen->activation = NULL;
}
