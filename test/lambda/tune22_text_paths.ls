// Tune22 text fast paths: list join provenance and literal-RHS equality.
fn literal_cases(value) => [
    value == "",
    value == "0",
    value == "level",
    value != "0"
]

let ascii_join = join(["alpha", "beta"], "-")
let symbol_join = join(['alpha', 'beta'], ":")
let unicode_join = join(["hé", "世界"], "-")
let split_empty = split("", ",")
let split_repeated = split("a,b,,c,", ",")
let split_unicode_chars = split("hé世界", "")
let split_symbol = split('a:b', ':')
let split_whitespace = split("  alpha\tbeta  ", null)
let split_keep_delim = split("a--b--", "--", true)

{
  join_values: [ascii_join, symbol_join, unicode_join],
  join_index: [ascii_join[5], ascii_join[9], unicode_join[1]],
  split_values: [split_empty, split_repeated, split_unicode_chars, split_symbol,
    split_whitespace, split_keep_delim],
  literal_zero: literal_cases("0"),
  literal_prefix: literal_cases("0x"),
  literal_empty: literal_cases(""),
  literal_non_string: literal_cases(0),
  literal_null: literal_cases(null),
  literal_error: [
    error("literal poison") == "",
    error("literal poison") == "0",
    error("literal poison") == "level",
    error("literal poison") != "0"
  ]
}
