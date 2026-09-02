/**
 * js_dom_adapter.cpp — JS-side adapter for the DOM core (F25 / ES33).
 *
 * The DOM core in lambda/dom/ is realm-neutral: it drives Radiant's tree and is
 * reachable from both page JavaScript and Lambda script. Where it needs a
 * service that only the JS realm can provide, it declares a neutral entry point
 * and this file supplies the JS-specific implementation.
 *
 * Today that is the scheduling seam. The core asks for "run this once the
 * current turn settles"; the JS event loop is what actually runs it, and this
 * translation lives here so lambda/dom/ keeps no direct dependency on
 * js_event_loop.h. See vibe/Lambda_Design_DOM_API.md ES33.
 */

#include "../lambda-data.hpp"
#include "../dom/dom.h"
#include "js_event_loop.h"
#include "js_runtime.h"

extern "C" void dom_schedule_microtask(Item callback) {
    js_microtask_enqueue(callback);
}

extern "C" void dom_schedule_task(Item callback) {
    // Zero delay: this is "after the current turn", not timed work. setTimeout(0)
    // is the loop's existing spelling for that, so the seam needs no delay
    // parameter until a caller genuinely wants one.
    js_setTimeout(callback, (Item){.item = i2it(0)});
}

// ---------------------------------------------------------------------------
// Realm-prototype seam (ESO79)
//
// "Where does this realm keep the Range constructor?" is a question only the JS
// side can answer, so the core asks by name and this file does the lookup. Each
// entry tolerates a missing realm or a missing constructor by answering null,
// because the same DOM construction paths run for Lambda-only documents that
// have no JS global at all.
// ---------------------------------------------------------------------------

extern "C" Item dom_realm_constructor(const char* ctor_name) {
    if (!ctor_name || !ctor_name[0]) return ItemNull;
    Item global = js_get_global_this();
    if (get_type_id(global) != LMD_TYPE_MAP) return ItemNull;
    Item ctor = js_get_key_default(global, js_name_item(ctor_name));
    return get_type_id(ctor) == LMD_TYPE_FUNC ? ctor : ItemNull;
}

extern "C" Item dom_realm_constructor_prototype(const char* ctor_name) {
    Item ctor = dom_realm_constructor(ctor_name);
    if (ctor.item == ItemNull.item) return ItemNull;
    Item proto = js_get_key_cstr(ctor, "prototype");
    return get_type_id(proto) == LMD_TYPE_MAP ? proto : ItemNull;
}

extern "C" void dom_realm_apply_prototype(Item value, const char* ctor_name) {
    Item proto = dom_realm_constructor_prototype(ctor_name);
    if (proto.item != ItemNull.item) js_set_prototype(value, proto);
}
