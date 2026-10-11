// GM3/GM10: a JSON grammar — a space-free string rule, a hidden separator,
// labels as attributes, and repetition.
grammar json {
    value   = object | array | string | number | "true" | "false" | "null"
    object  = "{", (pair, (_comma, pair)*)?, "}"
    pair    = key: string, ":", value: value
    array   = "[", (value, (_comma, value)*)?, "]"
    nospace string = "\"", chars?, "\""
    chars   := ("\\" . | !("\"" | "\\"))+
    number  := "-"? "\d"+ ("." "\d"+)? (("e" | "E") ("+" | "-")? "\d"+)?
    _comma  := ","
    space   := "\s"+
}

let doc = parse("{\"a\": [1, 2.5, -3e2], \"b\": {\"c\": null}}", json)^
doc
let pairs = [for (p in doc[0]) if (p is element) p]
;[for (p in pairs) p.key[0]]
parse("[ true , false ]", json)^
parse(" \"x\\\"y\" ", json)^
;["[1, 2]" is json, "[1, 2" is json, "[1 2]" is json]
