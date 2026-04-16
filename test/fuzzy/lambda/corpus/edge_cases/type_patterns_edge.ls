// Edge cases for type patterns - tests boundary conditions and unusual combinations

// Empty constructs with types
type EmptyArray = []
type EmptyMap = {}
type EmptyTuple = ()
type EmptyElement = <>

// Single element types
type SingleArray = [int]
type SingleMap = {x: int}
type SingleTuple = (int)

// Deeply nested optional types
type DeepOptional = ((((string?)?)?)?)?

// Deeply nested array types
type VeryDeepArray = [[[[[[int]*]*]*]*]*]

// Multiple occurrence operators
type MultiOccurrence = [int+]*
type OptionalMultiple = [string*]?
type ComplexOccurrence = [[int+]*]?

// Union with many alternatives
type ManyUnion = int | string | bool | null | float | int64 | decimal

// Empty vs non-empty variations
type EmptyOrArray = [] | [int+]
type NullOrComplex = null | {a: int, b: [string*]}

// Self-referential patterns (recursive types)
type TreeNode = {value: int, left: TreeNode?, right: TreeNode?}
type LinkedList = {head: int, tail: LinkedList?}

// Function types with edge cases
type VoidFunc = fn() null
type ManyParamFunc = fn(a: int, b: string, c: bool, d: float, e: int64) int
type FuncWithVarArgs = fn(int, ...) int
type HigherOrderFunc = fn(fn(int) int) fn(int) int

// Element with many attributes
type ElementManyAttrs = <div id: string, class: string, style: string, title: string?>

// Element with mixed content
type ElementMixedContent = <section; string | <p; string> | <span; string>>

// Map with many fields
type BigMap = {
    f1: int, f2: string, f3: bool, f4: float,
    f5: int?, f6: string?, f7: bool?, f8: float?
}

// Type operations chained
type ChainedUnion = int | (string | (bool | null))
type ChainedIntersect = {a: int} & ({b: string} & {c: bool})

// Anonymous vs named types
let anon_arr: [int] = [1, 2, 3]
let anon_map: {x: int} = {x: 42}

// Type with special identifiers
type Type_With_Underscore = string
type Type123 = int

// Boundary integer values in types (if applicable)
type BoundaryMap = {min: int, max: int}

// Symbol type patterns
type SymbolArray = [symbol*]
type SymbolMap = {status: symbol}

// Binary type patterns  
type BinaryArray = [binary*]
type BinaryMap = {data: binary}

// DateTime patterns
type DateTimeArray = [datetime+]
type DateRange = {start: datetime, end: datetime}

// Number type (union of int, float, decimal)
type NumericArray = [number*]
type NumericMap = {score: number}

// Complex element nesting
type ComplexDocument = <doc version: string;
    <head; <title; string>, <meta name: string, content: string;>*>?,
    <body; <p; string>*>
>

// Testing type expressions
StringType
IntType
[int] | [string]
{a: int} & {b: string}
fn(int) string

// Mixed type checks
1 is int
"a" is string
null is null
true is bool
[1] is [int]
{x: 1} is {x: int}
