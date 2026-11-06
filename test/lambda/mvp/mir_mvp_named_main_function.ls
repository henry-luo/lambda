// S12.1.1v3: only a pn main is a procedural entry; an fn with that name
// remains an ordinary callable, with parameters and defaults.
fn main(value: int = 2) int => value + 1;
[main(), main(4)]
