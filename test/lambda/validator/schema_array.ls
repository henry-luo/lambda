// Schema for array types - demonstrates array_type syntax
type ArrayTypes = {
    simple_array: [int*],
    string_array: [string*],
    nested_array: [[int*]*],
    mixed_array: [int | string | bool | null*],
    empty_array: [any*]
}
