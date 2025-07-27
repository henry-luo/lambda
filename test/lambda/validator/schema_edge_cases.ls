// Schema for edge cases - should handle all edge conditions
type NestedEmpty = {
    empty_array: any*,
    empty_object: any,
    null_value: null | any
}

type EdgeCaseSchema = {
    empty_string: string,
    zero_value: int,
    negative_number: int,
    very_long_string: string,
    nested_empty: NestedEmpty,
    unicode_content: string,
    special_chars: string
}
