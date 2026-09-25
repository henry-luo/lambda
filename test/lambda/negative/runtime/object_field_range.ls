// S11.4.10 (LR03-20): an object literal admits each field against its declared
// contract. A range field took 9 unchecked.
type Obj { a: 1 to 5 }
"bound: " ++ string(<Obj a: 9>)
