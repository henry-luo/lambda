// Fixture for keyword_sysfunc_shadowing.ls. S12.3.7: a shadowing definition
// exports like any other, so `pub` extends it to an importing script through
// the explicit import — never ambiently.
pub fn sum(xs) { 99 }
sum([1, 2, 3])
