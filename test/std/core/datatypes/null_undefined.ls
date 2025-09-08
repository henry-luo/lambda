// Test: Null and Undefined Values
// Category: core/datatypes
// Type: positive
// Expected: 1

// Null and undefined values
let n = null
n
let u = undefined
u
let eq_null = n == null      
eq_null
let eq_undefined = u == null 
eq_undefined
let strict_eq = n === null   
strict_eq
let strict_neq = n === u     
strict_neq
let is_null = n === null     
is_null
let is_undefined = u === undefined 
is_undefined
let default1 = n ?? "default"  
default1
let default2 = u ?? 42         
default2
let default3 = 0 ?? "default"  
default3
let obj = {a: {b: 42}}
obj
let value1 = obj?.a?.b     
value1
let value2 = obj?.x?.y     
value2
1
