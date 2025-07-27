// Schema for union types - demonstrates binary type expressions
type UnionTypes = {
    string_or_int: string | int,
    number_or_bool: int | bool,
    nullable_string: string | null,
    multi_union: int | float | string,
    optional_field: string | null
}
