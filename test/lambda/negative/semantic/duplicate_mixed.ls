// Negative test: duplicate definition - mixed kinds (variable and type)
// Expected error: E209 - duplicate definition of 'foo' in the same scope

let foo = 1
type foo = int  // Error: duplicate definition (type with same name as variable)
