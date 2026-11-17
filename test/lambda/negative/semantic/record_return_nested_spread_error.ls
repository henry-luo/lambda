// S7.7.2v2: a failed nested map is an error value, not an admitted map field.
type Value = {value: map}
fn invalid() Value { return {value:{*:error("spread")}} }
fn relay() Value => invalid()
relay().value
