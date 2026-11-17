// S7.7.2v2: a later valid field cannot erase failure of an earlier spread.
type Value = {value: int}
fn invalid() Value { return {*:error("spread"),value:1} }
fn relay() Value => invalid()
relay().value
