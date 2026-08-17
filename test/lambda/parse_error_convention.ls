// Parsing failures are non-admissive: they must remain errors, while a
// successfully parsed JSON null remains the valid null value.

let bad_json = parse("{ invalid json }", 'json') ^ { ^ }
{value: type(bad_json), error: type(bad_json)}

let parsed_null = parse("null", 'json') ^ { ^ }
{value: type(parsed_null), error: type(parsed_null)}

let unknown_format = parse("anything", 'not_a_format') ^ { ^ }
{value: type(unknown_format), error: type(unknown_format)}

let bad_fragment = parse_html_fragment(42)
bad_fragment is error

type(parse_html_fragment("<p>ok</p>"))
