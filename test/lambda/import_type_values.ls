// A type definition read inside a function, and a call whose result is a type
// value, give that type on every tier (D8.1.1v17). The eager JIT gave a
// `pub type` a module slot that captured its read in the module's functions
// (a `pub grammar`'s slot was never written), and a caller unboxed a call
// typed as a type value as the value the type names: `0` for `int`, null for
// `string`.
import .mod_type_values

// imported functions reading their own module's type definitions
which()
which2()
check("s")
check(1)
check_num(2)
check_num("2")
let n = num()
1.5 is n
"x" is n
type(which())
which() == int

// local functions whose result is a type value
pub type V = bool
type L = float
let t = int
fn f1() => int
fn f2() => t
fn f3() => L
fn f4() => [L]
fn f5() => V
f1()
f2()
f3()
f4()
f5()
V
let a = f3()
a
type(a)
let types = [f1(), f3()]
types
let k = f1()
5 is k
