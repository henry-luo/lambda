## CLI Commands
```bash
lambda                    # Start REPL
// REPL Commands: .quit, .help, .clear
lambda script.ls          # Eval functional script
lambda run script.ls      # Run procedual script
lambda --transpile-only script.ls # Transpile only
lambda --help             # Show help
```

**Validation:**
```bash
lambda validate file.json -s schema.ls  # With schema
lambda validate file.json            # Default schema
```

## Type System

**Scalar Types:**
```lambda
null  bool  int  float  decimal
string  symbol  binary  datetime
```

**Container Types:**
```lambda
range, 1 to 10          // Range (inclusive both ends)
array, [123, true]      // Array of values
list, (0.5, "string")   // List
map, {key: 'symbol'}    // Map
object, {point x: 1, y: 2}  // Object (nominally-typed)
element, <div class: bold; "text" <br>>  // Element
```

**Type Operators:**
```lambda
int | string     // Union type
int & number     // Intersection
int?             // Optional (int | null)
int*             // Zero or more
int+             // One or more
int[]            // Array of ints (same as int*)
int[5]           // Array of exactly 5 ints
[int*]           // Bracket form: array of 0+ ints
[int+]           // Bracket form: array of 1+ ints
float[]          // Array of floats
fn (a: int, b: string) bool   // Function type
fn int                        // Same as fn () int
{a: int, b: bool}             // Map type
<div id:symbol; <br>>         // Element type
```

**Type Declarations:**
```lambda
type User = {name: string, age: int};   // Map type alias
type Point = (float, float);            // Tuple type
type Result = int | error;              // Union type
```

## Object Types

**Definition:**
```lambda
type Point { x: float, y: float }     // Fields only
type Counter {
    value: int = 0;                   // Default value
    fn double() => value * 2          // Functional
    fn add(n: int) => value + n
    pn inc() { value = value + 1 }    // Procedural
}
type Circle : Shape { radius: float; }  // Inheritance
```

**Literals & Access:**
```lambda
let p = {Point x: 1.0, y: 2.0}   // Object literal
let c = {Counter}                // All defaults
p.x                              // Field access
c.double()                       // Method call
{Point p, x: 5.0}                // Wrap and override
```

**Type Checking (nominal only):**
```lambda
p is Point     // true
p is object    // true
p is map       // true (objects are map-compatible)
{x: 1} is Point  // false (plain maps don't match)
```

**Constraints:**
```lambda
type User {
  name: string that (len(~) > 1),  // Field constraint
  age: int that (0 <= ~ <= 150);
  that (~.name != "admin")         // Object constraint
}
```

**Self reference `~`:**
```lambda
type Vec { x: float, y: float;
  fn len() => math.sqrt(x**2 + y**2)
  fn scale(f) => {Vec ~, x: ~.x*f, y: ~.y*f}  // ~ = self
}
```

## Literals

**Numbers:**
```lambda
42        // Integer
3.14      // Float
1.5e-10   // Scientific notation
123.45n   // Decimal128 (~34 digits)
123.45N   // Decimal (ultra precision, 200 digits)
inf  nan  // Special values
```

**Strings & Symbols:**
```lambda
"hello"           // String
"multi-line       // Multi-line string
string"
'symbol'          // Symbol
symbol            // Unquoted symbol
```

**Binary:**
```lambda
b'\xDEADBEEF'     // Hex binary
b'\64QUVGRw=='    // Base64 binary
```

**DateTime:**
```lambda
t'2025-01-01T14:30:00Z'  // DateTime
t'2025-04-26' is date       // Sub-types: date
t'10:30:00' is time         // Sub-types: time

// Member properties
dt.date  dt.year  dt.month  dt.day
dt.time  dt.hour  dt.minute dt.second  dt.millisecond
dt.weekday  dt.yearday  dt.week  dt.quarter
dt.unix  dt.timezone  dt.utc_offset  dt.utc  dt.local

// Formatting
dt.format("YYYY-MM-DD")  dt.format('iso')

// Constructors
datetime()  today()  justnow()   // current date/time
datetime(2025, 4, 26, 10, 30)
date(2025, 4, 26)  time(10, 30, 45)
```

**Collections:**
```lambda
[1, 2, 3]         // Array
(1, "two", 3.0)   // List
{a: 1, b: 2}      // Map
<div id: "main">  // Element
```

**Indexing & Slicing:**
```lambda
arr[0]            // First element
arr.0             // Alt. syntax for const index
arr[1 to 3]       // Slice (indices 1, 2, 3)
map.key           // Map field access
map["key"]        // Map field by string
```

**Namespaces (via `import` with bare URI):**
```lambda
import svg: 'http://www.w3.org/2000/svg'
import xlink: 'http://www.w3.org/1999/xlink'

<svg.rect svg.width: 100>   // Namespaced tag & attr
// desugars to: <svg.rect svg: {width: 100}>
elem.svg.width              // Chained sub-map access
elem.svg                    // {width: 100} sub-map
svg.rect                    // Qualified symbol
symbol("href", 'xlink_url') // Dynamic namespaced symbol
```

## Variables & Declarations

**Let Expressions:**
```lambda
(let x = 5, x + 1, x * 2)      // Single binding
(let a = 1, b = 2, a + b)      // Multiple bindings
```

**Let Statements (immutable):**
```lambda
let x = 42;               // Immutable binding
let y : int = 100;        // With type annotation
let a = 1, b = 2;         // Multiple bindings
x = 10       // ERROR E211: cannot reassign let binding
```

**Var Statements (mutable, `pn` only):**
```lambda
var x = 0;         // Mutable variable
var y: int = 42;   // With type annotation
var a: int[] = [1, 2, 3];  // Array of ints
var b: float[] = [0.1, 0.2]; // Array of floats
x = x + 1          // OK: reassignment
x = "hello"        // OK: type widening (int → string)
y = "oops"      // ERROR E201: annotated type enforced
```

## Operators

**Arithmetic:** addition, subtraction, multiplication, division, integer division, modulo, exponentiation
```lambda
+  -  *  /  div  %  **
```

**Spread:** *
```lambda
let a = [1, 2, 3]
(*a, *[10, 20])    // (1, 2, 3, 10, 20)
```

**Comparison:** equal, not equal, less than, less equal, greater than, greater equal
```lambda
==  !=  <  <=  >  >=
```

`==` performs **structural deep equality** on all types:
```lambda
[1, 2] == [1, 2]             // true  (list/array)
{a: 1, b: 2} == {b: 2, a: 1} // true  (map, order-independent)
[1] == [1.0]                  // true  (numeric promotion)
(1 to 3) == [1, 2, 3]         // true  (cross-type sequence)
```

**Logical:** logical and, or, not
```lambda
and  or  not
```

**Type & Set:** type check, membership, range, union, intersection, exclusion
```lambda
is  in  to  |  &  !
```

**Query:** type-based search
```lambda
?   .?           // recursive descendant search
expr[T]          // child-level query (direct only)
```

**Vector Arithmetic:** scalar broadcast, element-wise ops
```lambda
1+[2,3] = [3,4]  [1,2]*2 = [2,4]  [1,2]+[3,4] = [4,6]
```

## Pipe Expressions

**Pipe `|` with current item `~`:**
```lambda
[1,2,3] | ~ * 2          // [2,4,6] - map over items
[1,2,3] | sum            // 6 - aggregate (no ~)
users | ~.age            // [12,20,62] - extract field
['a','b'] | {i:~#, v:~}  // ~# = index/key
```

**Filter with `where`:**
```lambda
[1,2,3,4,5] where ~ > 3         // [4,5]
users where ~.age >= 18 | ~.name // filter then map
[1,2,3] | ~ ** 2 where ~ > 5 | sum // 13 (4+9)
```

## Query Expressions

**Results in document order** (depth-first, pre-order).

**Query `?` — attributes + all descendants:**
```lambda
html?<img>                // all <img> at any depth
html?<div class: string>  // <div> with class attr
data?int                  // all int values in tree
data?(int | string)       // all int or string values
data?{name: string}       // maps with string 'name'
data?{status: "ok"}       // maps where status == "ok"
```

**Self-inclusive query `.?` — self + attributes + all descendants:**
```lambda
div.?<div>     // includes div itself if it matches
el.?int        // self + all int values in subtree
42.?int        // [42] — trivial self-match
```

**Child-level query `[T]` — direct attributes + children only (no recursion):**
```lambda
[1, "a", 3, true][int]   // [1, 3] — int items
{name: "Alice", age: 30}[string]  // ["Alice"]
el[element]     // direct child elements only
el[string]      // attr values + text children
```

## Pipe to File (procedural only)
```lambda
// Target can be string, symbol, or path
data |> 'output.txt'        // file under CWD
data |> /tmp.'output.txt'   // output at full path
data |>> "output.txt"       // append to file
data | format('json') |> "output.json"
```
Data type determines output format:

- String: raw text (no formatting)
- Binary: raw binary data
- Other types: Lambda/Mark format

## Control Flow

**If Expressions (parenthesized condition, else required):**
```lambda
if (x > 0) "positive" else "non-positive"
if (score >= 90) "A"
else if (score >= 80) "B" else "C"
if (x > 0) "pos" else { log("neg"); "neg" } // block else
```

**If Statements (block body, else optional):**
```lambda
if x > 0 { "positive" }
if condition { something() } else { otherThing() }
if x > 0 { compute() } else "default"   // expr else
```

Both forms share the same `else` syntax: `else expr`, `else { stam }`, or `else if ...`.

**Match Expressions:**
```lambda
// Type patterns or Literal
match value {
    case 200: "OK"                // case literal
    case string: "text"           // case type
    case int | float: "number"    // case type union
    case Circle:                   // case object type
        3.14 * ~.r ** 2
    default: "other"
}

// String pattern arms (full-match semantics)
string digits = \d+
string alpha = \a+
match input {
    case digits: "number"         // case named pattern
    case alpha: "word"
    default: "other"
}
```

**For Expressions:** (produce spreadable arrays)
```lambda
for (x in [1, 2, 3]) x * 2     // [2, 4, 6]
for (i in 1 to 5) i * i        // [1, 4, 9, 16, 25]
for (x in data) if (x > 0) x else 0  // Conditional
// Nested for-expressions flatten
[for (i in 1 to 2) for (j in 1 to 2) i*j]  // [1,2,2,4]
// Empty for produces spreadable null (skipped)
[for (x in [] ) x]             // []

// for loop over map by keys
for (k at {a: 1, b: 2}) k      // "a", "b"
for (k, v at {a: 1, b: 2}) k ++ v  // ["a1", "b2"]
```

**For Expression Clauses:** `let`, `where`, `order by`, `limit`, `offset`
```lambda
for (x in data where x > 0) x           // filter
for (x in data, let sq = x*x) sq        // let binding
for (x in [3,1,2] order by x) x         // (1,2,3)
for (x in [3,1,2] order by x desc) x    // (3,2,1)
for (x in data limit 5 offset 10) x     // pagination
for (x in data, let y=x*2
    where y>5 order by y desc limit 3) y
```

**For Statements:**
```lambda
for item in collection { transform(item) }
```

**Procedural Control (in `pn`):**
```lambda
var x=0;   // Mutable variable
while(c) { break;  continue;  return x; }
```

**Assignment Targets:**
```lambda
x = 10              // Variable reassignment (var only)
arr[i] = val        // Array element reassignment
obj.field = val     // Map field reassignment
elem.attr = val     // Element attribute reassignment
elem[i] = val       // Element child reassignment
```

## Functions

**Function Declaration:**
```lambda
// Function with statement body
fn add(a: int, b: int) int { a + b }
// Function  with expression body
fn multiply(x: int, y: int) => x * y
// Anonymous function
let square = (x) => x * x;
// Procedural function
pn f(n) { var x=0; while(x<n) {x=x+1}; x }
// Array parameters
pn advance(pos: float[], vel: float[], n: int) { ... }
```

**Advanced Features:**
```lambda
fn f(x?:int)    // optional param
fn f(x=10)      // default param value
fn f(...)       // variadic args
f(b:2, a:1)     // named param call
fn outer(n) { fn inner(x)=>x+n; inner } // closure
```

## String Patterns

Define named patterns for string validation and matching. Uses regex-like syntax integrated into the type system.

**Definition:**
```lambda
string digits = \d+                    // one or more digits
string email = \w+ "@" \w+ "." \a[2,6] // email-like
string ws = \s+                        // whitespace
symbol keyword = 'if' | 'else' | 'for' // symbol pattern
```

**Type check (`is`) — full-match semantics:**
```lambda
"hello@world.com" is email    // true
"abc" is email                // false
"123" is digits               // true
```

**Character classes:** `\d` digit, `\w` word, `\s` whitespace, `\a` alpha, `\.` any char, `...` any string

**Quantifiers:** `?` optional, `+` one or more, `*` zero or more, `[n]` exactly n, `[n,m]` range

## System Functions

**Type:**

`int(v)` `int64(v)` `float(v)` `decimal(v)` `string(v)` `symbol(v)` `binary(v)` `number(v)` `type(v)` `len(v)`

**Math:**

`math.pi` `math.e` `abs(x)` `sign(x)` `min(a,b)` `max(a,b)` `round(x)` `floor(x)` `ceil(x)` `math.trunc(x)` `math.sqrt(x)` `math.cbrt(x)` `math.hypot(x,y)` `math.pow(b,e)` `math.log(x)` `math.log2(x)` `math.log10(x)` `math.log1p(x)` `math.exp(x)` `math.exp2(x)` `math.expm1(x)` `math.sin(x)` `math.cos(x)` `math.tan(x)` `math.asin(x)` `math.acos(x)` `math.atan(x)` `math.atan2(y,x)` `math.sinh(x)` `math.cosh(x)` `math.tanh(x)` `math.asinh(x)` `math.acosh(x)` `math.atanh(x)`

**Stats:**

`sum(v)` `avg(v)` `math.mean(v)` `math.median(v)` `math.variance(v)` `math.deviation(v)` `math.quantile(v,p)` `math.prod(v)`

**Date/Time:**

`datetime()` `today()` `now()` `justnow()` `date(dt)` `time(dt)`

**Range:**

`s to e` creates a range from `s` to `e` (inclusive both ends). `range(s,e,step)` creates a range with custom step (exclusive end).
```lambda
1 to 5                 // [1, 2, 3, 4, 5]
range(0, 10, 2)        // [0, 2, 4, 6, 8]
```

**String:**

`replace(str,old,new)` `split(str,sep)` `join(strs,sep)` `find(str,pattern)` `normalize(str)`

All three accept both plain strings and named patterns as the second argument:
```lambda
string digit = \d
string digits = \d+
string ws = \s+

// replace(str, pattern_or_string, replacement)
replace("a1b2c3", digit, "X")        // "aXbXcX"
replace("hello   world", ws, " ")    // "hello world"
replace("abc", "b", "")              // "ac"

// split(str, pattern_or_string)
split("a1b2c3", digit)               // ["a", "b", "c", ""]
split("hello   world", ws)           // ["hello", "world"]
split("a,b,c", ",")                  // ["a", "b", "c"]
split("a1b2c3", digit, true)         // ["a", "1", "b", "2", "c", "3", ""] — keep delimiters

// find(str, pattern_or_string) → [{value, index}, ...]
find("a1b22c333", digits)            // [{value: "1", index: 1}, {value: "22", index: 3}, ...]
find("hello world", "lo")            // [{value: "lo", index: 3}]
```

**Collection:**

`slice(v,i,j)` `set(v)` `all(v)` `any(v)` `reverse(v)` `sort(v)` `unique(v)` `concat(a,b)` `take(v,n)` `drop(v,n)` `zip(a,b)` `fill(n,x)` `range(s,e,step)` `map(f,v)` `filter(f,v)` `reduce(f,v,init)`

**Vector:**

`math.dot(a,b)` `math.norm(v)` `math.cumsum(v)` `math.cumprod(v)` `argmin(v)` `argmax(v)` `diff(v)`

**I/O:**

`input(file,fmt)` `format(data,fmt)` `print(v)` `output(data,file)` `fetch(url,opts)` `cmd(c,args)` `error(msg)` `varg()`

## Input/Output Formats

**Supported Input Types:** `json`, `xml`, `yaml`, `markdown`, `csv`, `html`, `latex`, `toml`, `rtf`, `css`, `ini`, `math`, `pdf`
```lambda
input("path/file.md", 'markdown')   // Input Markdown
```

**Input with Flavors:** e.g. math flavors: `latex`, `typst`, `ascii`
```lambda
input("math.txt", {'type':'math', 'flavor':'ascii'})
```

**Output Formatting:** `json`, `yaml`, `xml`, `html`, `markdown`
```lambda
format(data, 'yaml')                // Format as YAML
```

## Modules, Imports & Exports

**Import Syntax:**
```lambda
import .relative_module          // Relative to script's directory
import .path.to.module           // Nested relative import
import module_name               // Relative to CWD/project root
import alias: .module            // Import with alias
```

**Export Declarations:**
```lambda
pub PI = 3.14159                 // Export variable
pub fn square(x) => x * x       // Export function
pub pn log(msg) { print(msg) }  // Export procedure
pub type Score = int             // Export type alias
pub type Counter {               // Export object type
    value: int = 0;
    fn double() => value * 2
}
pub data^err = input("f", 'json) // Export with error var
```

**Module Usage:**
```lambda
// In math_utils.ls:
pub PI = 3.14159
pub type Vec2 { x: float, y: float; fn len() => math.sqrt(x**2 + y**2) }

// In main.ls:
import .math_utils
let area = PI * r ** 2
let v = {Vec2 x: 3.0, y: 4.0}
v.len()        // 5.0
v is Vec2      // true
```
## Error Handling

**Creating Errors:**
```lambda
error("Message")    error("load failed", inner_err)
error({code: 304, message: "div by zero"})
```

**Error Return Types (`T^E`):**
```lambda
fn parse(s: string) int^ {...}   // Return int or any error
fn divide(a, b) int ^ DivErr {...}     // Specific error
fn load(p) Config ^ ParseErr|IOErr {...} // Multiple errors
```

**`raise` error , or propagate error with `^`**
```lambda
fn compute(x: int) int^ {
  if (b == 0) raise error("div by zero")  // raise error
  let a = parse(input)^    // return immediately on error
  let b = divide(a, x)^    // return immediately on error
  a + b
}
fun()^               // propagate error, discard value
```

**`let a^err` — destructure value and error:**
```lambda
let result^err = divide(10, x)
if (^err) print(err.message)  // ^err to check error
else result * 2
```

## Operator Precedence (High to Low)
1. `()` `[]` `.` `?` `.?` - Primary, query
2. `-` `+` `not` `!` - Unary (`not`: logical NOT, `!`: type negation)
3. `**` - Exponentiation
4. `*` `/` `div` `%` - Multiplicative
5. `+` `-` - Additive
6. `<` `<=` `>` `>=` - Relational
7. `==` `!=` - Equality
8. `and` - Logical AND
9. `or` - Logical OR
10. `to` - Range
11. `is` `in` - Type operations (`is nan` for NaN detection)
12. `|` `where` - Pipe and Filter

## Quick Examples

**Data Processing:**
```lambda
let data = input("sales.json", 'json')
let total = sum(
  (for (sale in data.sales) sale.amount))
let report = {total: total,
  count: len(data.sales)}
format(report, 'json')
```

**Function Definition:**
```lambda
fn factorial(n: int) int {
    if (n <= 1) 1 else n * factorial(n - 1)
}
```

**Element Creation:**
```lambda
let article = <article title:"My Article"
    <h1 "Introduction">
    <p "Content goes here.">
>
format(article, 'html')
```

**Comprehensions - Complex data processing:**
```lambda
(let data = [1, 2, 3, 4, 5],
 let filtered = (for (x in data)
   if (x % 2 == 0) x else 0),
 let doubled = (for (x in filtered) x * 2), doubled)
```
