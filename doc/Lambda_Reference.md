# Lambda Script Language Reference

## Table of Contents

1. [Introduction](#introduction)
2. [Language Overview](#language-overview)
3. [Syntax and Grammar](#syntax-and-grammar)
4. [Data Types](#data-types)
5. [Literals](#literals)
6. [Variables and Declarations](#variables-and-declarations)
7. [Expressions](#expressions)
8. [Control Flow](#control-flow)
9. [Functions](#functions)
10. [Collections](#collections)
11. [Type System](#type-system)
12. [System Functions](#system-functions)
13. [Input/Output and Parsing](#inputoutput-and-parsing)
14. [Operators](#operators)
15. [Modules and Imports](#modules-and-imports)
16. [Error Handling](#error-handling)
17. [Memory Management](#memory-management)
18. [Examples](#examples)

---

## Introduction

Lambda Script is a **general-purpose, cross-platform, pure functional scripting language** designed for data processing and document presentation. Built from scratch in C with JIT compilation using MIR (Medium Internal Representation) and reference counting memory management.

### Key Features

- **Pure Functional**: Immutable data structures and functional programming paradigms
- **JIT Compilation**: Near-native performance through MIR-based compilation
- **Cross-Platform**: Runs on macOS, Linux, Windows with consistent behavior
- **Rich Type System**: Strong typing with type inference and advanced type constructs
- **Document Processing**: Built-in support for 13+ input formats and multiple output formats
- **Unicode Support**: Configurable Unicode support with ICU integration
- **Memory Safe**: Reference counting and pool-based memory management

---

## Language Overview

Lambda Script is designed around functional programming principles with modern syntax. Programs consist of expressions that evaluate to values, with support for:

- Immutable data structures (lists, arrays, maps, elements)
- First-class functions and closures
- Pattern matching and destructuring
- Comprehensive type system with inference
- Built-in document processing capabilities

### Philosophy

1. **Data as Code**: Documents and data structures are first-class citizens
2. **Type Safety**: Compile-time type checking prevents runtime errors
3. **Expressiveness**: Concise syntax for complex data transformations
4. **Performance**: JIT compilation for production-ready performance

---

## Syntax and Grammar

### Comments

```lambda
// Single-line comment

/*
   Multi-line comment
   can span multiple lines
*/
```

### Basic Structure

Lambda Script files contain expressions and statements:

```lambda
// Expressions (return values)
42
"hello world"
[1, 2, 3]

// Statements (declarations and control flow)
let x = 10;
if (x > 5) {
    print("x is greater than 5")
}
```

### Whitespace and Line Breaks

- Whitespace is generally ignored except in strings
- Line breaks can separate statements
- Semicolons (`;`) are used to terminate statements but are often optional

---

## Data Types

Lambda Script has a rich type system with both primitive and composite types:

### Primitive Types

| Type | Description | Example |
|------|-------------|---------|
| `null` | Null/void value | `null` |
| `bool` | Boolean values | `true`, `false` |
| `int` | 64-bit signed integers | `42`, `-123` |
| `float` | 64-bit floating point | `3.14`, `1.5e-10` |
| `decimal` | Arbitrary precision decimal | `123.456n` |
| `string` | UTF-8 text strings | `"hello"` |
| `symbol` | Interned identifiers | `'symbol` |
| `binary` | Binary data | `b'\xDEADBEEF'` |
| `datetime` | Date and time values | `t'2025-01-01'` |

### Composite Types

| Type | Description | Example |
|------|-------------|---------|
| `list` | Immutable ordered sequences | `(1, 2, 3)` |
| `array` | Mutable ordered collections | `[1, 2, 3]` |
| `map` | Key-value mappings | `{key: "value"}` |
| `element` | Structured markup elements | `<tag attr: value; content>` |
| `range` | Numeric ranges | `1 to 10` |
| `function` | Function values | `fn(x) => x + 1` |
| `type` | Type descriptors | `int`, `string` |

### Special Types

- `any` - Any type (top type)
- `error` - Error values
- `number` - Numeric union type (int | float)

---

## Literals

### Numeric Literals

```lambda
// Integers
42
-123
0

// Floats
3.14
-2.5
1.5e-10
1e6
inf
nan
-inf

// Decimals (arbitrary precision)
123.456n
-789.012N
```

### String Literals

```lambda
// Basic strings
"hello world"
"multiline strings
can span multiple lines"

// Escape sequences
"line 1\nline 2"
"tab\there"
"quote: \"hello\""
```

### Symbol Literals

```lambda
'identifier
'symbol-name
'CamelCase
```

### Binary Literals

```lambda
// Hexadecimal
b'\xDEADBEEF'
b'\xA0FE af0d'

// Base64
b'\64A0FE'
b'\64A0FE gh8='
b'\64A0FE gh=='
```

### DateTime Literals

```lambda
// Dates
t'2025'           // Year only
t'2025-06'        // Year-month
t'2025-04-26'     // Full date
t'-1000-12-25'    // Historical dates

// Times
t'10:30'          // Hour:minute
t'10:30:45'       // Hour:minute:second
t'10:30:45.123'   // With milliseconds
t'10:30+08'       // With timezone

// Date-time combinations
t'2025-05-01 10:30'
t'2025-05-01T14:30:00Z'
```

### Boolean and Null Literals

```lambda
true
false
null
```

---

## Variables and Declarations

### Let Expressions

Let expressions introduce local bindings:

```lambda
// Single binding
(let x = 42, x + 1)  // Returns 43

// Multiple bindings
(let a = 1, let b = 2, a + b)  // Returns 3

// Complex expressions
(let data = [1, 2, 3], 
 let doubled = (for (x in data) x * 2),
 doubled)
```

### Let Statements

Let statements declare variables in the current scope:

```lambda
let x = 42;
let y = 3.14, z = true;
let name = "Alice", age = 30;
```

### Public Declarations

Public declarations export variables:

```lambda
pub PI = 3.14159;
pub greeting = "Hello, World!";
```

### Type Annotations

Variables can have explicit type annotations:

```lambda
let x: int = 42;
let name: string = "Alice";
let items: [int] = [1, 2, 3];
```

---

## Expressions

### Primary Expressions

```lambda
// Literals
42
"hello"
true

// Variables
x
myVariable

// Parenthesized expressions
(x + y)

// Collection access
arr[0]
map.key
obj["field"]
```

### Arithmetic Expressions

```lambda
// Basic arithmetic
5 + 3     // Addition
5 - 3     // Subtraction
5 * 3     // Multiplication
10 / 3    // Division
10 _/ 3   // Integer division
17 % 5    // Modulo
2 ^ 3     // Exponentiation

// Unary operators
-x        // Negation
+x        // Positive (identity)
```

### Comparison Expressions

```lambda
// Equality
5 == 5    // Equal
5 != 3    // Not equal

// Relational
3 < 5     // Less than
5 > 3     // Greater than
3 <= 5    // Less than or equal
5 >= 3    // Greater than or equal
```

### Logical Expressions

```lambda
// Boolean operators
true and false   // Logical AND
true or false    // Logical OR
not true         // Logical NOT

// Short-circuit evaluation
(x > 0) and (y / x > 2)
```

### Type Expressions

```lambda
// Type queries
type(42)          // Returns 'int'
type("hello")     // Returns 'string'

// Type checking
42 is int         // Returns true
"hello" is string // Returns true
```

---

## Control Flow

### If Expressions

If expressions require both then and else branches:

```lambda
// Simple if expression
(if (x > 0) "positive" else "non-positive")

// Nested if expressions
(if (score >= 90) "A" 
 else if (score >= 80) "B" 
 else "C")

// If expressions in let bindings
(let x = 5, if (x > 3) "big" else "small")
```

### If Statements

If statements can have optional else clauses:

```lambda
if (x > 0) {
    print("x is positive")
}

if (temperature > 30) {
    print("It's hot!")
} else {
    print("It's not too hot")
}
```

### For Expressions

For expressions iterate over collections and return new collections:

```lambda
// Array iteration
(for (x in [1, 2, 3]) x * 2)  // [2, 4, 6]

// Range iteration
(for (i in 1 to 5) i * i)     // [1, 4, 9, 16, 25]

// Conditional iteration
(for (num in [1, 2, 3, 4, 5]) 
    if (num % 2 == 0) num else 0)

// String iteration
(for (item in ["a", "b", "c"]) item + "!")
```

### For Statements

For statements execute code for each iteration:

```lambda
for item in [1, 2, 3] {
    print(item)
}

for i in 1 to 10 {
    if (i % 2 == 0) {
        print(i, "is even")
    }
}

// Multiple loop variables
for x in [1, 2], y in [3, 4] {
    print(x, y)
}
```

---

## Functions

### Function Declarations

```lambda
// Named function statement
fn add(a: int, b: int) -> int {
    a + b
}

// Function expression statement  
fn multiply(x: int, y: int) => x * y

// Anonymous function expressions
(x: int) => x * 2
fn(x: int, y: int) => x + y

// Multi-statement function body
fn process(data: [int]) -> [int] {
    let filtered = (for (x in data) if (x > 0) x else 0);
    let doubled = (for (x in filtered) x * 2);
    doubled
}
```

### Function Parameters

```lambda
// Required parameters
fn greet(name: string) => "Hello, " + name

// Multiple parameters
fn calculate(x: float, y: float, operation: string) => {
    if (operation == "add") {
        x + y
    } else if (operation == "multiply") {
        x * y
    } else {
        error("Unknown operation")
    }
}
```

### Function Calls

```lambda
// Function calls
add(5, 3)
greet("Alice")
calculate(2.5, 3.0, "add")

// Higher-order functions
let numbers = [1, 2, 3, 4, 5];
let doubled = (for (x in numbers) multiply(x, 2))
```

### Closures

Functions can capture variables from their enclosing scope:

```lambda
let multiplier = 3;
let triple = fn(x: int) => x * multiplier;
triple(5)  // Returns 15
```

---

## Collections

### Lists

Immutable ordered sequences (tuples):

```lambda
// List creation
(1, 2, 3)
("hello", "world")
(true, 42, "mixed")

// List access
let lst = (1, 2, 3);
lst[0]    // First element: 1
lst[1]    // Second element: 2
lst[-1]   // Last element: 3

// List operations
len(lst)  // Length: 3
```

### Arrays

Mutable ordered collections:

```lambda
// Array creation
[1, 2, 3]
["a", "b", "c"]
[true, false, null]

// Mixed-type arrays
[42, "hello", 3.14, true, null]

// Nested arrays
[[1, 2], [3, 4], [5, 6]]

// Array access
let arr = [10, 20, 30];
arr[0]     // 10
arr[1]     // 20
arr[-1]    // 30 (last element)

// Array operations
len(arr)   // 3
```

### Maps

Key-value mappings:

```lambda
// Map creation
{key: "value"}
{name: "Alice", age: 30, active: true}

// Mixed key types
{"string_key": 1, symbol_key: 2, 3: "number_key"}

// Nested maps
{
    user: {name: "Bob", email: "bob@example.com"},
    settings: {theme: "dark", notifications: true}
}

// Map access
let person = {name: "Charlie", age: 25};
person.name      // "Charlie"
person["name"]   // "Charlie" 
person.age       // 25

// Map operations
len(person)      // 2
```

### Elements

Structured markup elements with attributes and content:

```lambda
// Basic element
<tag>

// Element with attributes
<div id: "main", class: "container">

// Element with content
<p; "Hello, world!">

// Element with attributes and content
<div class: "header"; 
    "Page Title";
    <span; "Subtitle">
>

// Complex elements
<article title: "My Article", author: "John Doe";
    <h1; "Introduction">
    <p; "This is the first paragraph.">
    <p; "This is the second paragraph.">
>
```

### Ranges

Numeric sequences:

```lambda
// Basic ranges
1 to 10        // [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
0 to 5         // [0, 1, 2, 3, 4, 5]
-2 to 2        // [-2, -1, 0, 1, 2]

// Range operations
len(1 to 10)   // 10
(1 to 5)[2]    // 3

// Range in for loops
(for (i in 1 to 5) i * i)  // [1, 4, 9, 16, 25]
```

---

## Type System

### Basic Types

```lambda
// Type literals
null
bool
int
float
decimal
string
symbol
binary
datetime
```

### Collection Types

```lambda
// Array types
[int]              // Array of integers
[string]           // Array of strings
[[int]]            // Array of integer arrays

// List types
(int, string)      // List with int and string
(string, string, string)  // List of three strings

// Map types
{name: string, age: int}  // Map with specific fields

// Element types
<tag attr: string; content_type>
```

### Union Types

```lambda
// Union types (using |)
int | string       // Either int or string
int | float | string  // One of int, float, or string

// Nullable types
int | null         // Integer or null
string | null      // String or null
```

### Function Types

```lambda
// Function type syntax
(int) => int              // Function taking int, returning int
(int, int) => int         // Function taking two ints, returning int
(string, bool) => string  // Function taking string and bool, returning string

// Example function with explicit type
let add: (int, int) => int = fn(a: int, b: int) => a + b;
```

### Type Declarations

```lambda
// Type aliases
type UserId = int;
type UserName = string;
type Point = (float, float);

// Object types
type User = {
    id: UserId,
    name: UserName,
    email: string,
    active: bool
};

// Element types
type Article = <article title: string, author: string;
    string,           // Text content
    [Section]         // Array of sections
>;

type Section = <section heading: string;
    string            // Section content
>;
```

### Type Occurrences

```lambda
// Optional types
int?               // Optional integer (int | null)
string?            // Optional string (string | null)

// Array types with occurrences
int*               // Array of zero or more integers
string+            // Array of one or more strings
```

---

## System Functions

### Type Functions

```lambda
// Type conversion
int("42")          // Parse string to int
float("3.14")      // Parse string to float
string(42)         // Convert to string
symbol("text")     // Convert to symbol

// Type inspection
type(42)           // Get type of value
len([1, 2, 3])     // Get length of collection
```

### I/O Functions

```lambda
// Input parsing
input("file.json", 'json')           // Parse JSON file
input("data.xml", 'xml')             // Parse XML file
input("document.md", 'markdown')     // Parse Markdown file

// Output formatting
print("Hello, world!")               // Print to console
format(data, 'json')                 // Format as JSON
format(data, 'yaml')                 // Format as YAML
```

### Mathematical Functions

```lambda
// Basic math
abs(-5)            // Absolute value: 5
min(1, 2, 3)       // Minimum: 1  
max(1, 2, 3)       // Maximum: 3
sum([1, 2, 3])     // Sum: 6
avg([1, 2, 3])     // Average: 2.0

// Rounding
round(3.7)         // Round: 4
floor(3.7)         // Floor: 3
ceil(3.2)          // Ceiling: 4
```

### Date/Time Functions

```lambda
// Current time
datetime()         // Current date and time
today()            // Current date
justnow()          // Current time

// Date operations  
date(t'2025-01-01')    // Extract date part
time(t'14:30:00')      // Extract time part
```

### Collection Functions

```lambda
// Array/list operations
slice([1, 2, 3, 4], 1, 3)    // [2, 3]
set([1, 1, 2, 2, 3])         // Remove duplicates

// Aggregation
all([true, true, false])     // false
any([false, false, true])    // true
```

### Error Handling

```lambda
// Error creation
error("Something went wrong")

// Error handling in expressions
if (x == 0) error("Division by zero") else (y / x)
```

---

## Input/Output and Parsing

Lambda Script provides comprehensive support for parsing and formatting various document types.

### Supported Input Formats

| Format | Description | Example |
|--------|-------------|---------|
| JSON | JavaScript Object Notation | `input("data.json", 'json')` |
| XML | Extensible Markup Language | `input("config.xml", 'xml')` |
| HTML | HyperText Markup Language | `input("page.html", 'html')` |
| YAML | YAML Ain't Markup Language | `input("config.yaml", 'yaml')` |
| TOML | Tom's Obvious Minimal Language | `input("config.toml", 'toml')` |
| Markdown | Markdown markup | `input("doc.md", 'markdown')` |
| CSV | Comma-Separated Values | `input("data.csv", 'csv')` |
| LaTeX | LaTeX markup | `input("doc.tex", 'latex')` |
| RTF | Rich Text Format | `input("doc.rtf", 'rtf')` |
| PDF | Portable Document Format | `input("doc.pdf", 'pdf')` |
| CSS | Cascading Style Sheets | `input("style.css", 'css')` |
| INI | Configuration files | `input("config.ini", 'ini')` |
| Math | Mathematical expressions | `input("formula.txt", 'math')` |

### Input Function Usage

```lambda
// Basic input parsing
let data = input("file.json", 'json');
let config = input("settings.yaml", 'yaml');

// Input with options
let math_expr = input("formula.txt", {'type': 'math', 'flavor': 'latex'});
let csv_data = input("data.csv", {'type': 'csv', 'delimiter': ','});

// Auto-detection (based on file extension)
let auto_data = input("document.md");  // Automatically detects Markdown
```

### Output Formatting

```lambda
// Format data as different types
let json_output = format(data, 'json');
let yaml_output = format(data, 'yaml');
let xml_output = format(data, 'xml');

// Format with options
let pretty_json = format(data, {'type': 'json', 'indent': 2});
let compact_json = format(data, {'type': 'json', 'compact': true});
```

### Mathematical Expression Parsing

Lambda Script includes a sophisticated mathematical expression parser supporting multiple syntaxes:

```lambda
// LaTeX syntax
let latex_formula = input("formula.tex", {'type': 'math', 'flavor': 'latex'});
// Supports: \frac{x}{y}, \sin(x), \alpha, \sum_{i=1}^{n}, etc.

// Typst syntax  
let typst_formula = input("formula.typ", {'type': 'math', 'flavor': 'typst'});
// Supports: frac(x, y), sin(x), alpha, sum(i=1, n), etc.

// ASCII syntax
let ascii_formula = input("formula.txt", {'type': 'math', 'flavor': 'ascii'});
// Supports: x/y, sin(x), alpha, sum(i=1 to n), etc.
```

---

## Operators

### Arithmetic Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `+` | Addition | `5 + 3` | `8` |
| `-` | Subtraction | `5 - 3` | `2` |
| `*` | Multiplication | `5 * 3` | `15` |
| `/` | Division | `10 / 3` | `3.333...` |
| `_/` | Integer division | `10 _/ 3` | `3` |
| `%` | Modulo | `17 % 5` | `2` |
| `^` | Exponentiation | `2 ^ 3` | `8` |

### Comparison Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `==` | Equal | `5 == 5` | `true` |
| `!=` | Not equal | `5 != 3` | `true` |
| `<` | Less than | `3 < 5` | `true` |
| `<=` | Less than or equal | `5 <= 5` | `true` |
| `>` | Greater than | `5 > 3` | `true` |
| `>=` | Greater than or equal | `5 >= 3` | `true` |

### Logical Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `and` | Logical AND | `true and false` | `false` |
| `or` | Logical OR | `true or false` | `true` |
| `not` | Logical NOT | `not true` | `false` |

### Unary Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `-` | Negation | `-5` | `-5` |
| `+` | Positive | `+5` | `5` |
| `not` | Logical NOT | `not true` | `false` |

### Set Operators

| Operator | Description  | Example       |
| -------- | ------------ | ------------- |
| \|       | Union        | set1 \| set2  |
| `&`      | Intersection | `set1 & set2` |
| `!`      | Exclusion    | `set1 ! set2` |

### Type Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `is` | Type check | `42 is int` | `true` |
| `in` | Membership | `2 in [1, 2, 3]` | `true` |
| `to` | Range | `1 to 5` | `[1, 2, 3, 4, 5]` |

### Operator Precedence

From highest to lowest precedence:

1. Primary expressions (`()`, `[]`, `.`)
2. Unary operators (`-`, `+`, `not`)
3. Exponentiation (`^`)
4. Multiplicative (`*`, `/`, `_/`, `%`)
5. Additive (`+`, `-`)
6. Relational (`<`, `<=`, `>`, `>=`)
7. Equality (`==`, `!=`)
8. Logical AND (`and`)
9. Logical OR (`or`)
10. Range (`to`)
11. Set operations (`|`, `&`, `!`)
12. Type operations (`is`, `in`)

---

## Modules and Imports

### Import Statements

```lambda
// Import modules
import module_name;
import .relative_module;        // Relative import
import \\windows\\style\\path;  // Windows-style path

// Import with alias
import alias: module_name;
import my_utils: .utilities;

// Multiple imports
import module1, module2, alias: module3;
```

### Module Structure

Each Lambda Script file is a module that can export public declarations:

```lambda
// math_utils.ls
pub PI = 3.14159;
pub E = 2.71828;

pub fn square(x: float) => x * x;
pub fn cube(x: float) => x * x * x;

// Private function (not exported)
fn helper(x: float) => x + 1;
```

### Using Imported Modules

```lambda
// main.ls
import math: .math_utils;

let area = math.PI * math.square(radius);
let volume = (4.0 / 3.0) * math.PI * math.cube(radius);
```

---

## Error Handling

### Error Values

Lambda Script uses explicit error values for error handling:

```lambda
// Creating errors
error("Something went wrong")
error("Division by zero in calculation")

// Functions that may return errors
fn safe_divide(a: float, b: float) -> float | error {
    if (b == 0.0) {
        error("Division by zero")
    } else {
        a / b
    }
}
```

### Error Propagation

```lambda
// Checking for errors
let result = safe_divide(10.0, 0.0);
if (result is error) {
    print("Error occurred:", result)
} else {
    print("Result:", result)
}

// Error handling in expressions
let value = if (x == 0) error("Invalid input") else process(x);
```

### Common Error Patterns

```lambda
// Type mismatches
5 + "hello"        // Error: cannot add int and string
true < false       // Error: cannot compare booleans

// Division by zero
5 / 0              // Error: division by zero
5 % 0              // Error: modulo by zero

// Index out of bounds
[1, 2, 3][5]       // Error: index out of bounds

// Invalid type operations
"hello" * 3.14     // Error: cannot multiply string by float
```

---

## Memory Management

Lambda Script uses automatic memory management with reference counting and memory pools:

### Reference Counting

- All values are automatically reference counted
- Memory is freed when reference count reaches zero
- No manual memory management required

### Memory Pools

- Objects are allocated from memory pools for efficiency
- Pools are automatically managed by the runtime
- Reduces fragmentation and improves performance

### Immutability

- Most data structures are immutable by default
- Immutability eliminates many memory safety issues
- Structural sharing for efficient memory usage

```lambda
// Immutable collections
let list1 = (1, 2, 3);
let list2 = (0, list1...);  // Shares structure with list1

// Mutable collections (arrays)
let arr = [1, 2, 3];
// arr is mutable, but assignment creates new references
```

---

## Examples

### Basic Data Processing

```lambda
// Read and process JSON data
let data = input("sales.json", 'json');

// Calculate total sales
let total = sum((for (item in data.sales) item.amount));

// Filter high-value sales
let high_value = (for (sale in data.sales) 
                     if (sale.amount > 1000) sale else null);

// Generate report
let report = {
    total_sales: total,
    high_value_count: len(high_value),
    average: total / len(data.sales),
    timestamp: datetime()
};

// Output as formatted JSON
print(format(report, 'json'));
```

### Document Processing

```lambda
// Parse Markdown document
let doc = input("article.md", 'markdown');

// Extract headings
let headings = (for (element in doc) 
                   if (element.tag == 'h1' or element.tag == 'h2') 
                       element.content 
                   else null);

// Generate table of contents
let toc = <div class: "toc";
    <h2; "Table of Contents">
    <ul;
        for (heading in headings) <li; <a href: "#" + heading; heading>>
    >
>;

// Create enhanced document
let enhanced = <article;
    toc;
    doc
>;

// Output as HTML
print(format(enhanced, 'html'));
```

### Mathematical Computation

```lambda
// Define mathematical functions
fn factorial(n: int) -> int {
    if (n <= 1) {
        1
    } else {
        n * factorial(n - 1)
    }
}

fn fibonacci(n: int) -> int {
    if (n <= 1) {
        n
    } else {
        fibonacci(n - 1) + fibonacci(n - 2)
    }
}

// Calculate sequences
let factorials = (for (i in 1 to 10) factorial(i));
let fibs = (for (i in 1 to 15) fibonacci(i));

// Generate mathematical report
let math_report = <div;
    <h1; "Mathematical Sequences">
    <h2; "Factorials">
    <p; "First 10 factorials: " + string(factorials)>
    <h2; "Fibonacci Numbers">  
    <p; "First 15 Fibonacci numbers: " + string(fibs)>
>;

print(format(math_report, 'html'));
```

### Type-Safe Configuration

```lambda
// Define configuration types
type DatabaseConfig = {
    host: string,
    port: int,
    database: string,
    ssl: bool
};

type ServerConfig = {
    listen_port: int,
    debug: bool,
    database: DatabaseConfig
};

// Load and validate configuration
let config_data = input("config.yaml", 'yaml');

// Type-safe configuration access
let server_config: ServerConfig = {
    listen_port: config_data.server.port,
    debug: config_data.server.debug,
    database: {
        host: config_data.database.host,
        port: config_data.database.port,
        database: config_data.database.name,
        ssl: config_data.database.ssl
    }
};

// Use configuration
if (server_config.debug) {
    print("Debug mode enabled");
    print("Database:", server_config.database.host);
}
```

### Advanced Data Transformation

```lambda
// Complex data transformation pipeline
let raw_data = input("customers.json", 'json');

// Multi-stage processing
let processed = (
    let customers = raw_data.customers,
    let active_customers = (for (c in customers) if (c.active) c else null),
    let enriched = (for (customer in active_customers) {
        ...customer,
        full_name: customer.first_name + " " + customer.last_name,
        account_age: datetime() - customer.created_at,
        lifetime_value: sum((for (order in customer.orders) order.amount))
    }),
    let high_value = (for (c in enriched) if (c.lifetime_value > 1000) c else null),
    let sorted = sort(high_value, fn(a, b) => b.lifetime_value - a.lifetime_value),
    sorted
);

// Generate customer report
let report = <html;
    <head; <title; "High-Value Customers">>
    <body;
        <h1; "Top Customers by Lifetime Value">
        <table;
            <tr; <th; "Name"> <th; "Email"> <th; "Lifetime Value">>
            for (customer in processed) <tr;
                <td; customer.full_name>
                <td; customer.email>
                <td; "$" + string(customer.lifetime_value)>
            >
        >
    >
>;

// Output formatted report
print(format(report, 'html'));
```

---

## Language Philosophy and Design Principles

### Functional Programming

Lambda Script embraces functional programming principles:

1. **Immutability**: Data structures are immutable by default
2. **Pure Functions**: Functions have no side effects (except I/O functions)  
3. **Expression-Oriented**: Everything is an expression that returns a value
4. **Higher-Order Functions**: Functions are first-class values

### Type Safety

Strong typing prevents runtime errors:

1. **Static Type Checking**: Types are checked at compile time
2. **Type Inference**: Types are automatically inferred when possible
3. **Explicit Types**: Optional type annotations for clarity
4. **Error Types**: Errors are explicit values, not exceptions

### Performance

JIT compilation provides excellent performance:

1. **MIR Backend**: Uses MIR for efficient code generation
2. **Memory Pools**: Efficient memory allocation strategies  
3. **Reference Counting**: Automatic memory management without GC pauses
4. **Structural Sharing**: Efficient copying of immutable data

### Expressiveness

Concise syntax for complex operations:

1. **Collection Comprehensions**: Powerful for-expressions for data processing
2. **Pattern Matching**: Destructuring in let expressions
3. **Type System**: Rich types including unions, intersections, and generics
4. **Document Processing**: Built-in support for markup and data formats

---

This reference provides a comprehensive overview of Lambda Script. The language continues to evolve, with new features and improvements being added regularly. For the latest updates and examples, refer to the test files in the `test/lambda/` directory and the official documentation.
