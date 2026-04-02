// rb_class.cpp — Ruby class system runtime functions
// Classes and instances are Lambda Maps with sentinel fields.
// Class:    { __rb_class__: true, __name__: "Foo", __superclass__: <class>, methods... }
// Instance: { __class__: <class>, instance vars... }

#include "rb_runtime.h"
#include "rb_transpiler.hpp"
#include "../lambda-data.hpp"
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

    // look up on instance → class chain
    return rb_instance_getattr(receiver, method_name);
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
// Block / Proc call
// ============================================================================

// Call a block/proc/lambda function with arguments.
// block is an Item wrapping a MIR func ptr (created by js_new_function).
extern "C" Item rb_block_call(Item block, Item* args, int argc) {
    if (block.item == ITEM_NULL || get_type_id(block) != LMD_TYPE_FUNC) {
        return (Item){.item = ITEM_NULL};
    }

    void* fptr = js_function_get_ptr(block);
    if (!fptr) return (Item){.item = ITEM_NULL};

    // call based on argc
    typedef Item (*RbFunc0)(void);
    typedef Item (*RbFunc1)(Item);
    typedef Item (*RbFunc2)(Item, Item);
    typedef Item (*RbFunc3)(Item, Item, Item);
    typedef Item (*RbFunc4)(Item, Item, Item, Item);
    typedef Item (*RbFunc5)(Item, Item, Item, Item, Item);

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
// Iterator methods (array.each { |x| ... }, etc.)
// ============================================================================

// array.each { |x| block }
extern "C" Item rb_array_each(Item array, Item block) {
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
