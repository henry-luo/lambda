// Helper module for constrained_type_predicate.ls: an exported constrained
// type whose predicate holds a string constant of its own module (LR03-25).
pub type Named = string that ~ != "admin"
pub type Short = string that len(~) < 4
