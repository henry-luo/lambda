// Helper module for constrained_type_predicate.ls: an exported constrained
// type whose predicate holds a string constant of its own module (LR03-25),
// and ones that read a module `let` and call a private function (LR03-27).
pub type Named = string that ~ != "admin"
pub type Short = string that len(~) < 4
let lim = 3
fn dbl(x) => x * 2
pub type AtLim = int that ~ == lim
pub type Doubled = int that dbl(~) > 6
