// S16.8.9: computed map keys retain shaped-map semantics and source order.
let key = "answer"
let base = {answer: 1, retained: 7}
let built = {fixed: 2, [key]: 3, *:base, [key]: 4};
[built.fixed, built[key], built.retained, len(built)]

// Symbols are NameKeys too, not VMap keys.
let symbol_key = 'kind'
let symbol_map = {[symbol_key]: "symbol"}
symbol_map.kind

// Computed attributes use the same normal element attribute face.
let attr = "data-value"
let input_el = <input type: "range", [attr]: "42">;
[input_el["type"], input_el[attr]]

// Computed attributes retain static dotted-attribute normalization.
let qualified = <node meta.one: "first", [attr]: "value", meta.two: "second">;
[qualified.meta.one, qualified.meta.two, qualified[attr]]

// A bracketed expression without a following colon remains a braced block.
{[1, 2]}
