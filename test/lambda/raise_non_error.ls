// S7.4.6 (LR10-8): `raise v` of a non-error value is shorthand for
// `raise error(v)` -- the error constructor decides its code and message, and
// the error is stamped where `v` is written. The JIT had put the value itself
// on the error lane, so a handler received the string as a success value.
fn f(x) int^ { if (x < 0) raise "s" else x }
fn g(x) int^ { if (x < 0) raise 7 else x }
fn k(x) int^ { if (x < 0) raise null else x }
fn m(x) int^ { if (x < 0) raise {code: 404, message: "missing"} else x }
fn h(x) int^ { if (x < 0) raise error("negative") else x }
let handled = [f(-1) ^ { 0 }, g(-1) ^ { 0 }, k(-1) ^ { -5 }, m(-1) ^ { 2 }, h(-1) ^ { 1 }, f(4) ^ { 0 }]
handled

// the raised value is an ordinary error value: code, message and site
let e = f(-1) ^ { ^ }
let fields = [e is error, e.code, e.message, e.line, e.column]
fields
let others = [(g(-1) ^ { ^ }).message, (m(-1) ^ { ^ }).code, (m(-1) ^ { ^ }).message, (h(-1) ^ { ^ }).message]
others
