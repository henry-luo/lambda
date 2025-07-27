// Schema for occurrence types - demonstrates ?, +, * modifiers
type OccurrenceTypes = {
    optional_field: string?,
    one_or_more: string+,
    zero_or_more: string*,
    empty_array: int*,
    single_item: string+
}
