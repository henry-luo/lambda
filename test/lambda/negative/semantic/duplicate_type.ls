// Negative test: duplicate type definition
// Expected error: E209 - duplicate definition of 'MyType' in the same scope

type MyType = int
type MyType = float  // Error: duplicate definition
