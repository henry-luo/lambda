// DOM3 interface compiler + record-driven host-object dispatch.
//
// A module declares its script-facing shape once, in Lambda type syntax
// (JubeModuleDef.interface_decl), and supplies behavior as binding tables
// (JubeTypeBinding / JubeMemberBind). At registration this file parses the
// declaration with the first-party Lambda parser, cross-checks it
// against the bindings, and compiles per-type member records plus a
// content-hashed name index dual-keyed on snake_case and camelCase. The
// generic host-object paths consult these records, so a fully declared type
// needs no hand-written dispatch at all. Runtime Jube registration therefore
// does not depend on the editor-oriented Tree-sitter frontend.

#include "jube_interface.h"
#include "jube_registry.h"
#include "../lambda.hpp"
#include "../js/js_runtime_internal.hpp"
#include "../js/js_property_attrs.h"
#include "../js/js_props.h"
#include "../runtime/heap_api.h"
#include "../runtime/lambda-number-runtime.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/str.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/hashmap_typed.hpp"
#include "../../lib/hash.h"
#include "../../lib/arraylist.hpp"
#include "../runtime/parser/lambda_rd_parser.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// engine entry points not exposed through public headers
extern __thread EvalContext* context;
// ============================================================================
// Compiled records
// ============================================================================

typedef enum JubeMemberKind {
    JUBE_MEMBER_FIELD = 0,   // data property backed by get/set handlers
    JUBE_MEMBER_METHOD,      // fn-typed member backed by a call handler
    JUBE_MEMBER_CONST,       // default literal, no binding
} JubeMemberKind;

typedef struct JubeMemberRecord JubeMemberRecord;
struct JubeMemberRecord {
    struct JubeTypeRecord* declaring_type;
    JubeMemberRecord* canonical_record; // inherited methods share their declaring interface
    bool (*prototype_receiver)(Item); // adapter-owned WebIDL receiver brand
    const JubeMemberBind* bind;   // NULL for constants
    char* snake_name;             // declared spelling (owned copy)
    char* camel_name;             // derived or js_name override (owned copy)
    uint8_t kind;                 // JubeMemberKind
    bool readonly;                // no set binding and not reflected
    bool enumerable;              // fields only; aliases/constants/methods opt out
    bool can_raise;
    int arity;                    // methods: declared parameter count
    const JubeTypeDef* result_type; // resolved field/method result type, if declared
    int64_t const_int;            // CONST int payload
    char* const_str;              // CONST string payload (owned copy, or NULL)
    bool const_is_str;
    Item method_fn;               // cached function object (lazy, GC-rooted)
    bool method_fn_rooted;
    Context* method_root_owner;   // heap that owns the cached root
    uint64_t method_root_generation;
};

typedef struct JubeTypeRecord {
    const JubeTypeDef* type;      // brand: vmap->host_type of instances
    const JubeTypeBinding* binding;
    struct JubeTypeRecord* base_record;
    int type_slot;                // process-stable registry slot
    int family_root_slot;         // base slot shared by derived records
    JubeMemberRecord* members;    // stable array, declaration order
    int member_count;
    HashMap* index;               // content-hashed name -> one member record
    Item prototype;               // lazy per-type prototype object (GC-rooted)
    bool prototype_rooted;
    bool prototype_absent;        // current realm intentionally has no prototype
    Context* prototype_root_owner; // heap that owns the cached root
    uint64_t prototype_root_generation;
} JubeTypeRecord;

typedef struct JubeTypeIndexEntry {
    const void* type;
    JubeTypeRecord* record;
} JubeTypeIndexEntry;

// optional renderer interfaces grow the catalog; record addresses remain stable for cached dispatch.
static lam::ArrayList<JubeTypeRecord*> s_type_records(MEM_CAT_CONTAINER,0);
static int s_type_record_count = 0;
static HashMap* s_type_index = NULL;

typedef struct JubeMemberIndexEntry {
    const char* chars;
    uint32_t len;
    JubeMemberRecord* rec;
} JubeMemberIndexEntry;

typedef TypedHashMap<JubeMemberIndexEntry,
    HashMapLenStrMemberKeyOps<JubeMemberIndexEntry, &JubeMemberIndexEntry::chars,
        &JubeMemberIndexEntry::len>> JubeMemberIndex;
typedef TypedHashMap<JubeTypeIndexEntry,
    HashMapPointerMemberKeyOps<JubeTypeIndexEntry, &JubeTypeIndexEntry::type>> JubeTypeIndex;

// ============================================================================
// Small helpers
// ============================================================================

static Item jube_undefined_item(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static bool jube_cached_generation_is_current(Context* owner, uint64_t generation) {
    Context* active = (Context*)context;
    return owner == active && generation != 0 &&
        generation == heap_generation_for(active);
}

static bool jube_cached_root_is_current(bool rooted, Context* owner,
                                        uint64_t generation) {
    return rooted && jube_cached_generation_is_current(owner, generation);
}

static void jube_cached_root_drop(Item* item, bool* rooted, Context** owner,
                                  uint64_t* generation) {
    if (!item || !rooted || !owner || !generation) return;
    if (*rooted && *owner && *generation != 0 &&
            heap_generation_for(*owner) == *generation) {
        heap_unregister_gc_root_for(*owner, &item->item);
    }
    *item = ItemNull;
    *rooted = false;
    *owner = NULL;
    *generation = 0;
}

static bool jube_cached_root_register(Item* item, bool* rooted, Context** owner,
                                      uint64_t* generation) {
    Context* active = (Context*)context;
    uint64_t active_generation = heap_generation_for(active);
    if (!active || active_generation == 0 ||
            !heap_register_gc_root_for(active, &item->item)) return false;
    *rooted = true;
    *owner = active;
    *generation = active_generation;
    return true;
}

static bool jube_item_key_chars(Item key, const char** chars, uint32_t* len) {
    TypeId type_id = get_type_id(key);
    if (!is_text_type_id(type_id)) return false;
    // JS Symbol descriptions never select a string-named member or numeric index.
    if (js_key_is_symbol(key) || (type_id == LMD_TYPE_STRING &&
            property_key_requires_identity(it2s(key)))) return false;
    const char* key_chars = key.get_chars();
    if (!key_chars) return false;
    *chars = key_chars;
    *len = key.get_len();
    return true;
}

static Item jube_name_item(const char* name) {
    return (Item){.item = s2it(heap_create_name(name))};
}

static bool jube_index_from_key(Item key, int64_t* out) {
    int64_t index = -1;
    if (!lambda_item_to_int64_exact(key, &index)) {
        const char* digits = NULL;
        uint32_t length = 0;
        if (!jube_item_key_chars(key, &digits, &length) || length == 0 ||
                (length > 1 && digits[0] == '0')) {
            return false;
        }
        index = 0;
        for (uint32_t i = 0; i < length; i++) {
            if (digits[i] < '0' || digits[i] > '9' ||
                    index > (INT64_MAX - (digits[i] - '0')) / 10) {
                return false;
            }
            index = index * 10 + (digits[i] - '0');
        }
    }
    // WebIDL property indices use canonical array-index keys, not numeric aliases.
    if (index < 0 || index >= UINT32_MAX) return false;
    *out = index;
    return true;
}

// snake_case -> camelCase; returns owned copy (identity copy when no '_')
static char* jube_derive_camel(const char* snake) {
    size_t len = strlen(snake);
    // Matches str_dup(): member metadata is released outside a JS heap
    // lifetime, so its ownership must not depend on memtrack's current mode.
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    size_t oi = 0;
    for (size_t i = 0; i < len; i++) {
        if (snake[i] == '_' && i + 1 < len && snake[i + 1] >= 'a' && snake[i + 1] <= 'z') {
            out[oi++] = (char)(snake[i + 1] - 'a' + 'A');
            i++;
        } else {
            out[oi++] = snake[i];
        }
    }
    out[oi] = '\0';
    return out;
}

static JubeTypeRecord* jube_record_for_type(const void* host_type) {
    if (!host_type || !s_type_index) return NULL;
    JubeTypeIndexEntry probe = {host_type, NULL};
    const JubeTypeIndexEntry* found = JubeTypeIndex::get(s_type_index, probe);
    return found ? found->record : NULL;
}

static JubeTypeRecord* jube_record_for(Item receiver) {
    const void* host_type = virtual_host_type(receiver);
    return host_type ? jube_record_for_type(host_type) : NULL;
}

void* jube_host_identity(Item item) {
    JubeTypeRecord* trec = jube_record_for(item);
    if (!trec) return NULL;
    return virtual_host_data(item);
}

static bool jube_native_alive(Item receiver) {
    return virtual_host_data(receiver) != NULL;
}

static bool jube_js_indexed(JubeTypeRecord* trec) {
    return trec && trec->binding && trec->binding->indexed_get &&
        !(trec->type->flags & JUBE_TYPE_NATIVE_INDEXED);
}

static int64_t jube_indexed_length(Item receiver, JubeTypeRecord* trec) {
    if (!trec || !trec->binding || !trec->binding->indexed_get ||
            !jube_native_alive(receiver)) return -1;
    if (get_type_id(receiver) == LMD_TYPE_VARRAY) return varray_count(receiver.varray);
    RootFrame roots(1);
    Rooted<Item> receiver_root(roots, receiver);
    return trec->binding->indexed_length ? trec->binding->indexed_length(receiver_root.get()) : -1;
}

// resolve a key against the type's compiled ordinal index.
static JubeMemberRecord* jube_resolve_member(JubeTypeRecord* trec, Item receiver, Item key,
        bool js_surface = true) {
    (void)receiver;
    const char* chars = NULL;
    uint32_t len = 0;
    if (!trec->index || !jube_item_key_chars(key, &chars, &len)) return NULL;
    JubeMemberIndexEntry probe = {chars, len, NULL};
    const JubeMemberIndexEntry* found = JubeMemberIndex::get(trec->index, probe);
    if (!found) return NULL;
    JubeMemberRecord* rec = found->rec;
    if (js_surface && (trec->type->flags & JUBE_TYPE_JS_EXACT_NAMES) &&
            (strlen(rec->camel_name) != len || memcmp(rec->camel_name, chars, len) != 0))
        return NULL;
    return rec;
}

// ============================================================================
// Generic expando store: every virtual carrier owns one explicit Item edge.
// GC marks that edge, so the expando object dies with the wrapper.
// ============================================================================

static Item jube_expando_object(Item receiver, bool create) {
    VirtualContainer* container = virtual_container_from_item(receiver);
    if (!container) return jube_undefined_item();
    Item existing = container->expando;
    if (get_type_id(existing) == LMD_TYPE_MAP && existing.map) return existing;
    if (!create) return jube_undefined_item();

    RootFrame roots(2);
    Rooted<Item> receiver_root(roots, receiver);
    Rooted<Item> object_root(roots, ItemNull);
    const JubeHostAPI* host = jube_internal_host_api();
    object_root.set(host->value->new_object());
    container = virtual_container_from_item(receiver_root.get());
    if (!container || get_type_id(object_root.get()) != LMD_TYPE_MAP) {
        return jube_undefined_item();
    }
    container->expando = object_root.get();
    return object_root.get();
}

static bool jube_prototype_member(const JubeMemberRecord* rec) {
    if (!rec) return false;
    const JubeMemberRecord* canonical = rec->canonical_record
        ? rec->canonical_record : rec;
    return canonical->prototype_receiver ||
        (rec->bind && (rec->bind->flags & JUBE_MEMBER_PROTOTYPE));
}

static bool jube_prototype_member_for(const JubeMemberRecord* rec, Item receiver) {
    if (!rec) return false;
    const JubeMemberRecord* canonical = rec->canonical_record
        ? rec->canonical_record : rec;
    // A module host type may cover several WebIDL interfaces, including
    // legacy projections outside the interface that publishes this member.
    // Methods follow the receiver's actual interface chain; shared host storage
    // must not make Element-only operations appear on DocumentFragment.
    return (canonical->prototype_receiver &&
        (rec->kind == JUBE_MEMBER_METHOD || canonical->prototype_receiver(receiver))) ||
        (rec->bind && (rec->bind->flags & JUBE_MEMBER_PROTOTYPE));
}

static bool jube_keyed_accessor(const JubeMemberRecord* rec) {
    return rec && rec->bind && (rec->bind->flags & JUBE_MEMBER_KEYED_ACCESSOR);
}

static bool jube_js_named(const JubeTypeRecord* trec) {
    return trec && !(trec->type->flags & JUBE_TYPE_NATIVE_NAMED);
}

static int jube_dispatch_keyed_accessor(Item receiver, JubeMemberRecord* rec,
        Item value, Item* out, bool setter) {
    const JubeTypeBinding* binding = rec->declaring_type->binding;
    RootFrame roots(3);
    Rooted<Item> receiver_root(roots, receiver), value_root(roots, value);
    Rooted<Item> key_root(roots, jube_name_item(rec->bind->reflect_attr));
    return setter ? binding->named_set(receiver_root.get(), key_root.get(), value_root.get(), out)
        : binding->named_get(receiver_root.get(), key_root.get(), out);
}

static bool jube_expando_has(Item expando, Item key) {
    return get_type_id(expando) == LMD_TYPE_MAP && it2b(js_has_own_property(expando, key));
}

static Item jube_member_lookup_object(Item target, Item key) {
    Item expando = jube_expando_object(target, false);
    if (jube_expando_has(expando, key)) return expando;
    Item proto = ItemNull;
    // optional Lambda reads can miss without a JS arena; do not build a prototype there.
    if (js_active_runtime_state && js_input && js_input->pool)
        jube_member_prototype(target, &proto);
    return proto;
}

static bool jube_member_accepts_receiver(const JubeMemberRecord* rec, Item receiver) {
    const JubeMemberRecord* canonical = rec && rec->canonical_record
        ? rec->canonical_record : rec;
    if (canonical && canonical->prototype_receiver)
        return canonical->prototype_receiver(receiver);
    if (!jube_prototype_member(rec)) return true;
    for (JubeTypeRecord* type = jube_record_for(receiver); type; type = type->base_record) {
        if (type == rec->declaring_type) return jube_native_alive(receiver);
    }
    return false;
}

// ============================================================================
// Method function objects: one per member record. A typed payload body owns
// every arity, and the process-stable record pointer is not an Item/GC edge.
// ============================================================================

// DS13: a catalog row is `Item body(Item receiver, ...)` at its own arity, while
// a JS call arrives with any argument count. Reconciling the two is one switch
// here rather than an adapter per member: a missing argument is `undefined`
// (WebIDL's absent-argument value) and extras are dropped.
#define JUBE_ROW_MAX_ARGC 8
extern "C" void* jube_host_dom_row_slot(unsigned index);

static Item jube_invoke_row(const JubeMemberBind* bind, Item receiver,
        Item* args, int argc) {
    Item a[JUBE_ROW_MAX_ARGC];
    int want = (int)bind->row_argc - 1;          // row_argc counts the receiver
    if (want < 0) want = 0;
    if (want > JUBE_ROW_MAX_ARGC) want = JUBE_ROW_MAX_ARGC;
    for (int i = 0; i < want; i++) {
        a[i] = (args && i < argc) ? args[i] : jube_undefined_item();
    }
    void* b = jube_host_dom_row_slot(bind->row_index);
    if (!b) return jube_undefined_item();
    switch (bind->row_argc) {
    case 1: return ((Item (*)(Item))b)(receiver);
    case 2: return ((Item (*)(Item, Item))b)(receiver, a[0]);
    case 3: return ((Item (*)(Item, Item, Item))b)(receiver, a[0], a[1]);
    case 4: return ((Item (*)(Item, Item, Item, Item))b)(receiver, a[0], a[1], a[2]);
    case 5: return ((Item (*)(Item, Item, Item, Item, Item))b)(
        receiver, a[0], a[1], a[2], a[3]);
    case 6: return ((Item (*)(Item, Item, Item, Item, Item, Item))b)(
        receiver, a[0], a[1], a[2], a[3], a[4]);
    default:
        log_error("JUBE_ROW: unsupported row arity %u", (unsigned)bind->row_argc);
        return jube_undefined_item();
    }
}

static Item jube_tramp_invoke(Item fn_item, Item this_value, Item* args,
        int argc, uint64_t* result_home) {
    (void)result_home;
    JsFunction* fn = get_type_id(fn_item) == LMD_TYPE_FUNC
        ? (JsFunction*)fn_item.function : NULL;
    JubeMemberRecord* rec = fn
        ? (JubeMemberRecord*)(uintptr_t)js_fn_native(fn)->target.bits : NULL;
    Item out = jube_undefined_item();
    if (rec && !jube_member_accepts_receiver(rec, this_value))
        return js_throw_type_error("Illegal invocation of a host interface method");
    if (rec && rec->bind && (rec->bind->flags & JUBE_MEMBER_HAS_REQUIRED_ARGS) &&
            argc < (int)(rec->bind->flags >> 8))
        return js_throw_type_error("Not enough arguments for a host interface method");
    if (rec && rec->bind) {
        if (rec->bind->row_index) {
            out = jube_invoke_row(rec->bind, this_value, args, argc);
        } else if (rec->bind->call) {
            rec->bind->call(this_value, args, argc, &out);
        }
    }
    return out;
}

static Item jube_member_js_method_item(JubeMemberRecord* rec) {
    if (jube_cached_root_is_current(rec->method_fn_rooted,
                                    rec->method_root_owner,
                                    rec->method_root_generation)) {
        return rec->method_fn;
    }
    // A runtime can replace its heap without reaching the normal reset hook.
    // Drop the stale cache before publishing a function into the new realm.
    jube_cached_root_drop(&rec->method_fn, &rec->method_fn_rooted,
                          &rec->method_root_owner, &rec->method_root_generation);
    const JubeHostAPI* host = jube_internal_host_api();
    int arity = rec->bind && (rec->bind->flags & JUBE_MEMBER_HAS_REQUIRED_ARGS)
        ? (int)(rec->bind->flags >> 8) : rec->arity;
    if (arity < 0) arity = 0;
    // payload functions receive an argument span; WebIDL methods may require more than eight arguments.
    RootFrame roots(1);
    Rooted<Item> function_root(roots, js_new_native_payload_function(jube_tramp_invoke,
        (uint64_t)(uintptr_t)rec, arity));
    host->script->set_function_name(function_root.get(), jube_name_item(rec->camel_name));
    rec->method_fn = function_root.get();
    if (!jube_cached_root_register(&rec->method_fn, &rec->method_fn_rooted,
                                   &rec->method_root_owner,
                                   &rec->method_root_generation)) {
        rec->method_fn = ItemNull;
        return (Item){.item = ITEM_ERROR};
    }
    return rec->method_fn;
}

static Item jube_lambda_method_invoke(Item env_item, Item* args, int argc) {
    Item* env = (Item*)env_item.item;
    Item out = jube_undefined_item();
    JubeTypeRecord* trec = env ? jube_record_for(env[0]) : NULL;
    int64_t ordinal = env ? fn_int64_index(env[1]) : -1;
    JubeMemberRecord* rec = trec && ordinal >= 0 && ordinal < trec->member_count
        ? &trec->members[ordinal] : NULL;
    if (rec && rec->bind) {
        // both doors resolve a row the same way, or the Lambda face would keep
        // the adapter the JS face just lost (D6.2.2v2)
        if (rec->bind->row_index) {
            out = jube_invoke_row(rec->bind, env[0], args, argc);
        } else if (rec->bind->call) {
            rec->bind->call(env[0], args, argc, &out);
        }
    }
    return out;
}

// Keep the captured environment Item-typed: the shared hosted dispatcher
// rejects the old incompatible void* closure-prefix prototype.
static Item jube_lambda_method_tramp_0(Item env) {
    return jube_lambda_method_invoke(env, NULL, 0);
}
static Item jube_lambda_method_tramp_1(Item env, Item a0) {
    Item args[] = {a0};
    return jube_lambda_method_invoke(env, args, 1);
}
static Item jube_lambda_method_tramp_2(Item env, Item a0, Item a1) {
    Item args[] = {a0, a1};
    return jube_lambda_method_invoke(env, args, 2);
}
static Item jube_lambda_method_tramp_3(Item env, Item a0, Item a1, Item a2) {
    Item args[] = {a0, a1, a2};
    return jube_lambda_method_invoke(env, args, 3);
}
static Item jube_lambda_method_tramp_4(Item env, Item a0, Item a1, Item a2, Item a3) {
    Item args[] = {a0, a1, a2, a3};
    return jube_lambda_method_invoke(env, args, 4);
}
static Item jube_lambda_method_tramp_5(Item env, Item a0, Item a1, Item a2, Item a3,
                                       Item a4) {
    Item args[] = {a0, a1, a2, a3, a4};
    return jube_lambda_method_invoke(env, args, 5);
}
static Item jube_lambda_method_tramp_6(Item env, Item a0, Item a1, Item a2, Item a3,
                                       Item a4, Item a5) {
    Item args[] = {a0, a1, a2, a3, a4, a5};
    return jube_lambda_method_invoke(env, args, 6);
}
static Item jube_lambda_method_tramp_7(Item env, Item a0, Item a1, Item a2, Item a3,
                                       Item a4, Item a5, Item a6) {
    Item args[] = {a0, a1, a2, a3, a4, a5, a6};
    return jube_lambda_method_invoke(env, args, 7);
}

static void* const s_jube_lambda_method_tramps[8] = {
    (void*)jube_lambda_method_tramp_0, (void*)jube_lambda_method_tramp_1,
    (void*)jube_lambda_method_tramp_2, (void*)jube_lambda_method_tramp_3,
    (void*)jube_lambda_method_tramp_4, (void*)jube_lambda_method_tramp_5,
    (void*)jube_lambda_method_tramp_6, (void*)jube_lambda_method_tramp_7,
};

static Item jube_member_lambda_method_item(Item receiver, JubeMemberRecord* rec) {
    int arity = rec->arity;
    if (arity < 0) arity = 0;
    if (arity > 7) arity = 7;
    RootFrame roots(2);
    Rooted<Item> rooted_receiver(roots, receiver);
    Rooted<Function*> rooted_fn(roots, (Function*)NULL);
    // Lambda projection reads run outside js_input, so Jube methods cannot use
    // JS function allocation there. Inherited records retain their base ordinal.
    Function* fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    if (!fn) return jube_undefined_item();
    rooted_fn.set(fn);
    // The data-zone environment must acquire a rooted GC owner before another
    // allocation can collect or compact it. Allocating the Function first
    // closes the former ownerless-env window between these two allocations.
    Item* env = (Item*)heap_data_calloc(sizeof(Item) * 2);
    if (!env) return jube_undefined_item();
    fn = rooted_fn.get();
    env[0] = rooted_receiver.get();
    env[1] = (Item){.item = i2it(rec - rec->declaring_type->members)};
    function_init_abi(fn, LMD_TYPE_FUNC, FN_ENTRY_ABI_HOST_ADAPTER);
    fn->arity = (uint8_t)arity;
    fn->fn_type = NULL;
    fn->ptr = (fn_ptr)s_jube_lambda_method_tramps[arity];
    fn->closure_env = env;
    fn->name = rec->snake_name;
    // compaction copies exactly the declared Item slots; no native pointer tail.
    fn->closure_field_count = 2;
    return (Item){.function = fn};
}

static Item jube_member_method_item(Item receiver, JubeMemberRecord* rec, bool js_surface) {
    if (rec->canonical_record) rec = rec->canonical_record;
    // Pure Lambda evaluators have no JS capsule; do not dereference derived JS
    // TLS merely to choose the host-language method wrapper.
    if (js_surface && js_active_runtime_state && js_input && js_input->pool) {
        return jube_member_js_method_item(rec);
    }
    return jube_member_lambda_method_item(receiver, rec);
}

static Item jube_member_const_item(JubeMemberRecord* rec) {
    if (rec->const_is_str) {
        return rec->const_str ? jube_name_item(rec->const_str) : jube_undefined_item();
    }
    return (Item){.item = i2it(rec->const_int)};
}

// ============================================================================
// Dispatch entry points
// ============================================================================

bool jube_type_has_interface(const JubeTypeDef* type) {
    return jube_record_for_type((const void*)type) != NULL;
}

static JubeTypeRecord* jube_record_for_query(const JubeTypeDef* type, int ordinal) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    if (!trec || ordinal < 0 || ordinal >= trec->member_count) return NULL;
    return trec;
}

static uint64_t jube_digest_text(uint64_t hash, const char* text) {
    size_t len = text ? strlen(text) : 0;
    hash = hash_combine_u64(hash, (uint64_t)len);
    if (len > 0) hash = hash_combine_u64(hash, hashmap_hash_xxhash3_bytes(text, len, 0, 0));
    return hash;
}

extern "C" uint64_t jube_interface_registry_digest(void) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    hash = hash_combine_u64(hash, (uint64_t)JUBE_ABI_VERSION);
    hash = hash_combine_u64(hash, (uint64_t)s_type_record_count);
    for (int i = 0; i < s_type_record_count; i++) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!trec) continue;
        hash = jube_digest_text(hash, trec->type ? trec->type->name : NULL);
        hash = hash_combine_u64(hash, (uint64_t)trec->family_root_slot);
        hash = hash_combine_u64(hash, (uint64_t)trec->member_count);
        for (int ordinal = 0; ordinal < trec->member_count; ordinal++) {
            JubeMemberRecord* rec = &trec->members[ordinal];
            hash = jube_digest_text(hash, rec->snake_name);
            hash = jube_digest_text(hash, rec->camel_name);
            hash = hash_combine_u64(hash, (uint64_t)rec->kind);
            hash = hash_combine_u64(hash, (uint64_t)rec->arity);
            hash = hash_combine_u64(hash, rec->can_raise ? 1 : 0);
            hash = jube_digest_text(hash,
                rec->result_type ? rec->result_type->name : NULL);
            hash = jube_digest_text(hash, rec->bind ? rec->bind->name : NULL);
        }
    }
    return hash;
}

extern "C" const JubeTypeDef* jube_iface_type_by_name(const char* name,
                                                       uint32_t len) {
    if (!name || len == 0) return NULL;
    for (int i = 0; i < s_type_record_count; i++) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!trec || !trec->type || !trec->type->name) continue;
        if (strlen(trec->type->name) == len && memcmp(trec->type->name, name, len) == 0) {
            return trec->type;
        }
    }
    return NULL;
}

extern "C" int jube_iface_type_slot(const JubeTypeDef* type) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    return trec ? trec->family_root_slot : -1;
}

extern "C" int jube_member_count(const JubeTypeDef* type) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    return trec ? trec->member_count : -1;
}

extern "C" const char* jube_member_name_at(const JubeTypeDef* type, int ordinal,
                                             bool camel_case) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    if (!trec) return NULL;
    return camel_case ? trec->members[ordinal].camel_name :
                        trec->members[ordinal].snake_name;
}

extern "C" int jube_member_ordinal(const JubeTypeDef* type, const char* name,
                                    uint32_t len) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    if (!trec || !name || len == 0) return -1;
    for (int i = 0; i < trec->member_count; i++) {
        JubeMemberRecord* rec = &trec->members[i];
        if ((strlen(rec->snake_name) == len && memcmp(rec->snake_name, name, len) == 0) ||
            (strlen(rec->camel_name) == len && memcmp(rec->camel_name, name, len) == 0)) {
            return i;
        }
    }
    return -1;
}

extern "C" uint8_t jube_member_kind_at(const JubeTypeDef* type, int ordinal) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    return trec ? trec->members[ordinal].kind : UINT8_MAX;
}

extern "C" bool jube_member_can_raise_at(const JubeTypeDef* type, int ordinal) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    return trec ? trec->members[ordinal].can_raise : false;
}

extern "C" int jube_member_arity_at(const JubeTypeDef* type, int ordinal) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    return trec ? trec->members[ordinal].arity : -1;
}

extern "C" const JubeTypeDef* jube_member_result_type_at(const JubeTypeDef* type,
                                                          int ordinal) {
    JubeTypeRecord* trec = jube_record_for_query(type, ordinal);
    return trec ? trec->members[ordinal].result_type : NULL;
}

static JubeMemberRecord* jube_record_at_guarded(Item receiver, int slot,
                                                uint32_t ordinal,
                                                JubeTypeRecord** out_trec) {
    if (out_trec) *out_trec = NULL;
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || slot < 0 || trec->family_root_slot != slot ||
            ordinal >= (uint32_t)trec->member_count) return NULL;
    if (out_trec) *out_trec = trec;
    return &trec->members[ordinal];
}

static int jube_dispatch_get_record(Item receiver, JubeMemberRecord* rec, Item* out,
        bool js_surface = false) {
    if (!rec || !out) return 0;
    switch (rec->kind) {
    case JUBE_MEMBER_CONST:
        *out = jube_member_const_item(rec);
        return 1;
    case JUBE_MEMBER_METHOD:
        // A method may have an availability handler for a feature-gated
        // surface; the handler owns the semantic predicate, not the kernel.
        if (rec->bind && rec->bind->get && !rec->bind->get(receiver, out)) return 0;
        *out = jube_member_method_item(receiver, rec, js_surface);
        return 1;
    default:
        if (jube_keyed_accessor(rec))
            return jube_dispatch_keyed_accessor(receiver, rec, ItemNull, out, false);
        if (rec->bind && rec->bind->get && rec->bind->get(receiver, out)) return 1;
        return 0;
    }
}

static int jube_dispatch_set_record(Item receiver, JubeTypeRecord* trec,
                                    JubeMemberRecord* rec, Item value, Item* out,
                                    bool js_surface = false) {
    if (!trec || !rec || !out) return 0;
    if (jube_keyed_accessor(rec)) {
        int handled = jube_dispatch_keyed_accessor(receiver, rec, value, out, true);
        if (handled && js_surface && !item_is_error(*out)) *out = (Item){.item = ITEM_TRUE};
        return handled;
    }
    if (rec->readonly || rec->kind != JUBE_MEMBER_FIELD) {
        if (trec->binding && trec->binding->named_set && jube_native_alive(receiver) &&
                trec->binding->named_set(receiver, jube_name_item(rec->camel_name),
                                          value, out)) {
            if (js_surface && !item_is_error(*out)) *out = (Item){.item = ITEM_TRUE};
            return 1;
        }
        *out = js_surface ? (Item){.item = ITEM_FALSE} : value;
        return 1;
    }
    if (rec->bind && rec->bind->set && rec->bind->set(receiver, value, out)) {
        if (js_surface && !item_is_error(*out)) *out = (Item){.item = ITEM_TRUE};
        return 1;
    }
    return 0;
}

static Item jube_accessor_invoke(Item function, Item receiver, Item* args,
        int argc, uint64_t* result_home) {
    (void)result_home;
    JsFunction* fn = (JsFunction*)function.function;
    uintptr_t payload = (uintptr_t)js_fn_native(fn)->target.bits;
    bool setter = (payload & 1u) != 0;
    JubeMemberRecord* rec = (JubeMemberRecord*)(payload & ~(uintptr_t)1u);
    if (!jube_member_accepts_receiver(rec, receiver))
        return js_throw_type_error("Illegal invocation of a host interface accessor");
    RootFrame roots(2);
    Rooted<Item> receiver_root(roots, receiver);
    Rooted<Item> value_root(roots, argc ? args[0] : jube_undefined_item());
    Item out = jube_undefined_item();
    if (setter) {
        jube_dispatch_set_record(receiver_root.get(), jube_record_for(receiver_root.get()),
            rec, value_root.get(), &out, true);
        return item_is_error(out) ? out : jube_undefined_item();
    }
    jube_dispatch_get_record(receiver_root.get(), rec, &out, true);
    return out;
}

static Item jube_accessor_function(JubeMemberRecord* rec, bool setter) {
    RootFrame roots(2);
    Rooted<Item> function_root(roots, js_new_native_payload_function(jube_accessor_invoke,
        (uint64_t)((uintptr_t)rec | (setter ? 1u : 0u)), setter ? 1 : 0));
    Rooted<Item> name_root(roots, ItemNull);
    StrBuf* name = strbuf_new();
    strbuf_append_all(name, 2, setter ? "set " : "get ", rec->camel_name);
    name_root.set(jube_name_item(name->str));
    strbuf_free(name);
    jube_internal_host_api()->script->set_function_name(function_root.get(), name_root.get());
    return function_root.get();
}

static void jube_install_member_accessor(Item prototype, JubeMemberRecord* rec) {
    RootFrame roots(4);
    Rooted<Item> prototype_root(roots, prototype);
    Rooted<Item> key_root(roots, jube_name_item(rec->camel_name));
    Rooted<Item> getter_root(roots, jube_accessor_function(rec, false));
    Rooted<Item> setter_root(roots, ItemNull);
    if (rec->bind->set || jube_keyed_accessor(rec))
        setter_root.set(jube_accessor_function(rec, true));
    js_install_native_accessor(prototype_root.get(), key_root.get(), getter_root.get(),
        setter_root.get(), 0);
}

static void jube_install_prototype_member(Item prototype, JubeMemberRecord* rec) {
    if (rec->kind == JUBE_MEMBER_FIELD) {
        if (jube_prototype_member(rec)) jube_install_member_accessor(prototype, rec);
    } else if (rec->kind == JUBE_MEMBER_METHOD) {
        RootFrame roots(2);
        Rooted<Item> prototype_root(roots, prototype);
        Rooted<Item> method_root(roots, jube_member_js_method_item(rec));
        jube_internal_host_api()->value->property_set(prototype_root.get(),
            jube_name_item(rec->camel_name), method_root.get());
    }
}

extern "C" bool jube_publish_prototype_member(const JubeTypeDef* type,
        Item prototype, const char* name, bool (*accepts_receiver)(Item)) {
    JubeTypeRecord* trec = jube_record_for_type(type);
    if (!trec || !name || !accepts_receiver || get_type_id(prototype) != LMD_TYPE_MAP)
        return false;
    JubeMemberIndexEntry probe = {name, (uint32_t)strlen(name), NULL};
    const JubeMemberIndexEntry* found = JubeMemberIndex::get(trec->index, probe);
    if (!found || !found->rec->bind) return false;
    JubeMemberRecord* rec = found->rec->canonical_record
        ? found->rec->canonical_record : found->rec;
    // Native brands are process-lifetime metadata; only the published Items
    // belong to the current realm (D6.2.2v2, D5.4.1).
    rec->prototype_receiver = accepts_receiver;
    jube_install_prototype_member(prototype, rec);
    return true;
}

static Item jube_type_prototype_for(JubeTypeRecord* trec) {
    if (jube_cached_root_is_current(trec->prototype_rooted,
                                    trec->prototype_root_owner,
                                    trec->prototype_root_generation)) {
        return trec->prototype;
    }
    if (trec->prototype_absent && jube_cached_generation_is_current(
            trec->prototype_root_owner, trec->prototype_root_generation)) {
        return ItemNull;
    }
    jube_cached_root_drop(&trec->prototype, &trec->prototype_rooted,
                          &trec->prototype_root_owner,
                          &trec->prototype_root_generation);
    trec->prototype_absent = false;
    const JubeHostAPI* host = jube_internal_host_api();
    // adopt the module's existing prototype object when one is seeded, so
    // constructor .prototype identity (instanceof) survives the conversion.
    // A seed returning a non-map means "this type has no prototype" (style
    // objects): record ItemNull, publish nothing, register no root.
    if (trec->binding && trec->binding->prototype_seed) {
        trec->prototype = trec->binding->prototype_seed();
        if (get_type_id(trec->prototype) != LMD_TYPE_MAP) {
            trec->prototype = ItemNull;
            trec->prototype_absent = true;
            trec->prototype_root_owner = (Context*)context;
            trec->prototype_root_generation =
                heap_generation_for(trec->prototype_root_owner);
            return trec->prototype;
        }
    } else {
        trec->prototype = host->value->new_object();
    }
    if (!jube_cached_root_register(&trec->prototype, &trec->prototype_rooted,
                                   &trec->prototype_root_owner,
                                   &trec->prototype_root_generation)) {
        trec->prototype = ItemNull;
        return (Item){.item = ITEM_ERROR};
    }
    if (trec->base_record) {
        Item parent = jube_type_prototype_for(trec->base_record);
        if (get_type_id(parent) == LMD_TYPE_MAP) js_set_prototype(trec->prototype, parent);
    }
    // publish method function objects onto the prototype: scripts read them as
    // Range.prototype.setStart (IDL shape / .length probes), and instance reads
    // must return the identical Item (range.setStart === Range.prototype.setStart)
    if (get_type_id(trec->prototype) == LMD_TYPE_MAP) {
        for (int i = 0; i < trec->member_count; i++) {
            JubeMemberRecord* rec = &trec->members[i];
            // inherited members live on the declaring prototype, including overrides.
            if (rec->declaring_type != trec) continue;
            jube_install_prototype_member(trec->prototype, rec);
        }
    }
    return trec->prototype;
}

// per-JS-runtime reset: prototype seeds read the CURRENT global constructor's
// .prototype, and batch runs recreate globals per script — cached prototypes
// and method items must drop so the next access rebuilds against the new
// runtime's globals (roots unregister while the old heap is still alive).
extern "C" void jube_interface_runtime_reset(void) {
    for (int i = 0; i < s_type_record_count; i++) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!trec) continue;
        jube_cached_root_drop(&trec->prototype, &trec->prototype_rooted,
                              &trec->prototype_root_owner,
                              &trec->prototype_root_generation);
        trec->prototype_absent = false;
        for (int j = 0; j < trec->member_count; j++) {
            JubeMemberRecord* rec = &trec->members[j];
            jube_cached_root_drop(&rec->method_fn, &rec->method_fn_rooted,
                                  &rec->method_root_owner,
                                  &rec->method_root_generation);
        }
    }
}

extern "C" Item jube_type_prototype(const JubeTypeDef* type) {
    JubeTypeRecord* trec = jube_record_for_type((const void*)type);
    if (!trec) return ItemNull;
    return jube_type_prototype_for(trec);
}

extern "C" int jube_member_get_by_ordinal(Item receiver, int slot,
                                           uint32_t ordinal, Item* out) {
    JubeTypeRecord* trec = NULL;
    JubeMemberRecord* rec = jube_record_at_guarded(receiver, slot, ordinal, &trec);
    if (!trec || !rec || !out) return 0;
    if (jube_prototype_member_for(rec, receiver)) return 0;
    if (!jube_native_alive(receiver)) {
        // the native payload is gone, but the wrapper identity remains a valid husk.
        *out = jube_undefined_item();
        return 1;
    }
    return jube_dispatch_get_record(receiver, rec, out, true);
}

extern "C" int jube_member_set_by_ordinal(Item receiver, int slot,
                                           uint32_t ordinal, Item value, Item* out) {
    JubeTypeRecord* trec = NULL;
    JubeMemberRecord* rec = jube_record_at_guarded(receiver, slot, ordinal, &trec);
    if (!trec || !rec || !out) return 0;
    if (jube_prototype_member_for(rec, receiver)) return 0;
    if (!jube_native_alive(receiver)) {
        *out = (Item){.item = ITEM_FALSE};
        return 1;
    }
    return jube_dispatch_set_record(receiver, trec, rec, value, out, true);
}

extern "C" int jube_member_call_by_ordinal(Item receiver, int slot,
                                            uint32_t ordinal, Item* args, int argc,
                                            Item* out) {
    JubeTypeRecord* trec = NULL;
    JubeMemberRecord* rec = jube_record_at_guarded(receiver, slot, ordinal, &trec);
    if (!trec || !rec || !out || rec->kind != JUBE_MEMBER_METHOD ||
            !rec->bind || (!rec->bind->call && !rec->bind->row_index)) return 0;
    if (jube_prototype_member_for(rec, receiver)) return 0;
    if (!jube_native_alive(receiver)) return 0;
    if (rec->bind->row_index) {
        *out = jube_invoke_row(rec->bind, receiver, args, argc);
        return 1;
    }
    return rec->bind->call(receiver, args, argc, out) ? 1 : 0;
}

static int jube_member_get_impl(Item target, Item key, Item receiver, Item* out, bool js_surface) {
    JubeTypeRecord* trec = jube_record_for(target);
    if (!trec || !out) return 0;
    RootFrame roots(4);
    Rooted<Item> target_root(roots, target), key_root(roots, key), receiver_root(roots, receiver);
    Rooted<Item> lookup_root(roots, ItemNull);
    if (!jube_native_alive(target)) {
        // neutered husk (post-release / document teardown): every read degrades
        // to undefined instead of touching the freed native payload
        *out = jube_undefined_item();
        return 1;
    }
    JubeMemberRecord* rec = jube_resolve_member(trec, target, key, js_surface);
    if (js_surface && (jube_prototype_member_for(rec, target) || !rec) &&
            js_active_runtime_state && js_input && js_input->pool) {
        lookup_root.set(jube_member_lookup_object(target_root.get(), key_root.get()));
        // native named hooks must not swallow an ordinary prototype interceptor.
        if (!rec && get_type_id(lookup_root.get()) == LMD_TYPE_MAP) {
            Item present = js_has_property(lookup_root.get(),
                js_property_lane_for_canonical_key(key_root.get()), key_root.get());
            if (item_is_error(present)) {*out = present; return 1;}
            if (!it2b(present)) lookup_root.set(ItemNull);
        }
        if (rec || get_type_id(lookup_root.get()) == LMD_TYPE_MAP) {
            *out = js_get_key_core(lookup_root.get(), key_root.get(), receiver_root.get());
            return 1;
        }
    }
    if (rec && jube_dispatch_get_record(target_root.get(), rec, out, js_surface)) {
        return 1;
    }
    // array-index reads (sheet[0]) resolve through the indexed hook; JS index
    // keys arrive as ints or all-digit strings depending on the access path
    if (trec->binding && trec->binding->indexed_get &&
            (!js_surface || jube_js_indexed(trec))) {
        int64_t index = -1;
        if (jube_index_from_key(key_root.get(), &index)) {
            int64_t count = jube_indexed_length(target_root.get(), trec);
            if ((count < 0 || index < count) &&
                    trec->binding->indexed_get(target_root.get(), index, out)) return 1;
        }
    }
    if ((!js_surface || jube_js_named(trec)) && trec->binding && trec->binding->named_get &&
            trec->binding->named_get(target_root.get(), key_root.get(), out)) {
        return 1;
    }
    const char* key_chars = NULL;
    uint32_t key_len = 0;
    if (jube_item_key_chars(key_root.get(), &key_chars, &key_len) && key_len == 9 &&
            memcmp(key_chars, "__proto__", 9) == 0) {
        *out = jube_type_prototype_for(trec);
        return 1;
    }
    // expando accessors need the original receiver, not the backing storage Map.
    lookup_root.set(jube_member_lookup_object(target_root.get(), key_root.get()));
    if (get_type_id(lookup_root.get()) == LMD_TYPE_MAP) {
        *out = js_get_key_core(lookup_root.get(), key_root.get(), receiver_root.get());
        return 1;
    }
    *out = jube_undefined_item();
    return 1;
}

// a derived JS capsule can exist during Lambda evaluation; the caller owns the surface choice.
int jube_member_get(Item receiver, Item key, Item* out) {
    return jube_member_get_impl(receiver, key, receiver, out, false);
}

int jube_member_get_js(Item target, Item key, Item receiver, Item* out) {
    return jube_member_get_impl(target, key, receiver, out, true);
}

static int jube_member_projected_get_impl(Item receiver, Item key, Item* out,
        bool own_only) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out || !jube_native_alive(receiver)) return 0;
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver, key);
    if (!rec || (own_only && jube_prototype_member_for(rec, receiver))) return 0;
    // projection excludes named hooks and expandos; JS own reflection also
    // excludes published prototype members (D1.3v3).
    return jube_dispatch_get_record(receiver, rec, out);
}

int jube_member_projected_get(Item receiver, Item key, Item* out) {
    return jube_member_projected_get_impl(receiver, key, out, false);
}

int jube_member_native_own_get(Item receiver, Item key, Item* out) {
    return jube_member_projected_get_impl(receiver, key, out, true);
}

static int jube_member_set_impl(Item target, Item key, Item value, Item receiver,
        Item* out, bool js_surface) {
    JubeTypeRecord* trec = jube_record_for(target);
    if (!trec || !out) return 0;
    RootFrame roots(5);
    Rooted<Item> target_root(roots, target), key_root(roots, key), value_root(roots, value);
    Rooted<Item> receiver_root(roots, receiver), lookup_root(roots, ItemNull);
    if (!jube_native_alive(target)) {
        *out = js_surface ? (Item){.item = ITEM_FALSE} : jube_undefined_item();
        return 1;
    }
    JubeMemberRecord* rec = jube_resolve_member(trec, target, key, js_surface);
    if (js_surface && !rec) {
        lookup_root.set(jube_member_lookup_object(target_root.get(), key_root.get()));
        // inherited setters and read-only data descriptors precede the legacy
        // open-name fallback, which otherwise writes an expando unconditionally.
        if (get_type_id(lookup_root.get()) == LMD_TYPE_MAP) {
            Item present = js_has_property(lookup_root.get(),
                js_property_lane_for_canonical_key(key_root.get()), key_root.get());
            if (item_is_error(present)) {*out = present; return 1;}
            if (it2b(present)) {
                *out = js_set_completion_with_key(lookup_root.get(), key_root.get(),
                    value_root.get(), receiver_root.get());
                return 1;
            }
        }
    }
    if (!js_surface || !jube_prototype_member_for(rec, target)) {
        if (rec && jube_dispatch_set_record(target_root.get(), trec, rec, value_root.get(), out, js_surface))
            return 1;
        if ((!js_surface || jube_js_named(trec)) && trec->binding && trec->binding->named_set &&
                trec->binding->named_set(target_root.get(), key_root.get(), value_root.get(), out)) {
            if (js_surface && !item_is_error(*out)) *out = (Item){.item = ITEM_TRUE};
            return 1;
        }
    }
    if (js_surface) {
        lookup_root.set(jube_member_lookup_object(target_root.get(), key_root.get()));
        if (get_type_id(lookup_root.get()) != LMD_TYPE_MAP)
            lookup_root.set(jube_expando_object(target_root.get(), true));
        // [[Set]] completion is independent of the assigned value, including false/undefined.
        *out = js_set_completion_with_key(lookup_root.get(), key_root.get(),
            value_root.get(), receiver_root.get());
        return 1;
    }
    lookup_root.set(jube_expando_object(target_root.get(), true));
    if (get_type_id(lookup_root.get()) == LMD_TYPE_MAP) {
        *out = jube_internal_host_api()->value->property_set(lookup_root.get(), key_root.get(), value_root.get());
        return 1;
    }
    *out = value_root.get();
    return 1;
}

int jube_member_set(Item receiver, Item key, Item value, Item* out) {
    return jube_member_set_impl(receiver, key, value, receiver, out, false);
}

int jube_member_set_js(Item target, Item key, Item value, Item receiver, Item* out) {
    return jube_member_set_impl(target, key, value, receiver, out, true);
}

int jube_member_define_own(Item receiver, Item key, Item descriptor, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    if (!jube_native_alive(receiver)) {
        *out = jube_undefined_item();
        return 1;
    }
    RootFrame roots(4);
    Rooted<Item> receiver_root(roots, receiver);
    Rooted<Item> key_root(roots, key);
    Rooted<Item> descriptor_root(roots, descriptor);
    Rooted<Item> expando_root(roots, ItemNull);
    int64_t index = -1;
    if ((trec->type->flags & JUBE_TYPE_INDEXED_READONLY) &&
            jube_js_indexed(trec) &&
            !trec->binding->indexed_set && jube_index_from_key(key_root.get(), &index)) {
        // getter-only WebIDL indices reject definitions even outside current bounds.
        // descriptor conversion still precedes that rejection (D6.2.2v2, D5.3.3).
        JsPropertyDescriptor converted = {};
        Item status = js_descriptor_from_object(descriptor_root.get(), &converted);
        *out = item_is_error(status) ? status : (Item){.item = ITEM_FALSE};
        return 1;
    }
    expando_root.set(jube_expando_object(receiver_root.get(), true));
    if (get_type_id(expando_root.get()) != LMD_TYPE_MAP) return 0;
    // DOM and other virtual hosts keep arbitrary own fields in this map; use
    // DefineOwnProperty so descriptor attributes remain observable.
    Item result = js_object_define_property(expando_root.get(), key_root.get(),
        descriptor_root.get());
    if (item_is_error(result)) {
        *out = result;
        return 1;
    }
    *out = (Item){.item = ITEM_TRUE};
    return 1;
}

int jube_member_has(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    RootFrame roots(3);
    Rooted<Item> receiver_root(roots, receiver), key_root(roots, key), prototype_root(roots, ItemNull);
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver_root.get(), key_root.get());
    bool uses_prototype = jube_prototype_member_for(rec, receiver);
    bool present = rec && !uses_prototype;
    if (!present && jube_js_indexed(trec)) {
        int64_t index = -1;
        present = jube_index_from_key(key_root.get(), &index) &&
            index < jube_indexed_length(receiver_root.get(), trec);
    }
    if (!present && !uses_prototype && trec->binding && trec->binding->object_has &&
            jube_native_alive(receiver_root.get()) &&
            trec->binding->object_has(receiver_root.get(), key_root.get(), out)) {
        return 1;
    }
    if (!present && !uses_prototype && jube_js_named(trec) && trec->binding && trec->binding->named_has &&
            jube_native_alive(receiver_root.get()) &&
            trec->binding->named_has(receiver_root.get(), key_root.get(), out)) {
        return 1;
    }
    if (!present && jube_native_alive(receiver_root.get())) {
        Item expando = jube_expando_object(receiver_root.get(), false);
        if (get_type_id(expando) == LMD_TYPE_MAP) {
            present = jube_expando_has(expando, key_root.get());
        }
    }
    if (!present && js_active_runtime_state && js_input && js_input->pool) {
        Item prototype = ItemNull;
        jube_member_prototype(receiver_root.get(), &prototype);
        prototype_root.set(prototype);
        if (get_type_id(prototype_root.get()) == LMD_TYPE_MAP) {
            *out = js_in(key_root.get(), prototype_root.get());
            return 1;
        }
    } else if (rec && jube_prototype_member_for(rec, receiver)) present = true;
    *out = (Item){.item = b2it(present)};
    return 1;
}

int jube_member_delete(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    RootFrame roots(2);
    Rooted<Item> receiver_root(roots, receiver), key_root(roots, key);
    int64_t index = -1;
    if (jube_js_indexed(trec) && jube_index_from_key(key_root.get(), &index) &&
            index < jube_indexed_length(receiver_root.get(), trec)) {
        *out = (Item){.item = ITEM_FALSE};
        return 1;
    }
    if (JubeMemberRecord* rec = jube_resolve_member(trec, receiver_root.get(), key_root.get());
            rec && !jube_prototype_member_for(rec, receiver)) {
        *out = (Item){.item = b2it(false)};
        return 1;
    }
    if (trec->binding && trec->binding->object_delete && jube_native_alive(receiver_root.get()) &&
            trec->binding->object_delete(receiver_root.get(), key_root.get(), out)) {
        return 1;
    }
    // open-name members (CSS properties on style objects) refuse deletion,
    // matching the projected-property non-configurable contract
    if (jube_js_named(trec) && trec->binding && trec->binding->named_has &&
            jube_native_alive(receiver_root.get())) {
        Item present = ItemNull;
        if (trec->binding->named_has(receiver_root.get(), key_root.get(), &present) &&
                present.item == b2it(true)) {
            *out = (Item){.item = b2it(false)};
            return 1;
        }
    }
    if (jube_native_alive(receiver_root.get())) {
        Item expando = jube_expando_object(receiver_root.get(), false);
        if (get_type_id(expando) == LMD_TYPE_MAP) {
            *out = jube_internal_host_api()->script->reflect_delete_property(expando, key_root.get());
            return 1;
        }
    }
    *out = (Item){.item = b2it(true)};
    return 1;
}

static Item jube_make_data_descriptor(Item value, bool writable,
        bool configurable) {
    const JubeHostAPI* host = jube_internal_host_api();
    RootFrame roots(2);
    Rooted<Item> value_root(roots, value);
    Rooted<Item> descriptor_root(roots, host->value->new_object());
    // D5.1.1: descriptor shape transitions may collect, so both the object
    // under construction and its potentially managed value need exact roots.
    host->value->property_set(descriptor_root.get(), jube_name_item("value"),
                              value_root.get());
    host->value->property_set(descriptor_root.get(), jube_name_item("writable"),
                              (Item){.item = b2it(writable)});
    host->value->property_set(descriptor_root.get(), jube_name_item("enumerable"),
                              (Item){.item = b2it(true)});
    host->value->property_set(descriptor_root.get(), jube_name_item("configurable"),
                              (Item){.item = b2it(configurable)});
    return descriptor_root.get();
}

int jube_member_descriptor(Item receiver, Item key, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    RootFrame roots(4);
    Rooted<Item> receiver_root(roots, receiver);
    Rooted<Item> key_root(roots, key);
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> expando_root(roots, ItemNull);
    if (trec->binding && trec->binding->object_descriptor && jube_native_alive(receiver) &&
            trec->binding->object_descriptor(receiver_root.get(), key_root.get(), out)) {
        return 1;
    }
    if (jube_js_indexed(trec) &&
            jube_native_alive(receiver_root.get())) {
        int64_t index = -1;
        if (jube_index_from_key(key_root.get(), &index) &&
                index < jube_indexed_length(receiver_root.get(), trec)) {
            Item indexed_value = ItemNull;
            if (!trec->binding->indexed_get(receiver_root.get(), index,
                    &indexed_value)) {
                *out = jube_undefined_item();
                return 1;
            }
            value_root.set(indexed_value);
            *out = jube_make_data_descriptor(value_root.get(), false, true);
            return 1;
        }
    }
    JubeMemberRecord* rec = jube_resolve_member(trec, receiver_root.get(), key_root.get());
    if (rec && !jube_prototype_member_for(rec, receiver) && rec->kind != JUBE_MEMBER_METHOD &&
            jube_native_alive(receiver_root.get())) {
        Item member_value = jube_undefined_item();
        jube_member_get(receiver_root.get(), key_root.get(), &member_value);
        value_root.set(member_value);
        *out = jube_make_data_descriptor(value_root.get(), !rec->readonly, false);
        return 1;
    }
    if (jube_native_alive(receiver_root.get())) {
        expando_root.set(jube_expando_object(receiver_root.get(), false));
        if (get_type_id(expando_root.get()) == LMD_TYPE_MAP) {
            *out = js_object_get_own_property_descriptor(expando_root.get(), key_root.get());
            return 1;
        }
    }
    *out = jube_undefined_item();
    return 1;
}

static void jube_append_expando_keys(const JubeHostAPI* host, Item keys, Item expando,
                                     Rooted<Item>* expando_keys, Rooted<Item>* name);

int jube_member_own_keys(Item receiver, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    RootFrame roots(5);
    Rooted<Item> rooted_receiver(roots, receiver);
    Rooted<Item> rooted_keys(roots, ItemNull);
    Rooted<Item> rooted_name(roots, ItemNull);
    Rooted<Item> rooted_expando(roots, ItemNull);
    Rooted<Item> rooted_expando_keys(roots, ItemNull);
    if (trec->binding && trec->binding->object_own_keys && jube_native_alive(receiver) &&
            trec->binding->object_own_keys(rooted_receiver.get(), out)) {
        return 1;
    }
    const JubeHostAPI* host = jube_internal_host_api();
    rooted_keys.set(host->value->array_new(0));
    if (jube_js_indexed(trec) &&
            jube_native_alive(rooted_receiver.get())) {
        int64_t count = jube_indexed_length(rooted_receiver.get(), trec);
        for (int64_t i = 0; i < count; i++) {
            char index_name[32];
            snprintf(index_name, sizeof(index_name), "%lld", (long long)i);
            rooted_name.set(jube_name_item(index_name));
            host->value->array_push(rooted_keys.get(), rooted_name.get());
        }
    }
    for (int i = 0; i < trec->member_count; i++) {
        JubeMemberRecord* rec = &trec->members[i];
        if (!rec->enumerable) continue;
        rooted_name.set(jube_name_item(rec->camel_name));
        host->value->array_push(rooted_keys.get(), rooted_name.get());
    }
    if (jube_native_alive(rooted_receiver.get())) {
        rooted_expando.set(jube_expando_object(rooted_receiver.get(), false));
        jube_append_expando_keys(host, rooted_keys.get(), rooted_expando.get(),
            &rooted_expando_keys, &rooted_name);
    }
    *out = rooted_keys.get();
    return 1;
}

static bool jube_array_has_string_key(Item keys, const char* chars) {
    if (!chars || get_type_id(keys) != LMD_TYPE_ARRAY || !keys.array) return false;
    size_t len = strlen(chars);
    for (int64_t i = 0; i < keys.array->length; i++) {
        Item existing = keys.array->items[i];
        if (get_type_id(existing) != LMD_TYPE_STRING) continue;
        String* str = it2s(existing);
        if (str && str->len == len && memcmp(str->chars, chars, len) == 0) return true;
    }
    return false;
}

static void jube_append_expando_keys(const JubeHostAPI* host, Item keys, Item expando,
                                     Rooted<Item>* expando_keys, Rooted<Item>* name) {
    if (get_type_id(expando) != LMD_TYPE_MAP) return;
    expando_keys->set(host->script->reflect_own_keys(expando));
    if (get_type_id(expando_keys->get()) != LMD_TYPE_ARRAY || !expando_keys->get().array) return;
    for (int64_t i = 0; i < expando_keys->get().array->length; i++) {
        // reload the source array through its root because array_push may collect.
        Array* arr = expando_keys->get().array;
        name->set(arr->items[i]);
        host->value->array_push(keys, name->get());
    }
}

int jube_member_projection_keys(Item receiver, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    RootFrame roots(5);
    Rooted<Item> rooted_receiver(roots, receiver);
    Rooted<Item> rooted_keys(roots, ItemNull);
    Rooted<Item> rooted_name(roots, ItemNull);
    Rooted<Item> rooted_expando(roots, ItemNull);
    Rooted<Item> rooted_expando_keys(roots, ItemNull);
    const JubeHostAPI* host = jube_internal_host_api();
    rooted_keys.set(host->value->array_new(0));
    for (int i = 0; i < trec->member_count; i++) {
        JubeMemberRecord* rec = &trec->members[i];
        if (rec->kind != JUBE_MEMBER_FIELD) continue;
        if (jube_array_has_string_key(rooted_keys.get(), rec->snake_name)) continue;
        // Lambda projection iteration exposes declared snake_case fields; JS
        // own-key enumeration remains WebIDL/camelCase through object_own_keys.
        rooted_name.set(jube_name_item(rec->snake_name));
        host->value->array_push(rooted_keys.get(), rooted_name.get());
    }
    if (jube_native_alive(rooted_receiver.get())) {
        rooted_expando.set(jube_expando_object(rooted_receiver.get(), false));
        jube_append_expando_keys(host, rooted_keys.get(), rooted_expando.get(),
            &rooted_expando_keys, &rooted_name);
    }
    *out = rooted_keys.get();
    return 1;
}

int jube_member_prototype(Item receiver, Item* out) {
    JubeTypeRecord* trec = jube_record_for(receiver);
    if (!trec || !out) return 0;
    if (trec->binding && trec->binding->object_prototype && jube_native_alive(receiver) &&
            trec->binding->object_prototype(receiver, out)) {
        // DOM nodes need receiver-specific Element/Text/Document prototypes;
        // a static prototype_seed cannot preserve that WebIDL identity.
        return 1;
    }
    *out = jube_type_prototype_for(trec);
    return 1;
}

// ============================================================================
// Interface compilation
// ============================================================================

typedef struct JubeParsedMember {
    char* name;
    bool is_method;
    int arity;
    bool can_raise;
    char* result_type_name;
    bool has_default;
    bool default_is_str;
    int64_t default_int;
    char* default_str;
} JubeParsedMember;

typedef struct JubeParsedType {
    char* name;
    char* base_name;
    lam::ArrayList<JubeParsedMember>* members;
    int member_count;
} JubeParsedType;

static const JubeTypeDef* jube_module_type_by_name(const JubeModuleDef* module,
                                                   const char* name);

static void jube_member_record_release_owned(JubeMemberRecord* record) {
    if (!record) return;
    if (record->snake_name) free(record->snake_name);
    if (record->camel_name) free(record->camel_name);
    if (record->const_str) free(record->const_str);
}

static void jube_member_record_init(JubeMemberRecord* record,
                                    const JubeParsedMember* parsed,
                                    const JubeMemberBind* bind,
                                    const JubeModuleDef* module) {
    if (!record || !parsed) return;
    jube_member_record_release_owned(record);
    memset(record, 0, sizeof(*record));
    record->bind = bind;
    record->snake_name = str_dup(parsed->name, strlen(parsed->name));
    record->camel_name = (bind && bind->js_name)
        ? str_dup(bind->js_name, strlen(bind->js_name))
        : jube_derive_camel(record->snake_name);
    if (parsed->is_method) {
        record->kind = JUBE_MEMBER_METHOD;
        record->arity = parsed->arity;
        record->can_raise = parsed->can_raise;
        record->result_type = parsed->result_type_name
            ? jube_module_type_by_name(module, parsed->result_type_name) : NULL;
        record->readonly = true;
    } else if (!bind) {
        record->kind = JUBE_MEMBER_CONST;
        record->readonly = true;
        record->const_int = parsed->default_int;
        record->const_is_str = parsed->default_is_str;
        record->const_str = parsed->default_str
            ? str_dup(parsed->default_str, strlen(parsed->default_str)) : NULL;
        record->result_type = parsed->result_type_name
            ? jube_module_type_by_name(module, parsed->result_type_name) : NULL;
    } else {
        record->kind = JUBE_MEMBER_FIELD;
        record->readonly = !bind->set && !bind->reflect_attr;
        record->result_type = parsed->result_type_name
            ? jube_module_type_by_name(module, parsed->result_type_name) : NULL;
    }
    record->enumerable = record->kind == JUBE_MEMBER_FIELD &&
        !(bind && (bind->flags & (JUBE_MEMBER_NON_ENUMERABLE | JUBE_MEMBER_PROTOTYPE)));
}

// count fn_param children and detect '^' in the return type of a fn_type node
// With the external type-pattern scanner the attr's type is ONE opaque token
// (`type_pattern_token`), so fn-typed members are recognized and parsed from
// the token TEXT: `fn(a: T, b: U) R^E`.
static bool jube_text_is_fn_type(const char* text) {
    if (!text) return false;
    text = str_skip_line_space(text);
    if (text[0] != 'f' || text[1] != 'n') return false;
    const char* p = text + 2;
    p = str_skip_line_space(p);
    return *p == '(' || *p == '\0';
}

static void jube_parse_fn_type_text(char* text, int* arity, bool* can_raise,
                                    char** result_type_name) {
    *arity = 0;
    *can_raise = false;
    if (result_type_name) *result_type_name = NULL;
    char* p = strchr(text, '(');
    char* close = NULL;
    if (p) {
        int depth = 0;
        for (char* q = p; *q; q++) {
            if (*q == '(') depth++;
            else if (*q == ')') { depth--; if (!depth) { close = q; break; } }
            else if (*q == ',' && depth == 1) (*arity)++;
        }
        // one param when the parens are non-empty and hold no top-level comma
        if (close) {
            for (char* q = p + 1; q < close; q++) {
                if (*q != ' ' && *q != '\t') { (*arity)++; break; }
            }
        }
    }
    char* rest = close ? close + 1 : text;
    char* raise_marker = strchr(rest, '^');
    if (raise_marker) {
        *can_raise = true;
        *raise_marker = '\0';
    }
    const char* result_start = str_skip_line_space(rest);
    if (result_type_name && *result_start) {
        // trim trailing spaces
        const char* end = result_start + strlen(result_start);
        while (end > result_start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        *result_type_name = str_dup(result_start, (size_t)(end - result_start));
    }
    free(text);
}

static void jube_free_parsed_members(JubeParsedMember* members, int count) {
    for (int i = 0; i < count; i++) {
        if (members[i].name) free(members[i].name);
        if (members[i].default_str) free(members[i].default_str);
        if (members[i].result_type_name) free(members[i].result_type_name);
        members[i].name = NULL;
        members[i].default_str = NULL;
        members[i].result_type_name = NULL;
    }
}

static void jube_release_parsed_type(JubeParsedType* type) {
    if (!type) return;
    if (type->members) {
        jube_free_parsed_members(type->members->data(), type->member_count);
        delete type->members;
        type->members = NULL;
    }
    type->member_count = 0;
    if (type->name) free(type->name);
    if (type->base_name) free(type->base_name);
    type->name = NULL;
    type->base_name = NULL;
}

static int jube_count_binds(const JubeTypeBinding* binding,
                            const char* name) {
    if (!binding || !binding->members || !name) return 0;
    int count = 0;
    for (int32_t i = 0; i < binding->member_count; i++) {
        if (binding->members[i].name &&
                strcmp(binding->members[i].name, name) == 0) {
            count++;
        }
    }
    return count;
}

static const JubeTypeBinding* jube_find_type_binding(const JubeTypeBinding* bindings,
                                                     int32_t count, const char* type_name) {
    for (int32_t i = 0; i < count; i++) {
        if (bindings[i].type_name && strcmp(bindings[i].type_name, type_name) == 0) {
            return &bindings[i];
        }
    }
    return NULL;
}

static const JubeTypeDef* jube_module_type_by_name(const JubeModuleDef* module,
                                                   const char* name) {
    for (int32_t i = 0; i < module->type_count; i++) {
        if (module->types[i].name && strcmp(module->types[i].name, name) == 0) {
            return &module->types[i];
        }
    }
    return NULL;
}

static bool jube_index_insert(HashMap* index, const char* chars,
                              JubeMemberRecord* rec) {
    JubeMemberIndexEntry probe = {chars, (uint32_t)strlen(chars), NULL};
    JubeMemberIndexEntry* existing = JubeMemberIndex::get(index, probe);
    if (existing) {
        // duplicate spellings are rejected by declaration validation; retain
        // the first index entry if a malformed binding table slips through.
        return existing->rec == rec;
    }
    probe.rec = rec;
    JubeMemberIndex::set(index, probe);
    return !JubeMemberIndex::oom(index);
}

// find a previously compiled base type by declared name within the same module
static JubeTypeRecord* jube_find_compiled_base(const JubeModuleDef* module,
                                               const char* base_name) {
    const JubeTypeDef* base_type = jube_module_type_by_name(module, base_name);
    if (!base_type) return NULL;
    return jube_record_for_type((const void*)base_type);
}

static void jube_interface_release_record(JubeTypeRecord* trec, bool unregister_roots);

static int jube_compile_type(const JubeModuleDef* module,
                             JubeParsedType* parsed_type,
                             const JubeTypeBinding* bindings,
                             int32_t binding_count) {
    if (!parsed_type || !parsed_type->name) {
        log_error("JUBE_IFACE: module '%s' object type missing a name", module->name);
        return -1;
    }
    const char* type_name = parsed_type->name;

    const JubeTypeDef* host_brand = NULL;
    const JubeTypeBinding* binding =
        jube_find_type_binding(bindings, binding_count, type_name);
    if (!binding) {
        log_error("JUBE_IFACE: module '%s' declares type '%s' with no binding table",
                  module->name, type_name);
        return -1;
    }
    host_brand = binding->host_brand ? binding->host_brand
                                     : jube_module_type_by_name(module, type_name);
    if (!host_brand) {
        log_error("JUBE_IFACE: module '%s' type '%s' has no host brand JubeTypeDef",
                  module->name, type_name);
        return -1;
    }

    // inherited members flatten in first so derived declarations can override
    JubeTypeRecord* base_rec = NULL;
    if (parsed_type->base_name) {
        const char* base_name = parsed_type->base_name;
        base_rec = jube_find_compiled_base(module, base_name);
        if (!base_rec) {
            log_error("JUBE_IFACE: module '%s' type '%s' inherits unknown/uncompiled "
                      "base '%s' (declare bases before derived types)",
                      module->name, type_name, base_name ? base_name : "(null)");
            return -1;
        }
    }

    JubeParsedMember* parsed = parsed_type->members ? parsed_type->members->data() : NULL;
    int parsed_count = parsed_type->member_count;

    // cross-check declared members against bindings before compiling records
    for (int i = 0; i < parsed_count; i++) {
        int matching_binds = jube_count_binds(binding, parsed[i].name);
        if (matching_binds == 0) {
            if (parsed[i].is_method || !parsed[i].has_default) {
                log_error("JUBE_IFACE: type '%s' member '%s' is declared but unbound "
                          "(only default-valued constants may omit a binding)",
                          type_name, parsed[i].name);
                jube_free_parsed_members(parsed, parsed_count);
                return -1;
            }
            continue;
        }
        for (int32_t j = 0; j < binding->member_count; j++) {
            const JubeMemberBind* bind = &binding->members[j];
            if (!bind->name || strcmp(bind->name, parsed[i].name) != 0) continue;
            if ((bind->flags & JUBE_MEMBER_KEYED_ACCESSOR) &&
                    (!bind->reflect_attr || !binding->named_get || !binding->named_set)) {
                log_error("JUBE_IFACE: keyed accessor '%s.%s' lacks its key or named adapters",
                    type_name, parsed[i].name);
                jube_free_parsed_members(parsed, parsed_count);
                return -1;
            }
            // DS13: a method is implemented either by a call handler or by a
            // catalog row slot; requiring `call` would reject every BIND_ROW.
            if (parsed[i].is_method && !bind->call && !bind->row_index) {
                log_error("JUBE_IFACE: type '%s' method '%s' binding lacks a call handler",
                          type_name, parsed[i].name);
                jube_free_parsed_members(parsed, parsed_count);
                return -1;
            }
            if (!parsed[i].is_method && !bind->get && !bind->reflect_attr) {
                log_error("JUBE_IFACE: type '%s' field '%s' binding lacks a getter",
                          type_name, parsed[i].name);
                jube_free_parsed_members(parsed, parsed_count);
                return -1;
            }
        }
    }
    for (int32_t i = 0; i < binding->member_count; i++) {
        const char* bind_name = binding->members[i].name;
        bool declared = false;
        for (int j = 0; j < parsed_count && !declared; j++) {
            declared = strcmp(parsed[j].name, bind_name) == 0;
        }
        if (!declared && base_rec) {
            for (int j = 0; j < base_rec->member_count && !declared; j++) {
                declared = strcmp(base_rec->members[j].snake_name, bind_name) == 0;
            }
        }
        if (!declared) {
            log_error("JUBE_IFACE: type '%s' binds undeclared member '%s'",
                      type_name, bind_name);
            jube_free_parsed_members(parsed, parsed_count);
            return -1;
        }
    }

    if (!s_type_records.reserve(s_type_record_count+1)) {
        log_error("JUBE_IFACE: type record allocation failed at '%s'", type_name);
        jube_free_parsed_members(parsed, parsed_count);
        return -1;
    }
    if (!s_type_index) {
        s_type_index = JubeTypeIndex::create(16);
        if (!s_type_index) {
            log_error("JUBE_IFACE: failed to allocate type index for '%s'", type_name);
            jube_free_parsed_members(parsed, parsed_count);
            return -1;
        }
    }

    int base_count = base_rec ? base_rec->member_count : 0;
    int declared_record_count = 0;
    for (int i = 0; i < parsed_count; i++) {
        int matching_binds = jube_count_binds(binding, parsed[i].name);
        bool overrides_base = false;
        if (base_rec) {
            for (int j = 0; j < base_count; j++) {
                if (strcmp(base_rec->members[j].snake_name, parsed[i].name) == 0) {
                    overrides_base = true;
                    break;
                }
            }
        }
        if (!overrides_base) declared_record_count += matching_binds > 0 ? matching_binds : 1;
    }
    int total = base_count + declared_record_count;
    // Interface records survive runtime teardown and are released after the
    // tracker can change phase, so keep their C ownership independent of a
    // particular JS heap or memtrack mode.
    JubeTypeRecord* trec = (JubeTypeRecord*)calloc(1, sizeof(JubeTypeRecord));
    JubeMemberRecord* records = (JubeMemberRecord*)calloc(
        (size_t)(total > 0 ? total : 1), sizeof(JubeMemberRecord));
    if (!trec || !records) {
        if (trec) free(trec);
        if (records) free(records);
        jube_free_parsed_members(parsed, parsed_count);
        return -1;
    }

    int out_count = 0;
    for (int i = 0; i < base_count; i++) {
        // derived types re-record inherited members (records carry per-type
        // caches like method_fn, so they cannot be shared across brands)
        JubeMemberRecord* src = &base_rec->members[i];
        JubeMemberRecord* dst = &records[out_count++];
        dst->declaring_type = src->declaring_type;
        dst->canonical_record = src->canonical_record;
        dst->bind = src->bind;
        dst->snake_name = str_dup(src->snake_name, strlen(src->snake_name));
        dst->camel_name = str_dup(src->camel_name, strlen(src->camel_name));
        dst->kind = src->kind;
        dst->readonly = src->readonly;
        dst->enumerable = src->enumerable;
        dst->can_raise = src->can_raise;
        dst->arity = src->arity;
        dst->result_type = src->result_type;
        dst->const_int = src->const_int;
        dst->const_is_str = src->const_is_str;
        dst->const_str = src->const_str
            ? str_dup(src->const_str, strlen(src->const_str)) : NULL;
    }

    // release strips log_info(), so keep diagnostic counters out of NDEBUG builds.
#ifndef NDEBUG
    int method_count = 0, const_count = 0;
#endif
    for (int i = 0; i < parsed_count; i++) {
        int matching_binds = jube_count_binds(binding, parsed[i].name);
        int base_ordinal = -1;
        if (base_rec) {
            for (int j = 0; j < base_count; j++) {
                if (strcmp(base_rec->members[j].snake_name, parsed[i].name) == 0) {
                    base_ordinal = j;
                    break;
                }
            }
        }
        if (base_ordinal >= 0) {
            // Derived interfaces override an inherited ordinal in place. This
            // preserves the inherited prefix while replacing the base guard
            // row with the concrete subtype's unambiguous operation.
            if (matching_binds > 1) {
                log_error("JUBE_IFACE: derived type '%s' overrides member '%s' "
                          "with multiple bindings", type_name, parsed[i].name);
                free(records);
                free(trec);
                jube_free_parsed_members(parsed, parsed_count);
                return -1;
            }
            const JubeMemberBind* override_bind = NULL;
            for (int32_t j = 0; j < binding->member_count; j++) {
                if (binding->members[j].name &&
                        strcmp(binding->members[j].name, parsed[i].name) == 0) {
                    override_bind = &binding->members[j];
                    break;
                }
            }
            jube_member_record_init(&records[base_ordinal], &parsed[i],
                                    override_bind, module);
            records[base_ordinal].declaring_type = trec;
            records[base_ordinal].canonical_record = &records[base_ordinal];
            continue;
        }
        int variants = matching_binds > 0 ? matching_binds : 1;
        int variant_index = 0;
        for (int32_t j = 0; j < binding->member_count ||
                (matching_binds == 0 && variant_index == 0); j++) {
            const JubeMemberBind* bind = matching_binds > 0
                ? &binding->members[j] : NULL;
            if (bind && (!bind->name || strcmp(bind->name, parsed[i].name) != 0)) {
                continue;
            }
            JubeMemberRecord* rec = &records[out_count++];
            jube_member_record_init(rec, &parsed[i], bind, module);
            rec->declaring_type = trec;
            rec->canonical_record = rec;
            if (rec->kind == JUBE_MEMBER_METHOD) {
#ifndef NDEBUG
                method_count++;
#endif
#ifndef NDEBUG
            } else if (rec->kind == JUBE_MEMBER_CONST) {
                const_count++;
#endif
            }
            variant_index++;
            if (variant_index >= variants) break;
        }
    }

#ifndef NDEBUG
    // H4 requires derived ordinals to retain the complete inherited prefix;
    // catch a declaration-order regression before publishing the type record.
    if (base_rec) {
        for (int i = 0; i < base_count; i++) {
            if (strcmp(records[i].snake_name, base_rec->members[i].snake_name) != 0) {
                log_error("JUBE_IFACE: type '%s' broke inherited ordinal prefix at %d",
                          type_name, i);
                for (int j = 0; j < out_count; j++) {
                    jube_member_record_release_owned(&records[j]);
                }
                free(records);
                free(trec);
                jube_free_parsed_members(parsed, parsed_count);
                return -1;
            }
        }
    }
#endif

    HashMap* index = JubeMemberIndex::create(16);
    for (int i = 0; i < out_count; i++) {
        JubeMemberRecord* rec = &records[i];
        jube_index_insert(index, rec->snake_name, rec);
        if (strcmp(rec->snake_name, rec->camel_name) != 0) {
            jube_index_insert(index, rec->camel_name, rec);
        }
    }

    trec->type = host_brand;
    trec->binding = binding;
    trec->base_record = base_rec;
    trec->type_slot = s_type_record_count;
    trec->family_root_slot = base_rec ? base_rec->family_root_slot : trec->type_slot;
    trec->members = records;
    trec->member_count = out_count;
    trec->index = index;
    trec->prototype = ItemNull;
    JubeTypeIndexEntry type_entry = {host_brand, trec};
    JubeTypeIndex::set(s_type_index, type_entry);
    if (JubeTypeIndex::oom(s_type_index)) {
        jube_interface_release_record(trec, false);
        jube_free_parsed_members(parsed, parsed_count);
        return -1;
    }
    s_type_records.push_back(trec);s_type_record_count++;

#ifndef NDEBUG
    log_info("JUBE_REG: type %s.%s members=%d (methods=%d, consts=%d, inherited=%d)",
             module->name, type_name, out_count, method_count, const_count, base_count);
#endif
    jube_free_parsed_members(parsed, parsed_count);
    return 0;
}

typedef enum JubeDirectValueKind {
    JUBE_DIRECT_VALUE_GENERIC = 1,
    JUBE_DIRECT_VALUE_ATOM,
    JUBE_DIRECT_VALUE_TYPE_SLOT,
} JubeDirectValueKind;

typedef struct JubeDirectValue {
    JubeDirectValueKind kind;
    LambdaTokenKind token_kind;
    SourceSpan span;
    struct JubeDirectValue* next;
} JubeDirectValue;

typedef struct JubeDirectSink {
    const char* source;
    size_t source_length;
    const JubeModuleDef* module;
    const JubeTypeBinding* bindings;
    int32_t binding_count;
    JubeParsedType current_type;
    JubeDirectValue* values;
    int compiled_count;
    bool failed;
    const char* failure;
} JubeDirectSink;

static JubeDirectValue* jube_direct_value_from_parse(LambdaParseValue value) {
    return (JubeDirectValue*)(uintptr_t)value;
}

static char* jube_direct_span_text(const JubeDirectSink* sink,
                                   SourceSpan span) {
    if (!sink || !sink->source || span.end_byte < span.start_byte ||
            span.end_byte > sink->source_length) return NULL;
    return str_dup(sink->source + span.start_byte,
                        span.end_byte - span.start_byte);
}

static JubeDirectValue* jube_direct_new_value(JubeDirectSink* sink,
                                               JubeDirectValueKind kind,
                                               LambdaTokenKind token_kind,
                                               SourceSpan span) {
    JubeDirectValue* value = (JubeDirectValue*)calloc(1, sizeof(JubeDirectValue));
    if (!value) {
        sink->failed = true;
        sink->failure = "out of memory while reducing interface declaration";
        return NULL;
    }
    value->kind = kind;
    value->token_kind = token_kind;
    value->span = span;
    value->next = sink->values;
    sink->values = value;
    return value;
}

static void jube_direct_fail(JubeDirectSink* sink, const char* message) {
    if (!sink->failed) sink->failure = message;
    sink->failed = true;
}

static bool jube_direct_copy_name(const JubeDirectSink* sink, LambdaToken token,
                                  char** out) {
    char* text = jube_direct_span_text(sink, token.span);
    if (!text) return false;
    size_t length = strlen(text);
    if (token.kind == LAMBDA_TOK_SYMBOL && length >= 2 &&
            text[0] == '\'' && text[length - 1] == '\'') {
        char* bare = str_dup(text + 1, length - 2);
        free(text);
        text = bare;
    }
    if (!text) return false;
    *out = text;
    return true;
}

static bool jube_direct_parse_default(const JubeDirectSink* sink,
                                      const JubeDirectValue* value,
                                      JubeParsedMember* member) {
    if (!value || !member || value->kind != JUBE_DIRECT_VALUE_ATOM) return false;
    char* text = jube_direct_span_text(sink, value->span);
    if (!text) return false;
    if (value->token_kind == LAMBDA_TOK_STRING) {
        size_t length = strlen(text);
        if (length < 2 || (text[0] != '"' && text[0] != '\'')) {
            free(text);
            return false;
        }
        member->default_str = str_dup(text + 1, length - 2);
        free(text);
        member->default_is_str = true;
        member->has_default = member->default_str != NULL;
        return member->has_default;
    }
    if (value->token_kind != LAMBDA_TOK_INTEGER) {
        free(text);
        return false;
    }
    char* end = NULL;
    errno = 0;
    member->default_int = strtoll(text, &end, 10);
    bool valid = errno == 0 && end && *end == '\0';
    free(text);
    if (!valid) return false;
    member->default_is_str = false;
    member->has_default = true;
    return true;
}

static LambdaParseValue jube_direct_reduce(void* context,
                                           const LambdaParseReduction* reduction) {
    JubeDirectSink* sink = (JubeDirectSink*)context;
    if (!sink || !reduction) return 0;
    if (sink->failed) return (LambdaParseValue)1;

    JubeDirectValueKind value_kind = reduction->kind == LAMBDA_REDUCE_ATOM
        ? JUBE_DIRECT_VALUE_ATOM
        : (reduction->kind == LAMBDA_REDUCE_TYPE_SLOT
            ? JUBE_DIRECT_VALUE_TYPE_SLOT : JUBE_DIRECT_VALUE_GENERIC);
    JubeDirectValue* value = jube_direct_new_value(sink, value_kind,
        reduction->detail_token.kind, reduction->span);
    if (!value) return (LambdaParseValue)1;

    if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_BEGIN) {
        if (sink->current_type.name) {
            jube_direct_fail(sink, "nested or unterminated object type in interface declaration");
            return (LambdaParseValue)(uintptr_t)value;
        }
        memset(&sink->current_type, 0, sizeof(sink->current_type));
        if (!jube_direct_copy_name(sink, reduction->secondary_token,
                                   &sink->current_type.name)) {
            jube_direct_fail(sink, "object type name is outside the interface source span");
            return (LambdaParseValue)(uintptr_t)value;
        }
        if (reduction->detail_token.kind != LAMBDA_TOK_EOF &&
                reduction->detail_token.span.end_byte >
                    reduction->detail_token.span.start_byte &&
                !jube_direct_copy_name(sink, reduction->detail_token,
                                       &sink->current_type.base_name)) {
            jube_direct_fail(sink, "object type base name is outside the interface source span");
        }
        return (LambdaParseValue)(uintptr_t)value;
    }

    if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT_FIELD) {
        if (!sink->current_type.name || reduction->child_count < 1) {
            jube_direct_fail(sink, "invalid object field reduction in interface declaration");
            return (LambdaParseValue)(uintptr_t)value;
        }
        JubeDirectValue* type_value = jube_direct_value_from_parse(
            reduction->children[0]);
        if (!type_value || type_value->kind != JUBE_DIRECT_VALUE_TYPE_SLOT) {
            jube_direct_fail(sink, "object field is missing its type slot");
            return (LambdaParseValue)(uintptr_t)value;
        }
        // interface size follows declared metadata, including large property catalogs.
        if (!sink->current_type.members)
            sink->current_type.members = new lam::ArrayList<JubeParsedMember>();
        if (!sink->current_type.members->append(JubeParsedMember{})) {
            jube_direct_fail(sink, "cannot grow object fields in interface declaration");
            return (LambdaParseValue)(uintptr_t)value;
        }
        JubeParsedMember* member = &sink->current_type.members->back();
        sink->current_type.member_count++;
        memset(member, 0, sizeof(*member));
        if (!jube_direct_copy_name(sink, reduction->detail_token, &member->name)) {
            jube_direct_fail(sink, "object field name is outside the interface source span");
            return (LambdaParseValue)(uintptr_t)value;
        }
        char* type_text = jube_direct_span_text(sink, type_value->span);
        if (!type_text) {
            jube_direct_fail(sink, "object field type is outside the interface source span");
            return (LambdaParseValue)(uintptr_t)value;
        }
        if (jube_text_is_fn_type(type_text)) {
            member->is_method = true;
            jube_parse_fn_type_text(type_text, &member->arity, &member->can_raise,
                                    &member->result_type_name);
        } else {
            member->result_type_name = type_text;
        }
        if (reduction->child_count > 1) {
            JubeDirectValue* default_value = jube_direct_value_from_parse(
                reduction->children[1]);
            if (!jube_direct_parse_default(sink, default_value, member)) {
                jube_direct_fail(sink,
                    "Jube interface defaults must be integer or string literals");
                return (LambdaParseValue)(uintptr_t)value;
            }
        }
        return (LambdaParseValue)(uintptr_t)value;
    }

    if (reduction->form == LAMBDA_REDUCTION_FORM_TYPE_OBJECT) {
        if (!sink->current_type.name) {
            jube_direct_fail(sink, "object type declaration has no begin reduction");
            return (LambdaParseValue)(uintptr_t)value;
        }
        int rc = jube_compile_type(sink->module, &sink->current_type,
                                   sink->bindings, sink->binding_count);
        if (rc == 0) sink->compiled_count++;
        else sink->failed = true;
        jube_release_parsed_type(&sink->current_type);
        return (LambdaParseValue)(uintptr_t)value;
    }

    return (LambdaParseValue)(uintptr_t)value;
}

static void jube_direct_sink_cleanup(JubeDirectSink* sink) {
    if (!sink) return;
    jube_release_parsed_type(&sink->current_type);
    JubeDirectValue* value = sink->values;
    while (value) {
        JubeDirectValue* next = value->next;
        free(value);
        value = next;
    }
    sink->values = NULL;
}

static void jube_interface_release_record(JubeTypeRecord* trec, bool unregister_roots) {
    if (!trec) return;
    if (unregister_roots) {
        jube_cached_root_drop(&trec->prototype, &trec->prototype_rooted,
                              &trec->prototype_root_owner,
                              &trec->prototype_root_generation);
        for (int j = 0; j < trec->member_count; j++) {
            JubeMemberRecord* rec = &trec->members[j];
            jube_cached_root_drop(&rec->method_fn, &rec->method_fn_rooted,
                                  &rec->method_root_owner,
                                  &rec->method_root_generation);
        }
    }
    // The index stores borrowed member-name pointers; release it before the
    // records so hashmap teardown never hashes already-freed key storage.
    if (trec->index) {
        JubeMemberIndex::destroy(trec->index);
        trec->index = NULL;
    }
    for (int j = 0; j < trec->member_count; j++) {
        JubeMemberRecord* rec = &trec->members[j];
        if (rec->snake_name) free(rec->snake_name);
        if (rec->camel_name) free(rec->camel_name);
        if (rec->const_str) free(rec->const_str);
    }
    if (trec->members) free(trec->members);
    free(trec);
}

static bool jube_interface_record_belongs_to_module(const JubeTypeRecord* trec,
                                                     const JubeModuleDef* module) {
    if (!trec || !module || !module->types || module->type_count <= 0) return false;
    for (int32_t i = 0; i < module->type_count; i++) {
        if (trec->type == &module->types[i]) return true;
    }
    return false;
}

extern "C" void jube_interface_remove_module(const JubeModuleDef* module) {
    for (int i = 0; i < s_type_record_count;) {
        JubeTypeRecord* trec = s_type_records[i];
        if (!jube_interface_record_belongs_to_module(trec, module)) {
            i++;
            continue;
        }
        // Registration rollback must release module-owned compiled records
        // before dlclose, because the type descriptors live in that image.
        if (s_type_index) {
            JubeTypeIndexEntry probe = {trec->type, NULL};
            JubeTypeIndex::erase(s_type_index, probe);
        }
        jube_interface_release_record(trec, true);
        s_type_record_count--;
        s_type_records[i] = s_type_records[s_type_record_count];
        s_type_records.remove(s_type_record_count);
    }
}

// process-exit teardown: frees the compiled records so the memtrack zero-leak
// gate stays honest. Runs after GC-heap destruction, so rooted Items inside
// records are already dead memory — only the C-side allocations are released.
extern "C" void jube_interface_cleanup(void) {
    while (s_type_record_count > 0) {
        int index = --s_type_record_count;
        jube_interface_release_record(s_type_records[index], false);
        s_type_records.remove(index);
    }
    s_type_records.release();
    if (s_type_index) {
        JubeTypeIndex::destroy(s_type_index);
        s_type_index = NULL;
    }
}

extern "C" int jube_compile_module_interface(const JubeModuleDef* module) {
    const char* decl = jube_module_interface_decl(module);
    if (!decl || !*decl) return 0;
    int32_t binding_count = 0;
    const JubeTypeBinding* bindings = jube_module_type_bindings(module, &binding_count);
    if (!bindings || binding_count <= 0) {
        log_error("JUBE_IFACE: module '%s' has an interface_decl but no type bindings",
                  module->name);
        return -1;
    }

    // Jube consumes the first-party reduction stream because release builds
    // intentionally exclude Tree-sitter; keeping the interface compiler on a
    // CST would make module activation fail before Radiant can run scripts.
    JubeDirectSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.source = decl;
    sink.source_length = strlen(decl);
    sink.module = module;
    sink.bindings = bindings;
    sink.binding_count = binding_count;
    LambdaParseSink parse_sink = {jube_direct_reduce};
    LambdaParseError parse_error;
    memset(&parse_error, 0, sizeof(parse_error));
    LambdaParseStatus status = lambda_rd_parse_source(decl, sink.source_length,
        &parse_sink, &sink, NULL, &parse_error);

    int rc = 0;
    if (status != LAMBDA_PARSE_OK) {
        log_error("JUBE_IFACE: module '%s' interface_decl parse failed at byte %u: %s",
                  module->name, parse_error.span.start_byte,
                  parse_error.message ? parse_error.message : "syntax error");
        rc = -1;
    }
    if (sink.failed) {
        log_error("JUBE_IFACE: module '%s' interface reduction failed: %s",
                  module->name, sink.failure ? sink.failure : "unknown error");
        rc = -1;
    }
    // top-level fn/pn signatures stay on JubeFuncDef for now; the interface
    // text carries them for documentation until Phase 4 unifies functions
    if (rc == 0 && sink.compiled_count == 0) {
        log_error("JUBE_IFACE: module '%s' interface_decl declares no object types",
                  module->name);
        rc = -1;
    }

    jube_direct_sink_cleanup(&sink);
    if (rc != 0) {
        // A multi-type declaration may compile a prefix before a later type
        // fails; leave no dispatch records visible from that failed module.
        jube_interface_remove_module(module);
    }
    return rc;
}
