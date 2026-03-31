#pragma once

// ts_runtime.h — TypeScript runtime helper declarations (callable from C/MIR)

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// runtime type checking
Item ts_typeof(Item value);
Item ts_check_shape(Item obj, Item type_item);
Item ts_assert_type(Item value, Item type_item);

// enum support
Item ts_enum_create(int member_count);
Item ts_enum_add_member(Item enum_obj, Item name, Item value);
Item ts_enum_freeze(Item enum_obj);

#ifdef __cplusplus
}
#endif
