// Type negation: !T means "any except T"
// Tests for the ! unary operator as type negation

// Basic type negation with is
123 is !string        // true: int is not string
"hello" is !string    // false: string IS string
null is !null         // false: null IS null
42 is !null           // true: int is not null
123 is !int           // false: int IS int
"hi" is !int          // true: string is not int
true is !bool         // false: bool IS bool
3.14 is !float        // false: float IS float
3.14 is !int          // true: float is not int

// Type negation with collections
[1,2,3] is !array     // false: array IS array
[1,2,3] is !map       // true: array is not map
{a:1} is !map         // false: map IS map
{a:1} is !array       // true: map is not array

// logical not still works with 'not' keyword
not true              // false
not false             // true
not (1 == 2)          // true

// combining is and not
not (123 is int)      // false: negation of true
not ("hi" is int)     // true: negation of false

// Type comparison with == and !=
type(123) == int          // true
type(123) != string       // true
type(123) == string       // false
type(123) != int          // false
type("hello") == string   // true
type("hello") != string   // false
type(true) == bool        // true
type(3.14) == float       // true
type([1,2]) == array      // true
type({a:1}) == map        // true
type([1,2]) != map        // true
type({a:1}) != array      // true

// type() result compared with type() result
type(1) == type(2)        // true: both int
type(1) != type("a")     // true: int != string
type(1) == type("a")     // false

// special cases: null and error type comparison
type(null) == null        // true: null type == null
type(null) != null        // false
null != type(null)        // false: commutative
type(error("test")) == error  // true: error type == error
