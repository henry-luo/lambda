// helper for import_type_values.ls: type definitions read by this module's
// own functions, public and private
pub type T = int
type U = string
type Num = T | float
pub fn which() => T
pub fn which2() => U
pub fn num() => Num
pub fn check(x) => x is U
pub fn check_num(x) => x is Num
