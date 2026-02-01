// Negative test: duplicate function definition
// Expected error: E209 - duplicate definition of 'foo' in the same scope

fn foo() int { 1 }
fn foo() float { 2.0 }  // Error: duplicate definition
