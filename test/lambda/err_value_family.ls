// Formal semantics 7.6: errors fall into three families.
//   1. type family   -- `is error`, `type`, `match`, and `print` -- participate
//   2. truthy family -- `if`, `not`, `or`, `==` -- participate
//   3. value family  -- everything else PROPAGATES
//
// This pins family 3, which is where errors are easiest to lose. Each of these
// once absorbed the error and answered something plausible instead.

fn fail() int^ { raise error("boom") }

'-- text and name conversions propagate; they do not render the error --'
fn t_string() { let a^err = fail(); type(string(err)) }
t_string()
fn t_symbol() { let a^err = fail(); type(symbol(err)) }
t_symbol()
fn t_name()   { let a^err = fail(); type(name(err)) }
t_name()

'-- membership propagates: `in` searches one level, so `false` would mislead --'
fn t_in() { let a^err = fail(); type(err in [1, err, 3]) }
t_in()
fn t_at() { let a^err = fail(); type(err at {k: 1}) }
t_at()

'-- len is a value function too (8.1: iterating an error yields an error) --'
fn t_len() { let a^err = fail(); type(len(err)) }
t_len()

'-- but the truthy family still participates: these are the discharge surfaces --'
fn t_truthy() {
    let a^err = fail()
    [if (err) "T" else "F", not err, err or "dflt", err == err]
}
t_truthy()

'-- and containment is not propagation: an error inside a collection is data --'
fn t_contain() { let a^err = fail(); len([1, err, 3]) }
t_contain()
