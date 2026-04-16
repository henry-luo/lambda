// Type pattern test corpus for fuzzy testing
// Covers basic types, compound types, union/intersection, occurrence operators

// Basic primitive types
type StringType = string
type IntType = int  
type BoolType = bool
type FloatType = float
type NullType = null
type Int64Type = int64
type DecimalType = decimal
type DateTimeType = datetime
type SymbolType = symbol
type BinaryType = binary

// Optional types with ?
type OptionalString = string?
type OptionalInt = int?
type OptionalBool = bool?
type OptionalFloat = float?

// Array types with different occurrence operators
type SimpleArray = [int]
type ZeroMoreArray = [string*]
type OneMoreArray = [int+]
type OptionalItemArray = [string?]
type NestedArray = [[int]*]
type DeepNestedArray = [[[string*]*]+]

// Map types
type SimpleMap = {name: string}
type MultiFieldMap = {name: string, age: int, active: bool}
type OptionalFieldMap = {required: string, optional: int?}
type NestedMap = {person: {name: string, addr: {city: string}}}
type MapWithArrays = {tags: [string*], scores: [int+]}

// List/tuple types
type SimpleTuple = (int, string)
type MixedTuple = (int, string, bool, null)
type NestedTuple = ((int, string), (bool, float))
type OptionalTuple = (int, string?)?

// Element types
type SimpleElement = <div>
type ElementWithAttr = <a href: string>
type ElementWithContent = <p; string>
type ElementWithBoth = <div class: string; string>
type NestedElement = <article; <header; string>, <section; string>*>

// Union types
type StringOrInt = string | int
type MultiUnion = string | int | bool | null
type UnionWithOptional = (string | int)?
type UnionOfArrays = [int] | [string]
type ComplexUnion = {a: int} | {b: string} | [int*]

// Intersection types
type BasicIntersection = {a: int} & {b: string}
type MultiIntersection = {a: int} & {b: string} & {c: bool}

// Combined union and intersection
type MixedOperators = (string | int) & bool
type ComplexMixed = ({a: int} | {b: string}) & {c: bool}

// Function types
type SimpleFunc = fn(int) int
type MultiParamFunc = fn(int, string) bool
type OptionalParamFunc = fn(int, string?) int
type FuncReturningFunc = fn(int) fn(string) bool
type ArrayOfFuncs = [fn(int) int]
type MapWithFuncs = {handler: fn(string) int}

// Type references and aliases
type BaseType = string
type AliasType = BaseType
type CompoundAlias = [AliasType+]

// Complex nested patterns
type ComplexType = {
    id: int,
    name: string?,
    tags: [string*],
    metadata: {
        created: datetime?,
        modified: datetime?
    }?,
    children: [ComplexType*]?
}

// Numeric type
type NumberType = number

// Test instantiation
let str: StringType = "hello"
let arr: SimpleArray = [1, 2, 3]
let map_val: SimpleMap = {name: "test"}

// Type checks
"hello" is string
123 is int
[1, 2, 3] is [int]
{name: "test"} is {name: string}

// Type patterns in expressions
fn check_type(x) => x is string
fn typed_param(x: int) => x + 1
fn returns_typed(): string => "result"
