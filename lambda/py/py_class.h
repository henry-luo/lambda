#pragma once

// py_class.h — C API for Python class system
// Class objects and instances are both Lambda Map values distinguished by
// sentinel fields:
//   Class:    __is_class__ = true, __name__, __bases__, __mro__
//   Instance: __class__ = <class Map Item>
// Bound methods: __is_bound_method__ = true, __func__, __self__

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ========================================================================
// Class creation
// ========================================================================

// Create a new class object from name (string Item), bases (list Item), and
// methods (dict Item — copied into the class map).
Item py_class_new(Item name, Item bases, Item methods);

// Compute and return the MRO list (C3 linearization) as a py list.
Item py_compute_mro(Item cls);

// Allocate a new blank instance of cls with __class__ set.
Item py_new_instance(Item cls);

// ========================================================================
// Introspection helpers (all fast — single Map field lookups)
// ========================================================================

// Returns true if x is a class object (__is_class__ == true).
bool py_is_class(Item x);

// Returns true if x is a class instance (__class__ field present).
bool py_is_instance(Item x);

// Returns the class object for an instance, or ItemNull if not an instance.
Item py_get_class(Item obj);

// Walk the MRO of cls looking for an attribute named `name`.
// Returns the raw (unbound) value, or ItemNull if not found.
Item py_mro_lookup(Item cls, Item name);

// ========================================================================
// Method binding
// ========================================================================

// Wrap a function and self into a bound-method Map.
Item py_bind_method(Item func, Item self);

// Returns true if x is a bound-method Map.
bool py_is_bound_method(Item x);

// ========================================================================
// super()
// ========================================================================

// Create a super proxy: MRO lookup starts at the class after `type` in
// obj.__class__.__mro__.
Item py_super(Item type, Item obj);

// ========================================================================
// isinstance / issubclass (full MRO-based)
// ========================================================================

// isinstance(obj, cls): true if obj.__class__.__mro__ contains cls.
// Falls back to string-name comparison for legacy string class names.
bool py_isinstance_v3(Item obj, Item cls);

// issubclass(sub, cls): true if sub.__mro__ contains cls.
bool py_issubclass_v3(Item sub, Item cls);

// ========================================================================
// Map helpers (public — used from py_runtime.cpp and py_builtins.cpp)
// ========================================================================

// Get a field by C string key from a Map. Returns ItemNull if absent.
Item py_map_get_cstr(Item obj, const char* key);

// Set a field by C string key on a Map.
Item py_map_set_cstr(Item obj, const char* key, Item value);

// ========================================================================
// Built-in class hierarchy initialisation
// ========================================================================

// Called once at startup to create 'object', 'Exception', 'ValueError',
// 'TypeError', 'IndexError', 'KeyError', 'AttributeError',
// 'StopIteration', 'ZeroDivisionError', 'RuntimeError'.
// Each is stored as a module variable accessible by name.
void py_init_builtin_classes(void);

// Global 'object' class item (set after py_init_builtin_classes).
extern Item py_object_class;

#ifdef __cplusplus
}
#endif
