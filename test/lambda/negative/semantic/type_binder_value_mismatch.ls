// @expect-error: E207
// S11.4.8v2: admitting dynamic type values does not admit numeric arguments.
fn identity(T: type, value: T) T => value
identity(1, 2)
