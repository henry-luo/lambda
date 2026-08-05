// Parsing failures are non-admissive: they must remain errors, while a
// successfully parsed JSON null remains the valid null value.

let bad_json^bad_json_error = parse("{ invalid json }", 'json')
{value: type(bad_json), error: type(bad_json_error)}

let parsed_null^null_error = parse("null", 'json')
{value: type(parsed_null), error: type(null_error)}

let unknown_format^unknown_format_error = parse("anything", 'not_a_format')
{value: type(unknown_format), error: type(unknown_format_error)}

let bad_fragment = parse_html_fragment(42)
bad_fragment is error

type(parse_html_fragment("<p>ok</p>"))
