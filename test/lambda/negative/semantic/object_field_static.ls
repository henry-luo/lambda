// S11.4.1v3 (LR03-20): a field value that can never fit its contract is a
// compile error; construction used to store it unchecked.
type Obj { a: int }
<Obj a: "x">
