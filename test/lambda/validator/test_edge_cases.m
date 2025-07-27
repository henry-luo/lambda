// Test data for edge cases
{
    empty_string: "",
    zero_value: 0,
    negative_number: -42,
    very_long_string: "This is a very long string that tests the validator's ability to handle lengthy text content without issues. It should still validate correctly according to the schema definition.",
    nested_empty: {
        empty_array: [],
        empty_object: {},
        null_value: null
    },
    unicode_content: "Unicode test: 你好世界 🌍 αβγ",
    special_chars: "Special: !@#$%^&*()_+{}|:<>?[];'\"\\,./`~"
}
