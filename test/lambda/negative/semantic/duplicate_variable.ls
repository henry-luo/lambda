// Negative test: duplicate variable definition
// Expected error: E209 - duplicate definition of 'a' in the same scope

let a = 1
let a = 2  // Error: duplicate definition
