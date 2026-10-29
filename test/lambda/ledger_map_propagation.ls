fn failed_field() float^ { raise error("map field failure") }
fn build_map() any^ { {left: 1.0, failed: failed_field()^, right: 2.0} }
let outcome = build_map() ^ { ^.message };
[outcome, {value: error("ordinary data")}.value.message]
