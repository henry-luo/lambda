// rb_class.cpp — Ruby class system runtime functions
// Classes and instances are Lambda Maps with sentinel fields.
// Class:    { __rb_class__: true, __name__: "Foo", __superclass__: <class>, methods... }
// Instance: { __class__: <class>, instance vars... }

#include "rb_runtime.h"
#include "rb_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <cstring>

// external: get func pointer from Item (JsFunction or Function layout)
extern "C" void* js_function_get_ptr(Item fn_item);

// ============================================================================
// Helpers — set/get named attributes on maps
// ============================================================================

static void rb_map_set_cstr(Item map, const char* key, Item value) {
    Item k = (Item){.item = s2it(heap_create_name(key, strlen(key)))};
    rb_setattr(map, k, value);
}

static Item rb_map_get_cstr(Item map, const char* key) {
    Item k = (Item){.item = s2it(heap_create_name(key, strlen(key)))};
    return rb_getattr(map, k);
}

// ============================================================================
// Class creation
// ============================================================================

// Create a new Ruby class object
extern "C" Item rb_class_create(Item name, Item superclass) {
    Item cls = rb_new_object();

    // sentinel: this is a class
    Item true_val = (Item){.item = ITEM_TRUE};
    rb_map_set_cstr(cls, "__rb_class__", true_val);

    // class name
    rb_map_set_cstr(cls, "__name__", name);

    // superclass (can be null for top-level classes)
    if (superclass.item != ITEM_NULL) {
        rb_map_set_cstr(cls, "__superclass__", superclass);
    }

    log_debug("rb-class: created class '%s'", it2s(name) ? it2s(name)->chars : "?");
    return cls;
}

// Add a method to a class
extern "C" void rb_class_add_method(Item cls, Item method_name, Item func) {
    rb_setattr(cls, method_name, func);
}

// ============================================================================
// Instance creation
// ============================================================================

// Create a new instance of a class
extern "C" Item rb_class_new_instance(Item cls) {
    Item inst = rb_new_object();
    rb_map_set_cstr(inst, "__class__", cls);
    return inst;
}

// Check if an Item is a class
extern "C" int rb_is_class(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return 0;
    Item marker = rb_map_get_cstr(obj, "__rb_class__");
    return (marker.bool_val == true) ? 1 : 0;
}

// Check if an Item is an instance (has __class__)
extern "C" int rb_is_instance(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return 0;
    Item cls = rb_map_get_cstr(obj, "__class__");
    return (cls.item != ITEM_NULL) ? 1 : 0;
}

// Get the class of an instance
extern "C" Item rb_get_class(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = ITEM_NULL};
    return rb_map_get_cstr(obj, "__class__");
}

// ============================================================================
// Attribute lookup with inheritance
// ============================================================================

// Walk superclass chain to find a method/attribute
static Item rb_superclass_lookup(Item cls, const char* name, int name_len) {
    Item current = cls;
    while (current.item != ITEM_NULL && get_type_id(current) == LMD_TYPE_MAP) {
        Item k = (Item){.item = s2it(heap_create_name(name, name_len))};
        Item val = rb_getattr(current, k);
        if (val.item != ITEM_NULL) return val;
        current = rb_map_get_cstr(current, "__superclass__");
    }
    return (Item){.item = ITEM_NULL};
}

// Instance attribute access: check instance fields, then class hierarchy
extern "C" Item rb_instance_getattr(Item instance, Item name) {
    if (get_type_id(instance) != LMD_TYPE_MAP) return (Item){.item = ITEM_NULL};

    // first check own fields (instance variables)
    Item val = rb_getattr(instance, name);
    if (val.item != ITEM_NULL) return val;

    // walk class hierarchy for methods
    Item cls = rb_map_get_cstr(instance, "__class__");
    if (cls.item == ITEM_NULL) return (Item){.item = ITEM_NULL};

    String* s = it2s(name);
    if (!s) return (Item){.item = ITEM_NULL};

    return rb_superclass_lookup(cls, s->chars, (int)s->len);
}

// Instance attribute set (instance variables)
extern "C" void rb_instance_setattr(Item instance, Item name, Item value) {
    rb_setattr(instance, name, value);
}

// ============================================================================
// Method call on instances
// ============================================================================

// Call a method found by name on an object, passing self as first arg
// Returns ITEM_NULL if method not found
// The actual call is done in MIR — this is for dynamic dispatch fallback
extern "C" Item rb_method_lookup(Item receiver, Item method_name) {
    if (get_type_id(receiver) != LMD_TYPE_MAP) return (Item){.item = ITEM_NULL};

    // check if receiver is a class (for Class.new, Class.method style calls)
    if (rb_is_class(receiver)) {
        return rb_getattr(receiver, method_name);
    }

    // For instances: skip own fields (instance variables like @name stored as "name"
    // would shadow methods of the same name). Walk class hierarchy only.
    Item cls = rb_map_get_cstr(receiver, "__class__");
    if (cls.item == ITEM_NULL) return (Item){.item = ITEM_NULL};

    String* s = it2s(method_name);
    if (!s) return (Item){.item = ITEM_NULL};

    return rb_superclass_lookup(cls, s->chars, (int)s->len);
}

// ============================================================================
// super call support
// ============================================================================

// Look up a method in the superclass (skipping the current class)
extern "C" Item rb_super_lookup(Item cls, Item method_name) {
    // go to superclass first
    Item super = rb_map_get_cstr(cls, "__superclass__");
    if (super.item == ITEM_NULL) return (Item){.item = ITEM_NULL};

    String* s = it2s(method_name);
    if (!s) return (Item){.item = ITEM_NULL};

    return rb_superclass_lookup(super, s->chars, (int)s->len);
}

// ============================================================================
// attr_reader / attr_writer / attr_accessor generation
// ============================================================================

// Generate a getter method that retrieves @name from self
// We store this as a marker — the transpiler will generate the actual MIR function
extern "C" void rb_attr_reader(Item cls, Item attr_name) {
    // store marker: __attr_r_<name> = true
    String* s = it2s(attr_name);
    if (!s) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "__attr_r_%.*s", (int)s->len, s->chars);
    rb_map_set_cstr(cls, buf, (Item){.bool_val = true});
}

extern "C" void rb_attr_writer(Item cls, Item attr_name) {
    String* s = it2s(attr_name);
    if (!s) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "__attr_w_%.*s", (int)s->len, s->chars);
    rb_map_set_cstr(cls, buf, (Item){.bool_val = true});
}

extern "C" void rb_attr_accessor(Item cls, Item attr_name) {
    rb_attr_reader(cls, attr_name);
    rb_attr_writer(cls, attr_name);
}

// ============================================================================
// Module include — copy methods from module to class
// ============================================================================

extern "C" void rb_module_include(Item cls, Item module) {
    if (get_type_id(cls) != LMD_TYPE_MAP || get_type_id(module) != LMD_TYPE_MAP) return;

    // insert module into the superclass chain between cls and its current superclass
    // cls.__superclass__ = module, module.__superclass__ = old_super
    Item old_super = rb_map_get_cstr(cls, "__superclass__");
    rb_map_set_cstr(cls, "__superclass__", module);
    if (old_super.item != ITEM_NULL) {
        // only set if the module doesn't already have a superclass
        Item mod_super = rb_map_get_cstr(module, "__superclass__");
        if (mod_super.item == ITEM_NULL) {
            rb_map_set_cstr(module, "__superclass__", old_super);
        }
    }
}

// ============================================================================
// Block / Proc call
// ============================================================================

// Get closure env from a JsFunction (returns NULL for non-closures)
// Mirrors JsFunction struct layout from js_runtime.cpp
struct RbJsFuncView {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
};

static Item* rb_get_closure_env(Item block) {
    if (get_type_id(block) != LMD_TYPE_FUNC) return NULL;
    RbJsFuncView* fn = (RbJsFuncView*)block.function;
    return fn->env;
}

// Call a block/proc/lambda function with arguments.
// block is an Item wrapping a MIR func ptr (created by js_new_function or js_new_closure).
extern "C" Item rb_block_call(Item block, Item* args, int argc) {
    if (block.item == ITEM_NULL || get_type_id(block) != LMD_TYPE_FUNC) {
        return (Item){.item = ITEM_NULL};
    }

    void* fptr = js_function_get_ptr(block);
    if (!fptr) return (Item){.item = ITEM_NULL};

    // check for closure env
    Item* env = rb_get_closure_env(block);

    // call based on argc
    typedef Item (*RbFunc0)(void);
    typedef Item (*RbFunc1)(Item);
    typedef Item (*RbFunc2)(Item, Item);
    typedef Item (*RbFunc3)(Item, Item, Item);
    typedef Item (*RbFunc4)(Item, Item, Item, Item);
    typedef Item (*RbFunc5)(Item, Item, Item, Item, Item);
    typedef Item (*RbFunc6)(Item, Item, Item, Item, Item, Item);

    if (env) {
        // closure: prepend env pointer as first argument
        Item env_item;
        env_item.item = (uint64_t)env;
        switch (argc) {
            case 0: return ((RbFunc1)fptr)(env_item);
            case 1: return ((RbFunc2)fptr)(env_item, args[0]);
            case 2: return ((RbFunc3)fptr)(env_item, args[0], args[1]);
            case 3: return ((RbFunc4)fptr)(env_item, args[0], args[1], args[2]);
            case 4: return ((RbFunc5)fptr)(env_item, args[0], args[1], args[2], args[3]);
            case 5: return ((RbFunc6)fptr)(env_item, args[0], args[1], args[2], args[3], args[4]);
            default: {
                log_debug("rb-class: closure block_call with %d args, truncating to 5", argc);
                return ((RbFunc6)fptr)(env_item, args[0], args[1], args[2], args[3], args[4]);
            }
        }
    } else {
        switch (argc) {
            case 0: return ((RbFunc0)fptr)();
            case 1: return ((RbFunc1)fptr)(args[0]);
            case 2: return ((RbFunc2)fptr)(args[0], args[1]);
            case 3: return ((RbFunc3)fptr)(args[0], args[1], args[2]);
            case 4: return ((RbFunc4)fptr)(args[0], args[1], args[2], args[3]);
            case 5: return ((RbFunc5)fptr)(args[0], args[1], args[2], args[3], args[4]);
            default: {
                log_debug("rb-class: block_call with %d args, truncating to 5", argc);
                return ((RbFunc5)fptr)(args[0], args[1], args[2], args[3], args[4]);
            }
        }
    }
}

// Convenience: call block with 0 args
extern "C" Item rb_block_call_0(Item block) {
    return rb_block_call(block, NULL, 0);
}

// Convenience: call block with 1 arg
extern "C" Item rb_block_call_1(Item block, Item arg) {
    return rb_block_call(block, &arg, 1);
}

// Convenience: call block with 2 args
extern "C" Item rb_block_call_2(Item block, Item arg1, Item arg2) {
    Item args[2] = {arg1, arg2};
    return rb_block_call(block, args, 2);
}

// ============================================================================
// Method dispatch helper: call a method on an object by name
// ============================================================================

// Call a method on receiver: receiver.method_name(arg)
extern "C" Item rb_send_1(Item receiver, const char* method_name, Item arg) {
    Item mname = (Item){.item = s2it(heap_create_name(method_name, (int)strlen(method_name)))};
    Item method = rb_method_lookup(receiver, mname);
    if (method.item == ITEM_NULL || get_type_id(method) != LMD_TYPE_FUNC) {
        return (Item){.item = ITEM_NULL};
    }
    return rb_block_call_2(method, receiver, arg);
}

// Call <=> on an object
extern "C" Item rb_call_spaceship(Item left, Item right) {
    return rb_send_1(left, "<=>", right);
}

// ============================================================================
// method_missing support
// ============================================================================

// Call method_missing on receiver if defined in its class hierarchy.
// Signature: method_missing(self, method_name_as_string, *args)
// Returns ITEM_NULL if method_missing is not defined.
extern "C" Item rb_call_method_missing(Item receiver, Item method_name, Item* args, int argc) {
    if (get_type_id(receiver) != LMD_TYPE_MAP) return (Item){.item = ITEM_NULL};

    // look up method_missing in class hierarchy
    Item cls = rb_map_get_cstr(receiver, "__class__");
    if (cls.item == ITEM_NULL) return (Item){.item = ITEM_NULL};

    Item mm_fn = rb_superclass_lookup(cls, "method_missing", 14);
    if (mm_fn.item == ITEM_NULL || get_type_id(mm_fn) != LMD_TYPE_FUNC) {
        return (Item){.item = ITEM_NULL};
    }

    // call method_missing(self, method_name, args...)
    // build combined args: [self, method_name, arg0, arg1, ...]
    int total = 2 + argc; // self + method_name + user args
    Item combined[16]; // max 14 user args
    if (total > 16) total = 16;
    combined[0] = receiver;
    combined[1] = method_name;
    for (int i = 0; i < argc && (i + 2) < 16; i++) {
        combined[i + 2] = args[i];
    }
    return rb_block_call(mm_fn, combined, total);
}

// ============================================================================
// Struct support
// ============================================================================

// Create a new struct class from an array of field name symbols/strings.
// Returns a class (map) with __struct_fields__ metadata.
extern "C" Item rb_struct_new(Item fields_array) {
    Item cls = rb_new_object();
    rb_map_set_cstr(cls, "__rb_class__", (Item){.item = b2it(true)});
    rb_map_set_cstr(cls, "__name__", (Item){.item = s2it(heap_create_name("Struct", 6))});
    rb_map_set_cstr(cls, "__struct_fields__", fields_array);
    return cls;
}

// Initialize a struct instance with positional args.
// Called when StructClass.new(arg1, arg2, ...) is detected.
extern "C" Item rb_struct_init(Item instance, Item cls, Item* args, int argc) {
    Item fields_array = rb_map_get_cstr(cls, "__struct_fields__");
    if (fields_array.item == ITEM_NULL) return instance;

    Array* fields = it2arr(fields_array);
    if (!fields) return instance;

    for (int64_t i = 0; i < fields->length && i < argc; i++) {
        // field name is a symbol or string — extract the name
        Item field = fields->items[i];
        String* fname = NULL;
        if (get_type_id(field) == LMD_TYPE_SYMBOL) {
            // Symbol has same layout as String in Lambda
            fname = it2s(field);
        } else if (get_type_id(field) == LMD_TYPE_STRING) {
            fname = it2s(field);
        }
        if (fname) {
            // set as instance variable @field_name
            char ivar_name[256];
            snprintf(ivar_name, sizeof(ivar_name), "@%.*s", (int)fname->len, fname->chars);
            Item key = (Item){.item = s2it(heap_create_name(ivar_name, (int)strlen(ivar_name)))};
            rb_setattr(instance, key, args[i]);

            // also set a getter-friendly key (field_name without @) for .field access
            Item getter_key = (Item){.item = s2it(fname)};
            rb_setattr(instance, getter_key, args[i]);
        }
    }
    return instance;
}

// Check if a class is a struct class (has __struct_fields__)
extern "C" Item rb_is_struct(Item cls) {
    Item fields = rb_map_get_cstr(cls, "__struct_fields__");
    return (Item){.item = b2it(fields.item != ITEM_NULL)};
}

// Get struct members array
extern "C" Item rb_struct_members(Item instance) {
    Item cls = rb_map_get_cstr(instance, "__class__");
    if (cls.item == ITEM_NULL) return rb_array_new();
    return rb_map_get_cstr(cls, "__struct_fields__");
}

// ============================================================================
// Iterator methods (array.each { |x| ... }, etc.)
// ============================================================================

// forward declarations for hash iterators (used by array->hash dispatch)
extern "C" Item rb_hash_each(Item hash, Item block);
extern "C" Item rb_hash_map(Item hash, Item block);
extern "C" Item rb_hash_select(Item hash, Item block);

// array.each { |x| block }
extern "C" Item rb_array_each(Item array, Item block) {
    // dispatch to hash.each if map type
    if (get_type_id(array) == LMD_TYPE_MAP) return rb_hash_each(array, block);
    Array* arr = it2arr(array);
    if (!arr) return array;
    Item result = (Item){.item = ITEM_NULL};
    for (int64_t i = 0; i < arr->length; i++) {
        result = rb_block_call_1(block, arr->items[i]);
    }
    return array;  // each returns the receiver
}

// array.map { |x| block }
extern "C" Item rb_array_map(Item array, Item block) {
    if (get_type_id(array) == LMD_TYPE_MAP) return rb_hash_map(array, block);
    Array* arr = it2arr(array);
    if (!arr) return rb_array_new();
    Item result = rb_array_new();
    for (int64_t i = 0; i < arr->length; i++) {
        Item val = rb_block_call_1(block, arr->items[i]);
        rb_array_push(result, val);
    }
    return result;
}

// array.select { |x| block } (filter)
extern "C" Item rb_array_select(Item array, Item block) {
    if (get_type_id(array) == LMD_TYPE_MAP) return rb_hash_select(array, block);
    Array* arr = it2arr(array);
    if (!arr) return rb_array_new();
    Item result = rb_array_new();
    for (int64_t i = 0; i < arr->length; i++) {
        Item val = rb_block_call_1(block, arr->items[i]);
        if (rb_is_truthy(val)) {
            rb_array_push(result, arr->items[i]);
        }
    }
    return result;
}

// array.reject { |x| block } (inverse of select)
extern "C" Item rb_array_reject(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr) return rb_array_new();
    Item result = rb_array_new();
    for (int64_t i = 0; i < arr->length; i++) {
        Item val = rb_block_call_1(block, arr->items[i]);
        if (!rb_is_truthy(val)) {
            rb_array_push(result, arr->items[i]);
        }
    }
    return result;
}

// array.reduce(init) { |acc, x| block }
extern "C" Item rb_array_reduce(Item array, Item initial, Item block) {
    Array* arr = it2arr(array);
    if (!arr) return initial;
    Item acc = initial;
    for (int64_t i = 0; i < arr->length; i++) {
        acc = rb_block_call_2(block, acc, arr->items[i]);
    }
    return acc;
}

// array.each_with_index { |x, i| block }
extern "C" Item rb_array_each_with_index(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr) return array;
    for (int64_t i = 0; i < arr->length; i++) {
        Item idx = (Item){.item = i2it(i)};
        rb_block_call_2(block, arr->items[i], idx);
    }
    return array;
}

// array.any? { |x| block }
extern "C" Item rb_array_any(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr) return (Item){.bool_val = false};
    for (int64_t i = 0; i < arr->length; i++) {
        Item val = rb_block_call_1(block, arr->items[i]);
        if (rb_is_truthy(val)) return (Item){.bool_val = true};
    }
    return (Item){.bool_val = false};
}

// array.all? { |x| block }
extern "C" Item rb_array_all(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr) return (Item){.bool_val = true};
    for (int64_t i = 0; i < arr->length; i++) {
        Item val = rb_block_call_1(block, arr->items[i]);
        if (!rb_is_truthy(val)) return (Item){.bool_val = false};
    }
    return (Item){.bool_val = true};
}

// array.find { |x| block } — returns first element where block is truthy
extern "C" Item rb_array_find(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr) return (Item){.item = ITEM_NULL};
    for (int64_t i = 0; i < arr->length; i++) {
        Item val = rb_block_call_1(block, arr->items[i]);
        if (rb_is_truthy(val)) return arr->items[i];
    }
    return (Item){.item = ITEM_NULL};
}

// n.times { |i| block }
extern "C" Item rb_int_times(Item n, Item block) {
    int64_t count = it2i(n);
    Item result = (Item){.item = ITEM_NULL};
    for (int64_t i = 0; i < count; i++) {
        result = rb_block_call_1(block, (Item){.item = i2it(i)});
    }
    return n;
}

// n.upto(m) { |i| block }
extern "C" Item rb_int_upto(Item n, Item m, Item block) {
    int64_t start = it2i(n);
    int64_t end = it2i(m);
    for (int64_t i = start; i <= end; i++) {
        rb_block_call_1(block, (Item){.item = i2it(i)});
    }
    return n;
}

// n.downto(m) { |i| block }
extern "C" Item rb_int_downto(Item n, Item m, Item block) {
    int64_t start = it2i(n);
    int64_t end = it2i(m);
    for (int64_t i = start; i >= end; i--) {
        rb_block_call_1(block, (Item){.item = i2it(i)});
    }
    return n;
}

// array.flat_map { |x| block } — map then flatten one level
extern "C" Item rb_array_flat_map(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr) return rb_array_new();
    Item result = rb_array_new();
    for (int64_t i = 0; i < arr->length; i++) {
        Item val = rb_block_call_1(block, arr->items[i]);
        Array* sub = it2arr(val);
        if (sub) {
            for (int64_t j = 0; j < sub->length; j++) {
                rb_array_push(result, sub->items[j]);
            }
        } else {
            rb_array_push(result, val);
        }
    }
    return result;
}

// array.each_with_object(obj) { |x, obj| block }
extern "C" Item rb_array_each_with_object(Item array, Item obj, Item block) {
    Array* arr = it2arr(array);
    if (!arr) return obj;
    for (int64_t i = 0; i < arr->length; i++) {
        rb_block_call_2(block, arr->items[i], obj);
    }
    return obj;
}

// array.sort_by { |x| block }
extern "C" Item rb_array_sort_by(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr || arr->length <= 1) return array;
    int64_t n = arr->length;
    Item* keys = (Item*)alloca(sizeof(Item) * n);
    Item* elems = (Item*)alloca(sizeof(Item) * n);
    for (int64_t i = 0; i < n; i++) {
        elems[i] = arr->items[i];
        keys[i] = rb_block_call_1(block, arr->items[i]);
    }
    for (int64_t i = 1; i < n; i++) {
        Item key = keys[i];
        Item elem = elems[i];
        int64_t j = i - 1;
        while (j >= 0) {
            Item cmp = rb_cmp(keys[j], key);
            int64_t cmpv = it2i(cmp);
            if (cmpv <= 0) break;
            keys[j + 1] = keys[j];
            elems[j + 1] = elems[j];
            j--;
        }
        keys[j + 1] = key;
        elems[j + 1] = elem;
    }
    Item result = rb_array_new();
    for (int64_t i = 0; i < n; i++) {
        rb_array_push(result, elems[i]);
    }
    return result;
}

// array.min_by { |x| block }
extern "C" Item rb_array_min_by(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr || arr->length == 0) return (Item){.item = ITEM_NULL};
    Item best = arr->items[0];
    Item best_key = rb_block_call_1(block, arr->items[0]);
    for (int64_t i = 1; i < arr->length; i++) {
        Item key = rb_block_call_1(block, arr->items[i]);
        Item cmp = rb_cmp(key, best_key);
        if (it2i(cmp) < 0) {
            best = arr->items[i];
            best_key = key;
        }
    }
    return best;
}

// array.max_by { |x| block }
extern "C" Item rb_array_max_by(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr || arr->length == 0) return (Item){.item = ITEM_NULL};
    Item best = arr->items[0];
    Item best_key = rb_block_call_1(block, arr->items[0]);
    for (int64_t i = 1; i < arr->length; i++) {
        Item key = rb_block_call_1(block, arr->items[i]);
        Item cmp = rb_cmp(key, best_key);
        if (it2i(cmp) > 0) {
            best = arr->items[i];
            best_key = key;
        }
    }
    return best;
}

// array.reduce { |acc, x| block } — no initial value version
extern "C" Item rb_array_reduce_no_init(Item array, Item block) {
    Array* arr = it2arr(array);
    if (!arr || arr->length == 0) return (Item){.item = ITEM_NULL};
    Item acc = arr->items[0];
    for (int64_t i = 1; i < arr->length; i++) {
        acc = rb_block_call_2(block, acc, arr->items[i]);
    }
    return acc;
}

// hash.each { |k, v| block }
extern "C" Item rb_hash_each(Item hash, Item block) {
    if (get_type_id(hash) != LMD_TYPE_MAP) return hash;
    MapReader reader = MapReader::fromItem(hash);
    if (!reader.isValid()) return hash;
    MapReader::EntryIterator it = reader.entries();
    const char* key;
    ItemReader value;
    while (it.next(&key, &value)) {
        if (key[0] == '_' && key[1] == '_') continue;
        Item k = (Item){.item = s2it(heap_create_name(key, (int)strlen(key)))};
        rb_block_call_2(block, k, value.item());
    }
    return hash;
}

// hash.map { |k, v| block }
extern "C" Item rb_hash_map(Item hash, Item block) {
    if (get_type_id(hash) != LMD_TYPE_MAP) return rb_array_new();
    MapReader reader = MapReader::fromItem(hash);
    if (!reader.isValid()) return rb_array_new();
    Item result = rb_array_new();
    MapReader::EntryIterator it = reader.entries();
    const char* key;
    ItemReader value;
    while (it.next(&key, &value)) {
        if (key[0] == '_' && key[1] == '_') continue;
        Item k = (Item){.item = s2it(heap_create_name(key, (int)strlen(key)))};
        Item val = rb_block_call_2(block, k, value.item());
        rb_array_push(result, val);
    }
    return result;
}

// hash.select { |k, v| block }
extern "C" Item rb_hash_select(Item hash, Item block) {
    if (get_type_id(hash) != LMD_TYPE_MAP) return rb_new_object();
    MapReader reader = MapReader::fromItem(hash);
    if (!reader.isValid()) return rb_new_object();
    Item result = rb_new_object();
    MapReader::EntryIterator it = reader.entries();
    const char* key;
    ItemReader value;
    while (it.next(&key, &value)) {
        if (key[0] == '_' && key[1] == '_') continue;
        Item k = (Item){.item = s2it(heap_create_name(key, (int)strlen(key)))};
        Item val = rb_block_call_2(block, k, value.item());
        if (rb_is_truthy(val)) {
            rb_setattr(result, k, value.item());
        }
    }
    return result;
}
