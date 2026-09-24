// @expect-error: E100
// @description: S16.2.3v3: after a function-type signature with no return
// type, a name opening the next line could be the return type or a new
// statement, so neither reading is taken; S11.1.5v2 puts the return type
// on the line of the ')'

type Callback = fn (x: int)
int
