// S11.4.10: a required field is always present in an object, so leaving one
// without a value or a default is a compile error; it used to hold null.
type Obj { a: int }
<Obj>
