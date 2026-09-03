// The script-realm API's only implementation (ES47). This translation unit is
// the one place under lambda/dom/ that names the runtime's realm operations;
// everything above it calls dom_realm_*. Each body is a forward, deliberately:
// the value of the boundary is that it is named and countable, not that it
// transforms anything. A realm that is not JavaScript replaces this file.

#include "dom_realm.h"
#include "../../js/js_runtime.h"
#include "../../js/js_property_attrs.h"
#include "../../js/js_event_loop.h"

extern "C" Item js_prototype_lookup_ex(Item object, Item property, bool* out_found);

Item dom_realm_get(Item o, Item k) { return js_get_key_default(o, k); }
Item dom_realm_get_cstr(Item o, const char* k) { return js_get_key_cstr(o, k); }
Item dom_realm_set(Item o, Item k, Item v) { return js_set_key_default(o, k, v); }
Item dom_realm_set_cstr(Item o, const char* k, Item v) { return js_set_key_cstr(o, k, v); }
Item dom_realm_get_name(Item o, const char* n, int len) { return js_get_name_key(o, n, len); }
Item dom_realm_get_name(Item o, const char* n) { return js_get_name_key(o, n); }
Item dom_realm_set_name(Item o, const char* n, int len, Item v) { return js_set_name_key(o, n, len, v); }
Item dom_realm_set_name(Item o, const char* n, Item v) { return js_set_name_key(o, n, v); }
Item dom_realm_own_property_names(Item o) { return js_object_get_own_property_names(o); }
Item dom_realm_own_property_descriptor(Item o, Item n) { return js_object_get_own_property_descriptor(o, n); }

Item dom_realm_receiver(void) { return js_get_this(); }
Item dom_realm_global(void) { return js_get_global_this(); }

Item dom_realm_call(Item f, Item t, Item* a, int n) { return js_call_function(f, t, a, n); }
Item dom_realm_call_into(Item f, Item t, Item* a, int n, uint64_t* home) {
    return js_call_function_into(f, t, a, n, home);
}
bool dom_realm_is_callable(Item v) { return js_is_callable(v); }

Item dom_realm_throw(Item v) { return js_throw_value(v); }
Item dom_realm_throw_type_error(const char* m) { return js_throw_type_error(m); }
Item dom_realm_new_error(Item m) { return js_new_error(m); }
Item dom_realm_new_error_named(Item name, Item m) { return js_new_error_with_name(name, m); }

Item dom_realm_new_object_of_class(int c) { return js_new_object_with_class(c); }
Item dom_realm_new_array_of_class(int len, int c) { return js_array_new_with_class(len, c); }
Item dom_realm_define_property(Item o, Item n, Item d) { return js_object_define_property(o, n, d); }
void dom_realm_install_accessor(Item o, Item n, Item g, Item s, uint8_t a) {
    js_install_native_accessor(o, n, g, s, a);
}
Item dom_realm_prototype_of(Item o) { return js_get_prototype(o); }
Item dom_realm_prototype_lookup(Item o, Item p, bool* found) { return js_prototype_lookup_ex(o, p, found); }
Item dom_realm_intrinsic_prototype(int c) { return js_get_intrinsic_prototype_for_class(c); }
Item dom_realm_init_constructor_prototype(Item ctor, Item proto) {
    return js_initialize_native_constructor_prototype(ctor, proto);
}

Item dom_realm_promise_new(Item e) { return js_promise_create(e); }
Item dom_realm_promise_resolve(Item v) { return js_promise_resolve(v); }
Item dom_realm_promise_reject(Item r) { return js_promise_reject(r); }
Item dom_realm_promise_then(Item p, Item f, Item r) { return js_promise_then(p, f, r); }
Item dom_realm_promise_all(Item it) { return js_promise_all(it); }
void dom_realm_microtask_flush(void) { js_microtask_flush(); }

#define DOM_REALM_DEFINE_NATIVE(arity) \
    Item dom_realm_new_function(DomRealmFn##arity t) { return js_new_native_function(t); } \
    Item dom_realm_new_function(DomRealmFn##arity t, int n) { return js_new_native_function(t, n); } \
    void dom_realm_set_native(Item o, Item k, DomRealmFn##arity t) { js_set_native_key(o, k, t); } \
    Item dom_realm_install_method(Item o, const char* nm, DomRealmFn##arity t) { \
        return js_install_native_method(o, nm, t); } \
    Item dom_realm_install_method(Item o, const char* nm, DomRealmFn##arity t, int n) { \
        return js_install_native_method(o, nm, t, n); }
DOM_REALM_ARITIES(DOM_REALM_DEFINE_NATIVE)
#undef DOM_REALM_DEFINE_NATIVE
