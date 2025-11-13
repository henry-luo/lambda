// Lambda Validator Test Data: Primitive Types
// Tests basic type validation for string, int, float, bool, null

// ========== Type Definitions ==========

type StringType = string
type IntType = int
type FloatType = float
type BoolType = bool
type NullType = null

// ========== Valid Test Cases ==========

let valid_string: StringType = "hello world"
let valid_int: IntType = 42
let valid_float: FloatType = 3.14
let valid_bool: BoolType = true
let valid_null: NullType = null

// Edge cases
let empty_string: StringType = ""
let zero_int: IntType = 0
let negative_int: IntType = -100
let zero_float: FloatType = 0.0
let negative_float: FloatType = -2.5
let false_bool: BoolType = false

// ========== Invalid Test Cases (Type Mismatches) ==========

// These should fail validation
let invalid_string_int: StringType = 42          // Error: Expected string, got int
let invalid_int_string: IntType = "not a number" // Error: Expected int, got string
let invalid_float_bool: FloatType = true         // Error: Expected float, got bool
let invalid_bool_null: BoolType = null           // Error: Expected bool, got null
let invalid_null_int: NullType = 0               // Error: Expected null, got int
