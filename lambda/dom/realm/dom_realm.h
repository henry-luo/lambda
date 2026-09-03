#pragma once

/**
 * dom_realm.h — the script-realm API (ES47).
 *
 * The DOM core reaches the script runtime for things only a realm can do: run
 * the property protocol on an object, call a script function, read the current
 * receiver, raise a script exception, make a native function, settle a promise.
 * Those are *not* the shared value model. Building a plain map or array is ES12
 * — one runtime, values both realms read — and stays a direct `js_*` call.
 *
 * Everything here was a direct `js_*` call from `lambda/dom/`, which made the
 * core's dependency on the JS realm invisible: 785 call sites across 34 symbols,
 * indistinguishable at a glance from the sanctioned value helpers beside them.
 * Naming the boundary is what lets it be counted, and later re-pointed at a
 * realm that is not JavaScript.
 *
 * The single rule: **`lambda/dom/realm/` is the only place under `lambda/dom/`
 * that may name these symbols.** Everything above it calls `dom_realm_*`.
 */

#include "../../lambda-data.hpp"

// Structurally identical to the runtime's native-adapter pointers, so the
// overload sets below resolve the same way without this header pulling in the
// JS runtime's own. A typedef is an alias, not a distinct type.
typedef Item (*DomRealmFn0)(void);
typedef Item (*DomRealmFn1)(Item);
typedef Item (*DomRealmFn2)(Item, Item);
typedef Item (*DomRealmFn3)(Item, Item, Item);
typedef Item (*DomRealmFn4)(Item, Item, Item, Item);
typedef Item (*DomRealmFn5)(Item, Item, Item, Item, Item);
typedef Item (*DomRealmFn6)(Item, Item, Item, Item, Item, Item);
typedef Item (*DomRealmFn7)(Item, Item, Item, Item, Item, Item, Item);
typedef Item (*DomRealmFn8)(Item, Item, Item, Item, Item, Item, Item, Item);

// --- properties: the ECMAScript property protocol, not a map-slot write ------
// These run OrdinaryGet/OrdinarySet, so prototypes, accessors and proxies are
// all in play. That is why they are realm calls and `js_new_object` is not.
Item dom_realm_get(Item object, Item key);
Item dom_realm_get_cstr(Item object, const char* key);
Item dom_realm_set(Item object, Item key, Item value);
Item dom_realm_set_cstr(Item object, const char* key, Item value);
Item dom_realm_get_name(Item object, const char* name, int len);
Item dom_realm_get_name(Item object, const char* name);
Item dom_realm_set_name(Item object, const char* name, int len, Item value);
Item dom_realm_set_name(Item object, const char* name, Item value);
Item dom_realm_own_property_names(Item object);
Item dom_realm_own_property_descriptor(Item object, Item name);

// --- the current activation ---------------------------------------------
// The receiver of the script call in progress. A Lambda caller has none, so
// every use is a place the core assumes it was entered from a script method.
Item dom_realm_receiver(void);
Item dom_realm_global(void);

// --- functions -----------------------------------------------------------
Item dom_realm_call(Item func, Item this_val, Item* args, int argc);
Item dom_realm_call_into(Item func, Item this_val, Item* args, int argc,
                         uint64_t* result_home);
bool dom_realm_is_callable(Item value);

// --- exceptions ----------------------------------------------------------
Item dom_realm_throw(Item value);
Item dom_realm_throw_type_error(const char* message);
Item dom_realm_new_error(Item message);
Item dom_realm_new_error_named(Item error_name, Item message);

// --- the script object model --------------------------------------------
Item dom_realm_new_object_of_class(int class_id);
Item dom_realm_new_array_of_class(int length, int class_id);
Item dom_realm_define_property(Item object, Item name, Item descriptor);
void dom_realm_install_accessor(Item object, Item name, Item getter, Item setter,
                                uint8_t attrs);
Item dom_realm_prototype_of(Item object);
Item dom_realm_prototype_lookup(Item object, Item property, bool* out_found);
Item dom_realm_intrinsic_prototype(int class_id);
Item dom_realm_init_constructor_prototype(Item constructor, Item prototype);

// --- promises and the job queue -----------------------------------------
Item dom_realm_promise_new(Item executor);
Item dom_realm_promise_resolve(Item value);
Item dom_realm_promise_reject(Item reason);
Item dom_realm_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
Item dom_realm_promise_all(Item iterable);
void dom_realm_microtask_flush(void);

// --- native functions: one overload per arity, matching the runtime -------
#define DOM_REALM_ARITIES(M) M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8)
#define DOM_REALM_DECLARE_NATIVE(arity) \
    Item dom_realm_new_function(DomRealmFn##arity target); \
    Item dom_realm_new_function(DomRealmFn##arity target, int adapter_arity); \
    void dom_realm_set_native(Item object, Item key, DomRealmFn##arity target); \
    Item dom_realm_install_method(Item object, const char* name, DomRealmFn##arity target); \
    Item dom_realm_install_method(Item object, const char* name, DomRealmFn##arity target, int adapter_arity);
DOM_REALM_ARITIES(DOM_REALM_DECLARE_NATIVE)
#undef DOM_REALM_DECLARE_NATIVE
