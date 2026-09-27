// A pattern names only what is defined before it; `dw` is one name, not the
// classes `d w`. It had compiled to nothing, so the pattern matched only "".
type bad_name = \(dw)
