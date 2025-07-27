// Test data that doesn't match schema types
{
    string_field: 42,        // Should be string, got int
    int_field: "not_a_number", // Should be int, got string
    bool_field: null         // Should be bool, got null
}
