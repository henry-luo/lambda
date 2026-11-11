// Equality between type values in compiled functions (D8.3.1). The MVP compiler's dynamic integer
// speculation passed a TYPE operand's raw Type* carrier to em_guard_finite_int, which accepts only
// an int lane or an Item, and aborted; the guard now reads the boxed Item and falls back to the
// general comparison. The loop makes the functions hot enough to compile under the default tier.
fn same(a, b) => type(a) == type(b)
fn is_text(v) => type(v) == string
let t = type(1)
fn other(v) => type(v) != t
fn hot(k) => len([for (i in 1 to k where same(i, i * 2) and is_text("s") and other(1.5)) i]);
[same(1, 2), same(1, "x"), is_text("a"), is_text(1), other(2), other("a"), hot(20000)]
