// test cross-module array indexing with various array types
// verifies that imported arrays preserve nested type info for correct accessor dispatch
import .mod_arrays

// int array indexing (ArrayInt - must use array_int_get, not array_get)
int_arr[0]
int_arr[4]

// string array indexing (Array of Items)
[str_arr[1], str_arr[3]]

// float array indexing (ArrayFloat)
float_arr[2]

// mixed array indexing (Array of Items)
mixed_arr[0]
mixed_arr[1]

// nested array indexing
nested_arr[2]

// map field access
items_map.b

// cross-module function that indexes locally (should still work)
get_item(int_arr, 2)
