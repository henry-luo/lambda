# Lambda System Functions Reference

This document provides comprehensive documentation for all built-in system functions in Lambda Script.

## Table of Contents

1. [Type Functions](#type-functions)
2. [Mathematical Functions](#mathematical-functions)
3. [Statistical Functions](#statistical-functions)
4. [Date/Time Functions](#datetime-functions)
5. [String Functions](#string-functions)
6. [Collection Functions](#collection-functions)
7. [Aggregation & Reduction Functions](#aggregation--reduction-functions)
8. [Variadic Argument Functions](#variadic-argument-functions)
9. [Input and Format Functions](#io-functions)
10. [Procedural Functions](#procedural-functions)
11. [Error Handling](#error-handling)

---

## Type Functions

Functions for type conversion and inspection.

### Type Conversion

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `int(x)` | Convert to integer | `int("42")` | `42` |
| `int64(x)` | Convert to 64-bit integer | `int64("9999999999")` | `9999999999` |
| `float(x)` | Convert to float | `float("3.14")` | `3.14` |
| `decimal(x)` | Convert to arbitrary precision decimal | `decimal("123.456")` | `123.456n` |
| `string(x)` | Convert to string | `string(42)` | `"42"` |
| `symbol(x)` | Convert to symbol | `symbol("text")` | `'text` |
| `binary(x)` | Convert to binary | `binary("hello")` | `b'...'` |
| `number(x)` | Convert to number (int or float) | `number("3.14")` | `3.14` |

### Type Inspection

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `type(x)` | Get type of value | `type(42)` | `'int` |
| `name(x)` | Get name of element, function, or type | `name(<div>)` | `'div` |
| `len(x)` | Get length of collection | `len([1, 2, 3])` | `3` |

```lambda
// Type conversion examples
int("42")          // 42
float("3.14")      // 3.14
string(42)         // "42"
symbol("text")     // 'text

// Type inspection
type(42)           // 'int
type("hello")      // 'string
type([1, 2, 3])    // 'array
name(<div>)        // 'div
name(type(42))     // 'int
len([1, 2, 3])     // 3
len("hello")       // 5
```

---

## Mathematical Functions

Basic mathematical operations.

> **Import Styles:** The `math` module supports three import styles:
> - **No import** (default): `math.sqrt(x)`, `math.pi` — always available
> - **Global import** (`import math;`): `sqrt(x)`, `pi` — all functions available without prefix
> - **Aliased import** (`import m:math;`): `m.sqrt(x)`, `m.pi` — use custom prefix
>
> Standalone functions like `abs`, `round`, `floor`, `ceil`, `sign`, `min`, `max`, `sum` are always available without any prefix or import.

### Scalar Math

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `abs(x)` | Absolute value | `abs(-5)` | `5` |
| `round(x)` | Round to nearest integer | `round(3.7)` | `4` |
| `floor(x)` | Round down | `floor(3.7)` | `3` |
| `ceil(x)` | Round up | `ceil(3.2)` | `4` |
| `sign(x)` | Sign of number (-1, 0, 1) | `sign(-5)` | `-1` |

### Min/Max

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `min(a, b)` | Minimum of two values | `min(3, 5)` | `3` |
| `min(vec)` | Minimum in collection | `min([3, 1, 2])` | `1` |
| `max(a, b)` | Maximum of two values | `max(3, 5)` | `5` |
| `max(vec)` | Maximum in collection | `max([3, 1, 2])` | `3` |
| `argmin(vec)` | Index of minimum | `argmin([3, 1, 2])` | `1` |
| `argmax(vec)` | Index of maximum | `argmax([3, 1, 2])` | `0` |

```lambda
abs(-5)            // 5
round(3.7)         // 4
floor(3.7)         // 3
ceil(3.2)          // 4
sign(-5)           // -1

min(3, 5)          // 3
min([3, 1, 2])     // 1
max(3, 5)          // 5
max([3, 1, 2])     // 3
argmin([5, 2, 8, 1])  // 3 (index of 1)
argmax([5, 2, 8, 1])  // 2 (index of 8)
```

---

## Statistical Functions

Functions for statistical analysis on collections.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `sum(vec)` | Sum of elements | `sum([1, 2, 3])` | `6` |
| `avg(vec)` | Arithmetic mean | `avg([1, 2, 3])` | `2.0` |
| `math.mean(vec)` | Alias for avg | `math.mean([1, 2, 3])` | `2.0` |
| `math.median(vec)` | Median value | `math.median([1, 3, 2])` | `2` |
| `math.variance(vec)` | Population variance | `math.variance([1, 2, 3])` | `0.666...` |
| `math.deviation(vec)` | Standard deviation | `math.deviation([1, 2, 3])` | `0.816...` |
| `math.quantile(vec, p)` | p-th quantile | `math.quantile([1,2,3,4], 0.5)` | `2.5` |
| `math.prod(vec)` | Product of elements | `math.prod([2, 3, 4])` | `24` |
| `math.cumsum(vec)` | Cumulative sum | `math.cumsum([1, 2, 3])` | `[1, 3, 6]` |
| `math.cumprod(vec)` | Cumulative product | `math.cumprod([1, 2, 3])` | `[1, 2, 6]` |
| `math.dot(a, b)` | Dot product | `math.dot([1,2,3], [4,5,6])` | `32` |
| `math.norm(vec)` | Euclidean norm | `math.norm([3, 4])` | `5` |

```lambda
sum([1, 2, 3, 4])           // 10
avg([1, 2, 3, 4])           // 2.5
math.mean([1, 2, 3, 4])          // 2.5
math.median([1, 3, 2, 4, 5])     // 3
math.variance([1, 2, 3])         // 0.666...
math.deviation([1, 2, 3])        // 0.816...
math.quantile([1, 2, 3, 4], 0.5) // 2.5
math.prod([2, 3, 4])             // 24

// Cumulative & linear algebra
math.cumsum([1, 2, 3, 4])       // [1, 3, 6, 10]
math.cumprod([1, 2, 3, 4])      // [1, 2, 6, 24]
math.dot([1, 2, 3], [4, 5, 6])  // 32 (1*4 + 2*5 + 3*6)
math.norm([3, 4])               // 5 (sqrt(9 + 16))
```

---

## Element-wise Math Functions

Functions that apply to each element of a collection and return a collection of the same size. Also work on scalar values.

### Constants

| Constant | Description | Value |
|----------|-------------|-------|
| `math.pi` | Pi (π) | `3.1415926536` |
| `math.e` | Euler's number (e) | `2.7182818285` |

### Trigonometric

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `math.sin(x)` | Sine | `math.sin([0, 1.57...])` | `[0, 1]` |
| `math.cos(x)` | Cosine | `math.cos([0, 1.57...])` | `[1, 0]` |
| `math.tan(x)` | Tangent | `math.tan([0, 0.785...])` | `[0, 1]` |

### Inverse Trigonometric

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `math.asin(x)` | Inverse sine (arcsin) | `math.asin(1)` | `1.5707963268` |
| `math.acos(x)` | Inverse cosine (arccos) | `math.acos(1)` | `0` |
| `math.atan(x)` | Inverse tangent (arctan) | `math.atan(1)` | `0.7853981634` |
| `math.atan2(y, x)` | Two-argument arctan | `math.atan2(1, 1)` | `0.7853981634` |

### Hyperbolic

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `math.sinh(x)` | Hyperbolic sine | `math.sinh(1)` | `1.1752011936` |
| `math.cosh(x)` | Hyperbolic cosine | `math.cosh(0)` | `1` |
| `math.tanh(x)` | Hyperbolic tangent | `math.tanh(1)` | `0.761594156` |

### Inverse Hyperbolic

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `math.asinh(x)` | Inverse hyperbolic sine | `math.asinh(1)` | `0.881373587` |
| `math.acosh(x)` | Inverse hyperbolic cosine | `math.acosh(2)` | `1.3169578969` |
| `math.atanh(x)` | Inverse hyperbolic tangent | `math.atanh(0.5)` | `0.5493061443` |

### Exponential / Logarithmic

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `math.exp(x)` | Exponential (e^x) | `math.exp([0, 1, 2])` | `[1, e, e²]` |
| `math.exp2(x)` | Base-2 exponential (2^x) | `math.exp2(3)` | `8` |
| `math.expm1(x)` | exp(x) - 1 (precise for small x) | `math.expm1(0)` | `0` |
| `math.log(x)` | Natural logarithm | `math.log([1, 2.718...])` | `[0, 1]` |
| `math.log2(x)` | Base-2 logarithm | `math.log2(8)` | `3` |
| `math.log10(x)` | Base-10 logarithm | `math.log10([1, 10, 100])` | `[0, 1, 2]` |
| `math.log1p(x)` | log(1 + x) (precise for small x) | `math.log1p(0)` | `0` |

### Power / Root

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `math.pow(b, e)` | Power (b^e) | `math.pow(2, 10)` | `1024` |
| `math.sqrt(x)` | Square root | `math.sqrt([1, 4, 9])` | `[1, 2, 3]` |
| `math.cbrt(x)` | Cube root | `math.cbrt(27)` | `3` |
| `math.hypot(x, y)` | Hypotenuse √(x²+y²) | `math.hypot(3, 4)` | `5` |

### Rounding / Sign

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `abs(x)` | Absolute value | `abs([-1, 2, -3])` | `[1, 2, 3]` |
| `round(x)` | Round | `round([1.4, 1.6])` | `[1, 2]` |
| `floor(x)` | Floor | `floor([1.7, 2.3])` | `[1, 2]` |
| `ceil(x)` | Ceiling | `ceil([1.2, 2.8])` | `[2, 3]` |
| `math.trunc(x)` | Truncate toward zero | `math.trunc([-3.7, 3.7])` | `[-3, 3]` |
| `sign(x)` | Sign (-1, 0, 1) | `sign([-5, 0, 3])` | `[-1, 0, 1]` |

```lambda
math.pi                         // 3.1415926536
math.e                          // 2.7182818285
math.sin([0, 3.14159/2])        // [0, 1]
math.cos([0, 3.14159/2])        // [1, 0]
math.asin(1)                    // 1.5707963268 (π/2)
math.atan2(1, 1)                // 0.7853981634 (π/4)
math.sinh(1)                    // 1.1752011936
math.exp2(3)                    // 8
math.log2(8)                    // 3
math.pow(2, 10)                 // 1024
math.cbrt(27)                   // 3
math.hypot(3, 4)                // 5
math.log1p(0)                   // 0
math.trunc(3.7)                 // 3
abs([-1, 2, -3])                // [1, 2, 3]
sign([-5, 0, 3])                // [-1, 0, 1]
```

### Random Number Generation

Pure functional pseudo-random number generator using the SplitMix64 algorithm. Takes an integer seed and returns a list `[value, new_seed]` where `value` is a float in [0.0, 1.0).

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `math.random(seed)` | Generate random float and new seed | `let x, s = math.random(42)` | `[0.7415..., -7046...]` |

```lambda
// basic usage — returns [float_value, new_seed]
let x, seed1 = math.random(42)
x          // 0.7415648788 (float in [0.0, 1.0))

// chain the seed for subsequent values
let x2, seed2 = math.random(seed1)
let x3, seed3 = math.random(seed2)

// deterministic: same seed always produces same result
let a, _ = math.random(42)   // always 0.7415648788

// generate a random integer in [0, n)
let r, s = math.random(seed)
let dice = floor(r * 6) + 1   // random 1-6
```

---

## Date/Time Functions

Functions for date and time operations.

### Current Time

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `datetime()` | Current date and time | `datetime()` | `t'2025-01-23T14:30:00'` |
| `datetime(x)` | Parse as datetime | `datetime("2025-01-01")` | `t'2025-01-01'` |
| `today()` | Current date | `today()` | `t'2025-01-23'` |
| `now()` | Current timestamp (proc) | `now()` | `t'2025-01-23T14:30:00'` |
| `justnow()` | Current time only | `justnow()` | `t'14:30:00'` |

### Date/Time Extraction

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `date(dt)` | Extract date part | `date(t'2025-01-01T14:30')` | `t'2025-01-01'` |
| `time(dt)` | Extract time part | `time(t'2025-01-01T14:30')` | `t'14:30:00'` |

```lambda
datetime()                 // Current date and time
today()                    // Current date
justnow()                  // Current time

date(t'2025-01-01T14:30')  // t'2025-01-01'
time(t'2025-01-01T14:30')  // t'14:30:00'
```

---

## String Functions

Functions for string manipulation. `replace`, `split`, and `find` accept either a plain string or a **named string pattern** as the match argument.

### String Pattern Recap

String patterns are defined in the type system (see [Lambda_Type.md](Lambda_Type.md) § String Patterns) and can be used as arguments to string functions:

```lambda
string digits = \d+              // one or more digits
string ws = \s+                  // one or more whitespace chars
string word = \w+                // word characters
```

### replace(str, pattern_or_string, replacement)

Replace all occurrences of a pattern or substring in a string. Returns a new string.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `replace(str, pattern, repl)` | Replace all pattern matches | `replace("a1b2", \d, "X")` | `"aXbX"` |
| `replace(str, string, repl)` | Replace all substring matches | `replace("abc", "b", "X")` | `"aXc"` |

```lambda
string digit = \d
string digits = \d+
string ws = \s+

replace("a1b2c3", digit, "X")         // "aXbXcX"
replace("a1b22c333", digits, "N")     // "aNbNcN"
replace("hello   world", ws, " ")     // "hello world"
replace("no-digits", digit, "X")      // "no-digits" (no match → unchanged)
replace("a1b2", digit, "")            // "ab" (delete matches)
replace("abc", "b", "")               // "ac" (plain string delete)
```

### split(str, pattern_or_string, keep_delimiters?)

Split a string by pattern or substring. Returns an array of substrings.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `split(str, pattern)` | Split by pattern | `split("a1b2", \d)` | `["a", "b", ""]` |
| `split(str, string)` | Split by substring | `split("a,b,c", ",")` | `["a", "b", "c"]` |
| `split(str, sep, true)` | Split, keep delimiters | `split("a1b2", \d, true)` | `["a","1","b","2",""]` |

```lambda
string digit = \d
string digits = \d+
string ws = \s+

split("a1b2c3", digit)                // ["a", "b", "c", ""]
split("hello   world", ws)            // ["hello", "world"]
split("a1b22c333", digits)            // ["a", "b", "c", ""]
split("no-match", digit)              // ["no-match"]
split("a1b2c3", digit, true)          // ["a", "1", "b", "2", "c", "3", ""]  — keep delimiters
split("a,b,c", ",", true)             // ["a", ",", "b", ",", "c"]           — works with strings too
```

### join(strs, separator)

Join a list of strings with a separator. Returns a string.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `join(strs, sep)` | Join with separator | `join(["a", "b", "c"], ", ")` | `"a, b, c"` |
| `join(strs, "")` | Concatenate | `join(["hello", "world"], "")` | `"helloworld"` |

```lambda
join(["a", "b", "c"], ", ")           // "a, b, c"
join(["hello"], ", ")                 // "hello"
join(["x", "y"], "-")                // "x-y"
```

### find(str, pattern_or_string)

Find all occurrences of a pattern or substring. Returns a list of match maps `{value, index}`.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `find(str, pattern)` | Find all pattern matches | `find("a1b22", \d+)` | `[{value:"1",index:1}, ...]` |
| `find(str, string)` | Find all substring matches | `find("abab", "ab")` | `[{value:"ab",index:0}, ...]` |

Each match is a map with:
- `value` — the matched substring
- `index` — the start position (0-based) in the source string

```lambda
string digits = \d+
string words = \w+

find("a1b22c333", digits)
// [{value: "1", index: 1}, {value: "22", index: 3}, {value: "333", index: 6}]

find("hello world", words)
// [{value: "hello", index: 0}, {value: "world", index: 6}]

find("hello world hello", "lo")
// [{value: "lo", index: 3}, {value: "lo", index: 14}]

find("no-match", digits)
// [] (empty list)

// Extract just matching values via pipe
find("a1b22", digits) | ~.value     // ["1", "22"]
```

### normalize(str)

Normalize a string (Unicode normalization).

### ord(str)

Return the Unicode code point (integer) of the first character. Works on both strings and symbols.

```lambda
ord("A")             // 65
ord("é")             // 233
ord("😀")            // 128512
ord('A')             // 65 (symbol input)
ord("hello")         // 104 (first character 'h')
ord("")              // 0 (empty string)
```

### chr(int)

Return a 1-character string from a Unicode code point.

```lambda
chr(65)              // "A"
chr(233)             // "é"
chr(128512)          // "😀"
chr(-1)              // null (out of range)
```

**Round-trip:**
```lambda
chr(ord("Z"))        // "Z"
ord(chr(65))         // 65
```

---

## Collection Functions

Functions for working with arrays, lists, and other collections.

### Basic Operations

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `len(x)` | Length of collection | `len([1, 2, 3])` | `3` |
| `slice(vec, i, j)` | Extract slice [i, j) | `slice([1,2,3,4], 1, 3)` | `[2, 3]` |
| `set(vec)` | Remove duplicates | `set([1, 1, 2, 2, 3])` | `[1, 2, 3]` |
| `all(vec)` | All elements truthy? | `all([true, true, false])` | `false` |
| `any(vec)` | Any element truthy? | `any([false, false, true])` | `true` |

### Vector Manipulation

| Function             | Description                         | Example                                | Result             |
| -------------------- | ----------------------------------- | -------------------------------------- | ------------------ |
| `reverse(vec)`       | Reverse order                       | `reverse([1, 2, 3])`                   | `[3, 2, 1]`        |
| `sort(vec)`          | Sort ascending                      | `sort([3, 1, 2])`                      | `[1, 2, 3]`        |
| `sort(vec, 'desc)`   | Sort descending                     | `sort([1, 2, 3], 'desc)`               | `[3, 2, 1]`        |
| `sort(vec, fn)`      | Sort by key function                | `sort(users, ~.age)`                   | Sorted by age      |
| `sort(vec, options)` | Sort with options map               | `sort(users, {dir: 'desc, by: ~.age})` | Sorted by age desc |
| `unique(vec)`        | Remove duplicates (preserves order) | `unique([1, 2, 2, 3])`                 | `[1, 2, 3]`        |
| `take(vec, n)`       | First n elements                    | `take([1, 2, 3], 2)`                   | `[1, 2]`           |
| `drop(vec, n)`       | Drop first n elements               | `drop([1, 2, 3], 1)`                   | `[2, 3]`           |
| `zip(v1, v2)`        | Pair elements                       | `zip([1, 2], [3, 4])`                  | `[(1, 3), (2, 4)]` |

### Vector Construction

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `fill(n, value)` | Vector of n copies | `fill(3, 5)` | `[5, 5, 5]` |
| `range(start, end, step)` | Range with step | `range(0, 10, 2)` | `[0, 2, 4, 6, 8]` |

```lambda
slice([1, 2, 3, 4], 1, 3)  // [2, 3]
set([1, 1, 2, 2, 3])       // [1, 2, 3]
all([true, true, false])   // false
any([false, false, true])  // true

reverse([1, 2, 3])         // [3, 2, 1]
sort([3, 1, 2])            // [1, 2, 3]
sort([1, 2, 3], 'desc)     // [3, 2, 1]

// Sort by key function — 2nd arg is fn
let users = [{name: "Bob", age: 30}, {name: "Alice", age: 25}]
sort(users, ~.age)         // sorted by age ascending
sort(users, ~.name)        // sorted by name ascending

// Sort with options map — direction + key
sort(users, {dir: 'desc, by: ~.age})   // sorted by age descending

unique([1, 2, 2, 3, 3])    // [1, 2, 3]
take([1, 2, 3, 4], 2)      // [1, 2]
drop([1, 2, 3, 4], 2)      // [3, 4]
zip([1, 2], ["a", "b"])    // [(1, "a"), (2, "b")]

fill(3, 0)                 // [0, 0, 0]
range(0, 10, 2)            // [0, 2, 4, 6, 8]
```

---

## Aggregation & Reduction Functions

Functions that reduce collections to single values.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `sum(vec)` | Sum of elements | `sum([1, 2, 3])` | `6` |
| `avg(vec)` | Arithmetic mean | `avg([1, 2, 3])` | `2.0` |
| `min(vec)` | Minimum | `min([3, 1, 2])` | `1` |
| `max(vec)` | Maximum | `max([3, 1, 2])` | `3` |
| `all(vec)` | All truthy? | `all([true, true])` | `true` |
| `any(vec)` | Any truthy? | `any([false, true])` | `true` |
| `len(vec)` | Count elements | `len([1, 2, 3])` | `3` |
| `reduce(vec, fn)` | Reduce with binary function | `reduce([1,2,3], (a,b) => a+b)` | `6` |

### reduce(vec, fn)

Reduce a collection to a single value by applying a binary function cumulatively. Uses the first element as the initial accumulator.

```lambda
reduce([1, 2, 3, 4], (a, b) => a + b)     // 10 (sum)
reduce([1, 2, 3, 4, 5], (a, b) => a * b)   // 120 (product)
reduce([42], (a, b) => a + b)               // 42 (single element)

// Sort and reduce
let nums = [5, 3, 8, 1]
reduce(sort(nums), (acc, x) => acc * 10 + x)   // 1358
```

> **Note**: Collection must have at least one element. If only one element, it is returned directly without calling the function.

---

## Variadic Argument Functions

Functions for accessing variable arguments inside variadic functions.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `varg()` | All variadic arguments | `varg()` | List of args |
| `varg(n)` | nth variadic argument (0-indexed) | `varg(0)` | First arg |

```lambda
// Define a variadic function
fn sum_all(...) => sum(varg())
fn first_arg(...) => varg(0)
fn count_args(...) => len(varg())

// Use the functions
sum_all(1, 2, 3, 4)        // 10
first_arg("a", "b", "c")   // "a"
count_args(1, 2, 3)        // 3
```

---

## Input/Output Functions

Lambda Script provides comprehensive support for file I/O with unified path/URL handling and multiple document formats.

### Unified Path and URL Handling

Lambda uses `Path` literals with unified syntax for both local files and remote URLs:

```lambda
// Local paths
let local = @./data/config.json          // Relative path
let absolute = @/Users/name/project/     // Absolute path

// Remote URLs
let api = @https://api.example.com/data  // HTTPS URL
let file_url = @file:///path/to/file     // File URL

// All work uniformly with I/O functions
let data = input(local)
let remote = input(api)
```

### Pure I/O Functions

These functions can be used anywhere (in both `fn` and `pn` functions).

#### input(target) / input(target, format)

Parse content from a file path or URL.

| Function | Description | Example |
|----------|-------------|---------|
| `input(target)` | Parse target (auto-detect format) | `input(@./data.json)` |
| `input(target, format)` | Parse target with specified format | `input("data.json", 'json)` |

**Supported Input Formats**: `json`, `xml`, `html`, `yaml`, `toml`, `markdown`, `csv`, `latex`, `rtf`, `pdf`, `css`, `ini`, `math`

| Format | Description | Example |
|--------|-------------|---------|
| JSON | JavaScript Object Notation | `input(@./data.json, 'json)` |
| XML | Extensible Markup Language | `input(@./config.xml, 'xml)` |
| HTML | HyperText Markup Language | `input(@./page.html, 'html)` |
| YAML | YAML Ain't Markup Language | `input(@./config.yaml, 'yaml)` |
| TOML | Tom's Obvious Minimal Language | `input(@./config.toml, 'toml)` |
| Markdown | Markdown markup | `input(@./doc.md, 'markdown)` |
| CSV | Comma-Separated Values | `input(@./data.csv, 'csv)` |
| LaTeX | LaTeX markup | `input(@./doc.tex, 'latex)` |
| RTF | Rich Text Format | `input(@./doc.rtf, 'rtf)` |
| PDF | Portable Document Format | `input(@./doc.pdf, 'pdf)` |
| CSS | Cascading Style Sheets | `input(@./style.css, 'css)` |
| INI | Configuration files | `input(@./config.ini, 'ini)` |
| Math | Mathematical expressions | `input(@./formula.txt, 'math)` |

**Input Function Usage:**

```lambda
// Basic input parsing with Path literals
let data = input(@./file.json, 'json)
let config = input(@./settings.yaml, 'yaml)

// Input from URLs
let api_data = input(@https://api.example.com/users.json)

// Input with options map
let math_expr = input(@./formula.txt, {type: 'math, flavor: 'latex})
let csv_data = input(@./data.csv, {type: 'csv, delimiter: ','})

// Auto-detection (based on file extension)
let auto_data = input(@./document.md)  // Automatically detects Markdown
```

#### parse(str) / parse(str, format)

Parse a string into Lambda data structures. Like `input()` but operates on string content instead of file paths/URLs.

| Function | Description | Example |
|----------|-------------|--------|
| `parse(str)` | Parse string (auto-detect format) | `parse("{\"x\": 1}")` |
| `parse(str, format)` | Parse string with specified format | `parse(str, 'json)` |

**Supported Formats**: Same as `input()` — `json`, `xml`, `html`, `yaml`, `toml`, `markdown`, `csv`, `latex`, `css`, `ini`, `math`

```lambda
// Parse JSON string
let data^err = parse("{\"name\": \"Alice\", \"age\": 30}", 'json)
data.name     // "Alice"

// Auto-detect JSON from content
let data2^err = parse("{\"x\": 1}")
data2.x       // 1

// Parse with options map
let expr^err = parse("x^2 + y", {type: 'math, flavor: 'latex})

// Parse CSV from string
let csv^err = parse("name,age\nAlice,30\nBob,25", 'csv)
```

> **Note**: `parse()` can raise errors. Use `let result^err = parse(...)` for error-safe handling.

#### exists(target)

Check if a file, directory, or URL target exists. Returns `true` or `false`.

```lambda
let file_exists = exists(@./config.json)
let dir_exists = exists(@./data/)

if exists(@./cache.json) {
    let cached = input(@./cache.json)
}
```

#### format(data) / format(data, type)

Format data as a string in various formats. This is a pure function that returns a string.

| Function | Description | Example |
|----------|-------------|---------|
| `format(data)` | Format data as Lambda representation | `format(obj)` |
| `format(data, type)` | Format data as specified type | `format(obj, 'json)` |

```lambda
// Format data as different types
let json_str = format(data, 'json)
let yaml_str = format(data, 'yaml)
let xml_str = format(data, 'xml)

// Format with options
let pretty_json = format(data, {type: 'json, indent: 2})
let compact_json = format(data, {type: 'json, compact: true})
```

---
## Procedural I/O Functions

Functions that have side effects (I/O, state changes). These are only available in procedural functions (`pn`).

### Overview

| Function | Description | Example |
|----------|-------------|---------|
| `print(x)` | Print to console | `print("Hello!")` |
| `output(data, target)` | Write data to file/URL | `output(data, @./out.json)` |
| `data \|> target` | Pipe output (write/truncate) | `data \|> @./result.json` |
| `data \|>> target` | Pipe output (append) | `line \|>> @./log.txt` |
| `io.copy(src, dst)` | Copy file or directory | `io.copy(@./a.txt, @./b.txt)` |
| `io.move(src, dst)` | Move/rename file or directory | `io.move(@./old, @./new)` |
| `io.delete(target)` | Delete file or directory | `io.delete(@./temp.txt)` |
| `io.mkdir(path)` | Create directory | `io.mkdir(@./data/)` |
| `io.touch(path)` | Create empty file or update timestamp | `io.touch(@./flag.txt)` |
| `io.symlink(target, link)` | Create symbolic link | `io.symlink(@./src, @./link)` |
| `io.chmod(path, mode)` | Change file permissions | `io.chmod(@./script.sh, "755")` |
| `io.rename(src, dst)` | Rename file or directory | `io.rename(@./a.txt, @./b.txt)` |
| `io.fetch(url, options)` | HTTP fetch with options | `io.fetch(@https://api.example.com, {method: 'POST})` |
| `cmd(command, args...)` | Execute shell command | `cmd("ls", "-la")` |
| `clock()` | Monotonic clock in seconds | `clock()` |

### print(x)

Prints a value to the console (stdout).

```lambda
print("Hello, world!")
print(42)
print([1, 2, 3])
```

### output(data, target) / output(data, target, format)

Writes data to a file or URL. The format can be auto-detected from the target extension or explicitly specified.

```lambda
pn save_data() {
    let data = {name: "Alice", age: 30, scores: [95, 87, 92]}

    // Using Path literals
    output(data, @./result.json)     // Writes JSON
    output(data, @./result.yaml)     // Writes YAML
    output(data, @./result.xml)      // Writes XML

    // Explicit format specification
    output(data, @./data.txt, 'json)    // Force JSON format
    output(data, @./data.out, 'yaml)    // Force YAML format

    // With options
    output(data, @./pretty.json, {type: 'json, indent: 4})
}
```

**Supported output formats:**

| Format | Extension | Description |
|--------|-----------|-------------|
| `json` | `.json` | JSON format |
| `yaml` | `.yaml`, `.yml` | YAML format |
| `xml` | `.xml` | XML format |
| `html` | `.html`, `.htm` | HTML format |
| `markdown` | `.md` | Markdown format |
| `text` | `.txt` | Plain text |
| `toml` | `.toml` | TOML format |
| `ini` | `.ini` | INI configuration format |

### Pipe Output Operators: |> and |>>

Lambda provides pipe output operators for convenient file writing in procedural functions.

#### |> (Write/Truncate)

The `|>` operator writes data to a target, truncating existing content:

```lambda
pn generate_report() {
    let report = {title: "Monthly Report", date: today(), items: [...]}
    report |> @./reports/monthly.json

    // Equivalent to:
    output(report, @./reports/monthly.json)
}
```

#### |>> (Append)

The `|>>` operator appends data to a target:

```lambda
pn log_event(event) {
    let entry = format({time: now(), event: event}, 'json)
    entry |>> @./logs/events.jsonl
}

pn process_items(items) {
    for item in items {
        process(item) |>> @./output.txt
    }
}
```

### io Module Functions

The `io` module provides procedural functions for file system operations.

> **Import Styles:** The `io` module supports three import styles:
> - **No import** (default): `io.copy(@./a, @./b)` — always available
> - **Global import** (`import io;`): `copy(@./a, @./b)` — all functions without prefix
> - **Aliased import** (`import f:io;`): `f.copy(@./a, @./b)` — use custom prefix

#### io.copy(source, destination)

Copy a file or directory to a new location.

```lambda
pn backup_config() {
    io.copy(@./config.json, @./backup/config.json)
    io.copy(@./data/, @./backup/data/)  // Copy directory recursively
}
```

#### io.move(source, destination)

Move or rename a file or directory.

```lambda
pn archive_logs() {
    io.move(@./logs/current.log, @./logs/archive/2024-01.log)
}
```

#### io.delete(target)

Delete a file or directory.

```lambda
pn cleanup() {
    io.delete(@./temp.txt)
    io.delete(@./cache/)  // Delete directory recursively
}
```

#### io.mkdir(path)

Create a directory (and parent directories if needed).

```lambda
pn setup_project() {
    io.mkdir(@./src/)
    io.mkdir(@./tests/)
    io.mkdir(@./docs/api/)  // Creates parent dirs too
}
```

#### io.touch(path)

Create an empty file or update its modification timestamp.

```lambda
pn mark_complete() {
    io.touch(@./build/.done)
}
```

#### io.symlink(target, link_path)

Create a symbolic link.

```lambda
pn setup_links() {
    io.symlink(@./config/production.json, @./config.json)
}
```

#### io.chmod(path, mode)

Change file permissions (Unix-style).

```lambda
pn make_executable() {
    io.chmod(@./scripts/deploy.sh, "755")
    io.chmod(@./secrets.env, "600")
}
```

#### io.rename(source, destination)

Rename a file or directory (alias for move within same directory).

```lambda
pn rename_file() {
    io.rename(@./draft.txt, @./final.txt)
}
```

#### io.fetch(url, options)

Perform HTTP requests with full control over method, headers, and body.

```lambda
pn api_operations() {
    // GET request
    let data = io.fetch(@https://api.example.com/users, {
        method: 'GET,
        headers: {Authorization: "Bearer token123"}
    })

    // POST request
    let result = io.fetch(@https://api.example.com/users, {
        method: 'POST,
        headers: {Content-Type: "application/json"},
        body: format({name: "Alice", email: "alice@example.com"}, 'json)
    })
}
```

### cmd(command, args...)

Execute a shell command and return the result.

```lambda
pn run_commands() {
    let files = cmd("ls", "-la")
    let date = cmd("date", "+%Y-%m-%d")

    // With multiple arguments
    cmd("git", "commit", "-m", "Update files")
}
```

### clock()

Returns the current value of a monotonic clock as a `float` in seconds. Useful for measuring elapsed time in benchmarks and performance profiling. The returned value has nanosecond precision and is not related to wall-clock time.

```lambda
pn benchmark() {
    var t0 = clock()
    // ... work to measure ...
    var t1 = clock()
    var elapsed_ms = (t1 - t0) * 1000.0
    print("Elapsed: " + string(elapsed_ms) + " ms")
}
```

---

## Error Handling

Functions for creating and handling errors.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `error(msg)` | Create error value | `error("Invalid input")` | Error |

```lambda
// Create an error
error("Something went wrong")

// Error handling in expressions
let result = if (x == 0) error("Division by zero") else (y / x)

// Check for error
if (result is error) {
    print("Error occurred")
}
```

---

## Quick Reference Table

### Type Functions
| Function | Args | Description |
|----------|------|-------------|
| `len` | 1 | Get length |
| `type` | 1 | Get type |
| `int` | 1 | Convert to int |
| `int64` | 1 | Convert to int64 |
| `float` | 1 | Convert to float |
| `decimal` | 1 | Convert to decimal |
| `string` | 1 | Convert to string |
| `symbol` | 1 | Convert to symbol |
| `binary` | 1 | Convert to binary |
| `number` | 1 | Convert to number |

### Math Functions
| Function | Args | Description |
|----------|------|-------------|
| `abs` | 1 | Absolute value |
| `round` | 1 | Round to nearest |
| `floor` | 1 | Round down |
| `ceil` | 1 | Round up |
| `sign` | 1 | Sign (-1, 0, 1) |
| `math.sqrt` | 1 | Square root |
| `math.cbrt` | 1 | Cube root |
| `math.hypot` | 2 | Hypotenuse √(x²+y²) |
| `math.trunc` | 1 | Truncate toward zero |
| `math.pow` | 2 | Power (b^e) |
| `math.log` | 1 | Natural log |
| `math.log2` | 1 | Base-2 log |
| `math.log10` | 1 | Base-10 log |
| `math.log1p` | 1 | log(1 + x) |
| `math.exp` | 1 | Exponential (e^x) |
| `math.exp2` | 1 | Base-2 exponential |
| `math.expm1` | 1 | exp(x) - 1 |
| `math.sin` | 1 | Sine |
| `math.cos` | 1 | Cosine |
| `math.tan` | 1 | Tangent |
| `math.asin` | 1 | Inverse sine |
| `math.acos` | 1 | Inverse cosine |
| `math.atan` | 1 | Inverse tangent |
| `math.atan2` | 2 | Two-arg arctan |
| `math.sinh` | 1 | Hyperbolic sine |
| `math.cosh` | 1 | Hyperbolic cosine |
| `math.tanh` | 1 | Hyperbolic tangent |
| `math.asinh` | 1 | Inverse hyp. sine |
| `math.acosh` | 1 | Inverse hyp. cosine |
| `math.atanh` | 1 | Inverse hyp. tangent |
| `math.pi` | - | Pi (π) constant |
| `math.e` | - | Euler's number constant |

### Statistical Functions
| Function | Args | Description |
|----------|------|-------------|
| `sum` | 1 | Sum |
| `avg` | 1 | Average |
| `mean` | 1 | Mean (alias) |
| `median` | 1 | Median |
| `variance` | 1 | Variance |
| `deviation` | 1 | Std deviation |
| `quantile` | 2 | Quantile |
| `prod` | 1 | Product |
| `cumsum` | 1 | Cumulative sum |
| `cumprod` | 1 | Cumulative product |
| `dot` | 2 | Dot product |
| `norm` | 1 | Euclidean norm |
| `min` | 1-2 | Minimum |
| `max` | 1-2 | Maximum |
| `argmin` | 1 | Index of min |
| `argmax` | 1 | Index of max |
| `reduce` | 2 | Reduce with binary fn |

### Collection Functions
| Function | Args | Description |
|----------|------|-------------|
| `slice` | 3 | Extract slice |
| `set` | 1+ | Remove duplicates |
| `all` | 1 | All truthy |
| `any` | 1 | Any truthy |
| `reverse` | 1 | Reverse order |
| `sort` | 1-2 | Sort (dir, key fn, or options map) |
| `unique` | 1 | Unique elements |
| `take` | 2 | Take first n |
| `drop` | 2 | Drop first n |
| `zip` | 2 | Zip vectors |
| `fill` | 2 | Fill vector |
| `range` | 3 | Range with step |

### I/O Functions (Pure)
| Function | Args | Description |
|----------|------|-------------|
| `input` | 1-2 | Parse file/URL |
| `parse` | 1-2 | Parse string content |
| `exists` | 1 | Check if target exists |
| `format` | 1-2 | Format data as string |

### I/O Functions (Procedural)
| Function | Args | Description |
|----------|------|-------------|
| `print` | 1 | Print to console |
| `output` | 2-3 | Write to file/URL |
| `\|>` | 2 | Pipe write (truncate) |
| `\|>>` | 2 | Pipe write (append) |
| `io.copy` | 2 | Copy file/directory |
| `io.move` | 2 | Move file/directory |
| `io.delete` | 1 | Delete file/directory |
| `io.mkdir` | 1 | Create directory |
| `io.touch` | 1 | Create/update file |
| `io.symlink` | 2 | Create symbolic link |
| `io.chmod` | 2 | Change permissions |
| `io.rename` | 2 | Rename file/directory |
| `io.fetch` | 2 | HTTP request |
| `cmd` | 1+ | Shell command |
| `clock` | 0 | Monotonic clock (seconds) |

### Date/Time Functions
| Function | Args | Description |
|----------|------|-------------|
| `datetime` | 0-1 | Date and time |
| `date` | 1 | Extract date |
| `time` | 1 | Extract time |
| `today` | 0 | Current date (proc) |
| `now` | 0 | Current time (proc) |
| `justnow` | 0 | Time only |

### Other Functions
| Function | Args | Description |
|----------|------|-------------|
| `error` | 1 | Create error |
| `normalize` | 1 | Normalize string |
| `varg` | 0-1 | Variadic args |

### String Functions
| Function | Args | Description |
|----------|------|-------------|
| `replace` | 3 | Replace pattern/substring in string |
| `split` | 2-3 | Split string by pattern/substring |
| `join` | 2 | Join list of strings with separator |
| `find` | 2 | Find all pattern/substring matches |
| `ord` | 1 | Unicode code point of first character |
| `chr` | 1 | Character from Unicode code point |
