/**
 * Python Class System for Lambda
 *
 * Implements Python class objects, instances, bound methods, MRO, super(),
 * isinstance(), issubclass(), and the built-in exception class hierarchy.
 *
 * Design: classes and instances are both Lambda Map values distinguished by
 * sentinel fields:
 *   Class:    __is_class__ = true, __name__, __bases__, __mro__, plus methods
 *   Instance: __class__ = <class Map Item>, plus instance attributes
 *   Bound method: __is_bound_method__ = true, __func__, __self__
 *   Super proxy:  __is_super__ = true, __type__, __obj__
 */
#include "py_class.h"
#include "py_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdlib>

extern Input* py_input;
extern Item _map_read_field(ShapeEntry* field, void* map_data);
extern Item _map_get(TypeMap* map_type, void* map_data, char* key, bool* is_found);
extern TypeMap EmptyMap;

// py_getattr / py_setattr / py_list_* / py_dict_* / py_eq / py_is_truthy
// are declared via py_runtime.h (included through py_class.h)

// ============================================================================
// Global built-in class objects
// ============================================================================

Item py_object_class = {0};
Item py_type_class = {0};   // built-in 'type' metaclass

// table of built-in exception classes (name → Item)
#define PY_BUILTIN_CLASS_MAX 32
static const char* py_builtin_class_names[PY_BUILTIN_CLASS_MAX];
static Item       py_builtin_class_items[PY_BUILTIN_CLASS_MAX];
static int        py_builtin_class_count = 0;

// ============================================================================
// Internal helpers
// ============================================================================

// Read a string field from a Map by a C string key. Returns ItemNull if absent.
extern "C" Item py_map_get_cstr(Item obj, const char* key) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return ItemNull;
    Map* m = it2map(obj);
    if (!m || !m->type) return ItemNull;
    bool found = false;
    Item result = _map_get((TypeMap*)m->type, m->data, (char*)key, &found);
    return found ? result : ItemNull;
}

// Write a field by C string key. Returns value.
extern "C" Item py_map_set_cstr(Item obj, const char* key, Item value) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return value;
    Map* m = it2map(obj);
    if (!m || !py_input) return value;
    String* k = (String*)heap_create_name(key);
    map_put(m, k, value, py_input);
    return value;
}

// Returns true if a Map has a field set to a truthy bool value.
static bool py_map_flag(Item obj, const char* key) {
    Item v = py_map_get_cstr(obj, key);
    if (get_type_id(v) != LMD_TYPE_BOOL) return false;
    return it2b(v);
}

// Make a boxed bool item.
static inline Item py_bool_item(bool v) {
    return (Item){.item = b2it(v)};
}

// ============================================================================
// Introspection
// ============================================================================

extern "C" bool py_is_class(Item x) {
    return get_type_id(x) == LMD_TYPE_MAP && py_map_flag(x, "__is_class__");
}

extern "C" bool py_is_instance(Item x) {
    if (get_type_id(x) != LMD_TYPE_MAP) return false;
    Item cls = py_map_get_cstr(x, "__class__");
    return get_type_id(cls) != LMD_TYPE_NULL;
}

extern "C" Item py_get_class(Item obj) {
    return py_map_get_cstr(obj, "__class__");
}

// ============================================================================
// MRO lookup
// ============================================================================

extern "C" Item py_mro_lookup(Item cls, Item name) {
    if (get_type_id(cls) != LMD_TYPE_MAP) return ItemNull;
    if (get_type_id(name) != LMD_TYPE_STRING) return ItemNull;

    String* key = it2s(name);
    if (!key) return ItemNull;

    Item mro = py_map_get_cstr(cls, "__mro__");
    if (get_type_id(mro) != LMD_TYPE_ARRAY) {
        // no MRO: just check this class directly
        bool found = false;
        Map* m = it2map(cls);
        if (m && m->type) {
            Item result = _map_get((TypeMap*)m->type, m->data, key->chars, &found);
            if (found) return result;
        }
        return ItemNull;
    }

    // walk MRO list
    Array* mro_arr = it2arr(mro);
    for (int i = 0; i < mro_arr->length; i++) {
        Item entry = mro_arr->items[i];
        if (get_type_id(entry) != LMD_TYPE_MAP) continue;
        Map* m = it2map(entry);
        if (!m || !m->type) continue;
        bool found = false;
        Item result = _map_get((TypeMap*)m->type, m->data, key->chars, &found);
        if (found) return result;
    }
    return ItemNull;
}

// ============================================================================
// C3 MRO computation
// ============================================================================

// Merge step: take the head of the first list if it does not appear in the
// tail of any other list.
static Item c3_head_candidate(Item** lists, int* lengths, int list_count) {
    for (int i = 0; i < list_count; i++) {
        if (lengths[i] == 0) continue;
        Item head = lists[i][0];
        // check it doesn't appear in the tail of any other list
        bool blocked = false;
        for (int j = 0; j < list_count && !blocked; j++) {
            for (int k = 1; k < lengths[j]; k++) {
                if (lists[j][k].item == head.item) {
                    blocked = true;
                    break;
                }
            }
        }
        if (!blocked) return head;
    }
    return ItemNull;  // inconsistent hierarchy
}

static void c3_remove_from_lists(Item** lists, int* lengths, int list_count, Item cls) {
    for (int i = 0; i < list_count; i++) {
        if (lengths[i] > 0 && lists[i][0].item == cls.item) {
            lists[i]++;
            lengths[i]--;
        }
    }
}

extern "C" Item py_compute_mro(Item cls) {
    // result starts with cls itself
    Item result = py_list_new(0);
    py_list_append(result, cls);

    Item bases = py_map_get_cstr(cls, "__bases__");
    if (get_type_id(bases) != LMD_TYPE_ARRAY) return result;
    Array* bases_arr = it2arr(bases);
    if (!bases_arr || bases_arr->length == 0) return result;

    int n = bases_arr->length;

    // build list of arrays to merge: each base's MRO + the bases list itself
    // max lists = n + 1
    int max_lists = n + 1;
    Item** lists     = (Item**)malloc(max_lists * sizeof(Item*));
    int*   lengths   = (int*)  malloc(max_lists * sizeof(int));
    Item** alloced   = (Item**)malloc(max_lists * sizeof(Item*));  // for free

    for (int i = 0; i < n; i++) {
        Item base = bases_arr->items[i];
        Item base_mro = py_map_get_cstr(base, "__mro__");
        if (get_type_id(base_mro) == LMD_TYPE_ARRAY) {
            Array* bm = it2arr(base_mro);
            lists[i]   = bm->items;
            lengths[i] = bm->length;
            alloced[i] = NULL;  // not heap-owned here
        } else {
            // base has no MRO: treat as [base]
            Item* single = (Item*)malloc(sizeof(Item));
            single[0] = base;
            lists[i]   = single;
            lengths[i] = 1;
            alloced[i] = single;
        }
    }
    // last list = bases sequence itself
    lists[n]   = bases_arr->items;
    lengths[n] = bases_arr->length;
    alloced[n] = NULL;

    // C3 merge
    int total_iters = 0;
    while (true) {
        bool all_empty = true;
        for (int i = 0; i <= n; i++) {
            if (lengths[i] > 0) { all_empty = false; break; }
        }
        if (all_empty) break;
        if (++total_iters > 256) {
            log_error("py: TypeError: Cannot create a consistent MRO");
            break;
        }
        Item good = c3_head_candidate(lists, lengths, n + 1);
        if (get_type_id(good) == LMD_TYPE_NULL) {
            log_error("py: TypeError: Inconsistent MRO");
            break;
        }
        py_list_append(result, good);
        c3_remove_from_lists(lists, lengths, n + 1, good);
    }

    for (int i = 0; i <= n; i++) {
        if (alloced[i]) free(alloced[i]);
    }
    free(lists);
    free(lengths);
    free(alloced);

    return result;
}

// ============================================================================
// Class creation
// ============================================================================

// internal: core class creation logic (no metaclass check)
static Item py_class_new_core(Item name, Item bases, Item methods) {
    // create the class as a Map
    Item cls = py_new_object();

    // sentinel
    py_map_set_cstr(cls, "__is_class__", py_bool_item(true));

    // name
    py_map_set_cstr(cls, "__name__", name);

    // bases
    if (get_type_id(bases) == LMD_TYPE_NULL) {
        // default: base is 'object' (if initialized), else empty list
        if (get_type_id(py_object_class) != LMD_TYPE_NULL) {
            Item default_bases = py_list_new(0);
            py_list_append(default_bases, py_object_class);
            bases = default_bases;
        } else {
            bases = py_list_new(0);
        }
    }
    py_map_set_cstr(cls, "__bases__", bases);

    // copy methods into class map
    if (get_type_id(methods) == LMD_TYPE_MAP) {
        Map* mm = it2map(methods);
        if (mm && mm->type) {
            ShapeEntry* field = ((TypeMap*)mm->type)->shape;
            while (field) {
                Item val = _map_read_field(field, mm->data);
                py_setattr(cls,
                    (Item){.item = s2it((String*)name_pool_create_len(
                        py_input->name_pool, field->name->str, field->name->length))},
                    val);
                field = field->next;
            }
        }
    }

    // compute and store MRO
    Item mro = py_compute_mro(cls);
    py_map_set_cstr(cls, "__mro__", mro);

    // Phase F3: call __init_subclass__(cls) on each direct base that defines it.
    // This notifies parent classes that a new subclass was created.
    if (get_type_id(bases) == LMD_TYPE_ARRAY) {
        Array* bases_arr = it2arr(bases);
        for (int i = 0; i < bases_arr->length; i++) {
            Item base = bases_arr->items[i];
            if (get_type_id(base) != LMD_TYPE_MAP) continue;
            // look up __init_subclass__ in this base's MRO (skip object's default)
            Item init_sub_name = (Item){.item = s2it(heap_create_name("__init_subclass__"))};
            Item init_sub = py_mro_lookup(base, init_sub_name);
            if (get_type_id(init_sub) == LMD_TYPE_NULL) continue;
            if (get_type_id(init_sub) == LMD_TYPE_FUNC) {
                // call it as a class method: __init_subclass__(cls)
                Item args[1] = { cls };
                py_call_function(init_sub, args, 1);
            }
        }
    }

    return cls;
}

extern "C" Item py_class_new(Item name, Item bases, Item methods) {
    // check if any base has an inherited metaclass — if so, delegate to it
    if (get_type_id(bases) == LMD_TYPE_ARRAY) {
        Array* bases_arr = it2arr(bases);
        for (int i = 0; i < bases_arr->length; i++) {
            Item base = bases_arr->items[i];
            if (get_type_id(base) != LMD_TYPE_MAP) continue;
            Item meta = py_map_get_cstr(base, "__metaclass__");
            if (get_type_id(meta) == LMD_TYPE_MAP && py_is_class(meta)) {
                return py_class_new_meta(meta, name, bases, methods);
            }
        }
    }

    return py_class_new_core(name, bases, methods);
}

// ============================================================================
// Metaclass creation (Phase F5)
// ============================================================================

// Call metaclass(name, bases, methods) — the metaclass is treated as a callable.
extern "C" Item py_class_new_meta(Item metaclass, Item name, Item bases, Item methods) {
    Item args[3] = { name, bases, methods };
    return py_call_function(metaclass, args, 3);
}

// type.__new__(mcs, name, bases, namespace) — create a class from a metaclass __new__.
// Exposed to Python code as an attribute of the built-in `type` class.
extern "C" Item py_type_new(Item mcs, Item name, Item bases, Item namespace_dict) {
    Item cls = py_class_new_core(name, bases, namespace_dict);
    // store the metaclass as the class's type so subclasses can inherit it
    if (get_type_id(mcs) == LMD_TYPE_MAP && py_is_class(mcs)) {
        py_map_set_cstr(cls, "__metaclass__", mcs);
    }
    return cls;
}

// ============================================================================
// Instance creation
// ============================================================================

extern "C" Item py_new_instance(Item cls) {
    Item inst = py_new_object();
    py_map_set_cstr(inst, "__class__", cls);
    return inst;
}

// ============================================================================
// Bound method
// ============================================================================

extern "C" Item py_bind_method(Item func, Item self) {
    Item bm = py_new_object();
    py_map_set_cstr(bm, "__is_bound_method__", py_bool_item(true));
    py_map_set_cstr(bm, "__func__", func);
    py_map_set_cstr(bm, "__self__", self);
    return bm;
}

extern "C" bool py_is_bound_method(Item x) {
    return get_type_id(x) == LMD_TYPE_MAP && py_map_flag(x, "__is_bound_method__");
}

// ============================================================================
// super()
// ============================================================================

extern "C" Item py_super(Item type, Item obj) {
    Item sp = py_new_object();
    py_map_set_cstr(sp, "__is_super__", py_bool_item(true));
    py_map_set_cstr(sp, "__type__", type);
    py_map_set_cstr(sp, "__obj__", obj);
    return sp;
}

// ============================================================================
// isinstance / issubclass
// ============================================================================

// Check if a class item matches cls — pointer equality first, then name fallback.
static bool py_class_matches(Item mro_entry, Item cls) {
    if (mro_entry.item == cls.item) return true;
    // string fallback: cls may be a string class name (legacy v1/v2 path)
    if (get_type_id(cls) == LMD_TYPE_STRING) {
        Item entry_name = py_map_get_cstr(mro_entry, "__name__");
        if (get_type_id(entry_name) == LMD_TYPE_STRING) {
            return py_eq(entry_name, cls).item == ITEM_TRUE;
        }
    }
    return false;
}

extern "C" bool py_isinstance_v3(Item obj, Item cls) {
    // handle tuple of classes: isinstance(obj, (A, B))
    if (get_type_id(cls) == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(cls);
        for (int i = 0; i < arr->length; i++) {
            if (py_isinstance_v3(obj, arr->items[i])) return true;
        }
        return false;
    }

    // if obj is a class instance: walk its MRO
    if (py_is_instance(obj)) {
        Item obj_cls = py_get_class(obj);
        if (py_class_matches(obj_cls, cls)) return true;
        Item mro = py_map_get_cstr(obj_cls, "__mro__");
        if (get_type_id(mro) == LMD_TYPE_ARRAY) {
            Array* mro_arr = it2arr(mro);
            for (int i = 0; i < mro_arr->length; i++) {
                if (py_class_matches(mro_arr->items[i], cls)) return true;
            }
        }
        return false;
    }

    // string-name-based fallback for built-in primitive types
    if (get_type_id(cls) == LMD_TYPE_STRING) {
        String* cls_s = it2s(cls);
        if (!cls_s) return false;
        // map Lambda type id to Python type name
        const char* type_name = NULL;
        switch (get_type_id(obj)) {
        case LMD_TYPE_INT:    type_name = "int"; break;
        case LMD_TYPE_FLOAT:  type_name = "float"; break;
        case LMD_TYPE_STRING: type_name = "str"; break;
        case LMD_TYPE_BOOL:   type_name = "bool"; break;
        case LMD_TYPE_NULL:   type_name = "NoneType"; break;
        case LMD_TYPE_ARRAY:  type_name = "list"; break;
        case LMD_TYPE_MAP:    type_name = "dict"; break;
        case LMD_TYPE_FUNC:   type_name = "function"; break;
        default: break;
        }
        if (type_name) {
            size_t tlen = strlen(type_name);
            if ((size_t)cls_s->len == tlen && memcmp(cls_s->chars, type_name, tlen) == 0)
                return true;
        }
        return false;
    }

    return false;
}

extern "C" bool py_issubclass_v3(Item sub, Item cls) {
    if (!py_is_class(sub)) return false;
    if (sub.item == cls.item) return true;
    Item mro = py_map_get_cstr(sub, "__mro__");
    if (get_type_id(mro) == LMD_TYPE_ARRAY) {
        Array* mro_arr = it2arr(mro);
        for (int i = 0; i < mro_arr->length; i++) {
            if (py_class_matches(mro_arr->items[i], cls)) return true;
        }
    }
    return false;
}

// ============================================================================
// Built-in class hierarchy
// ============================================================================

// Register a built-in class so py_isinstance_v3 can resolve string names.
static void register_builtin_class(const char* name, Item cls) {
    if (py_builtin_class_count < PY_BUILTIN_CLASS_MAX) {
        py_builtin_class_names[py_builtin_class_count] = name;
        py_builtin_class_items[py_builtin_class_count] = cls;
        py_builtin_class_count++;
    }
}

// Resolve a built-in class name to its Item (for use in except clauses).
extern "C" Item py_get_builtin_class(const char* name) {
    for (int i = 0; i < py_builtin_class_count; i++) {
        if (strcmp(py_builtin_class_names[i], name) == 0)
            return py_builtin_class_items[i];
    }
    return ItemNull;
}

// built-in Exception.__init__(self, *args) — stores self.args and self.message
static Item py_exception_init(Item self, Item message) {
    py_map_set_cstr(self, "message", message);
    Item args_list = py_list_new(1);
    py_list_append(args_list, message);
    py_map_set_cstr(self, "args", args_list);
    return ItemNull;
}

// built-in Exception.__str__(self) — returns the message string
static Item py_exception_str(Item self) {
    Item msg = py_map_get_cstr(self, "message");
    if (get_type_id(msg) != LMD_TYPE_NULL) return msg;
    return (Item){.item = s2it(heap_create_name(""))};
}

static Item make_builtin_exc_class(const char* name, Item base) {
    Item bases = py_list_new(0);
    py_list_append(bases, base);
    Item methods = py_dict_new();
    Item name_item = (Item){.item = s2it(heap_create_name(name))};
    return py_class_new(name_item, bases, methods);
}

extern "C" void py_init_builtin_classes(void) {
    if (get_type_id(py_object_class) != LMD_TYPE_NULL) return;  // already done

    // register static Item variables as GC roots (BSS memory invisible to stack scanning)
    {
        heap_register_gc_root(&py_object_class.item);
        heap_register_gc_root(&py_type_class.item);
        heap_register_gc_root_range((uint64_t*)py_builtin_class_items, PY_BUILTIN_CLASS_MAX);
    }

    // 'object' — base of all classes, no bases
    Item obj_name  = (Item){.item = s2it(heap_create_name("object"))};
    Item obj_bases = py_list_new(0);
    Item obj_meths = py_dict_new();
    py_object_class = py_new_object();
    py_map_set_cstr(py_object_class, "__is_class__", py_bool_item(true));
    py_map_set_cstr(py_object_class, "__name__", obj_name);
    py_map_set_cstr(py_object_class, "__bases__", obj_bases);
    Item obj_mro = py_list_new(0);
    py_list_append(obj_mro, py_object_class);
    py_map_set_cstr(py_object_class, "__mro__", obj_mro);
    (void)obj_meths;

    register_builtin_class("object", py_object_class);

    // 'type' — built-in metaclass with __new__ → py_type_new
    {
        Item type_name = (Item){.item = s2it(heap_create_name("type"))};
        py_type_class = py_new_object();
        py_map_set_cstr(py_type_class, "__is_class__", py_bool_item(true));
        py_map_set_cstr(py_type_class, "__name__", type_name);
        Item type_bases = py_list_new(0);
        py_list_append(type_bases, py_object_class);
        py_map_set_cstr(py_type_class, "__bases__", type_bases);
        Item type_mro = py_list_new(0);
        py_list_append(type_mro, py_type_class);
        py_list_append(type_mro, py_object_class);
        py_map_set_cstr(py_type_class, "__mro__", type_mro);
        // __new__: py_type_new(mcs, name, bases, namespace) takes 4 params
        Item type_new_fn = py_new_function((void*)py_type_new, 4);
        py_map_set_cstr(py_type_class, "__new__", type_new_fn);
        register_builtin_class("type", py_type_class);
    }

    // Exception hierarchy
    Item exc_cls         = make_builtin_exc_class("Exception",         py_object_class);
    // add built-in __init__ and __str__ to Exception base
    py_map_set_cstr(exc_cls, "__init__", py_new_function((void*)py_exception_init, 2));
    py_map_set_cstr(exc_cls, "__str__",  py_new_function((void*)py_exception_str,  1));
    Item runtime_err     = make_builtin_exc_class("RuntimeError",      exc_cls);
    Item type_err        = make_builtin_exc_class("TypeError",         exc_cls);
    Item val_err         = make_builtin_exc_class("ValueError",        exc_cls);
    Item index_err       = make_builtin_exc_class("IndexError",        exc_cls);
    Item key_err         = make_builtin_exc_class("KeyError",          exc_cls);
    Item attr_err        = make_builtin_exc_class("AttributeError",    exc_cls);
    Item name_err        = make_builtin_exc_class("NameError",         exc_cls);
    Item zero_div_err    = make_builtin_exc_class("ZeroDivisionError", exc_cls);
    Item overflow_err    = make_builtin_exc_class("OverflowError",     exc_cls);
    Item stop_iter       = make_builtin_exc_class("StopIteration",     exc_cls);
    Item not_impl_err    = make_builtin_exc_class("NotImplementedError", runtime_err);
    Item lookup_err      = make_builtin_exc_class("LookupError",       exc_cls);
    Item arithmetic_err  = make_builtin_exc_class("ArithmeticError",   exc_cls);

    register_builtin_class("Exception",           exc_cls);
    register_builtin_class("RuntimeError",        runtime_err);
    register_builtin_class("TypeError",           type_err);
    register_builtin_class("ValueError",          val_err);
    register_builtin_class("IndexError",          index_err);
    register_builtin_class("KeyError",            key_err);
    register_builtin_class("AttributeError",      attr_err);
    register_builtin_class("NameError",           name_err);
    register_builtin_class("ZeroDivisionError",   zero_div_err);
    register_builtin_class("OverflowError",       overflow_err);
    register_builtin_class("StopIteration",       stop_iter);
    register_builtin_class("NotImplementedError", not_impl_err);
    register_builtin_class("LookupError",         lookup_err);
    register_builtin_class("ArithmeticError",     arithmetic_err);

    // primitive type classes — these need __name__ so user code can do int.__name__ etc.
    static const char* prim_names[] = { "int", "float", "str", "bool", "list", "dict",
                                        "tuple", "set", "bytes", "NoneType" };
    for (int i = 0; i < 10; i++) {
        Item prim_cls = py_new_object();
        py_map_set_cstr(prim_cls, "__is_class__", py_bool_item(true));
        Item prim_name_item = (Item){.item = s2it(heap_create_name(prim_names[i]))};
        py_map_set_cstr(prim_cls, "__name__", prim_name_item);
        Item prim_bases = py_list_new(0);
        py_list_append(prim_bases, py_object_class);
        py_map_set_cstr(prim_cls, "__bases__", prim_bases);
        Item prim_mro = py_list_new(0);
        py_list_append(prim_mro, prim_cls);
        py_list_append(prim_mro, py_object_class);
        py_map_set_cstr(prim_cls, "__mro__", prim_mro);
        register_builtin_class(prim_names[i], prim_cls);
    }

    log_debug("py: built-in class hierarchy initialized (%d classes)", py_builtin_class_count);
}
