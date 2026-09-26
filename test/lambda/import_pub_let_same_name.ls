// Two imported modules export a `pub let` of the same name: each import must
// read its own module's value, and each module's functions its own binding.
import a: .mod_same_let_a
import b: .mod_same_let_b

[a.label, b.label];
[len(a.sizes), len(b.sizes)];
[a.who(), b.who()];
[a.framed(), b.framed()]
