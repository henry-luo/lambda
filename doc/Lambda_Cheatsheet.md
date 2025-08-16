## CLI Commands
```bash
lambda                    # Start REPL
// REPL Commands: :quit, :help, :clear
lambda script.ls          # Run script  
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
1 to 10             // Range
[123, true]         // Array of values
(0.5, "string:)     // List/tuple
{key: 'symbol'}     // Map
<div class: bold; "text" <br>>    // Element
```

**Type Operators:**
```lambda
int | string     // Union type
int & number     // Intersection  
int?             // Optional (int | null)
int*             // Zero or more
int+             // One or more
(a: int, b: string) => bool   // Function type
{a: int, b: bool}             // Map type
<div id:symbol; <br>>         // Element type
```

## Literals

**Numbers:**
```lambda
42        // Integer
3.14      // Float  
1.5e-10   // Scientific notation
123.45n   // Decimal (arbitrary precision)
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

**Binary & DateTime:**
```lambda
b'\xDEADBEEF'     // Hex binary
b'\64QUVGRw=='    // Base64 binary
t'2025-01-01'     // Date
t'14:30:00'       // Time
t'2025-01-01T14:30:00Z'  // DateTime
```

**Collections:**
```lambda
[1, 2, 3]         // Array
(1, "two", 3.0)   // List
{a: 1, b: 2}      // Map
<div id: "main">  // Element
```

## Variables & Declarations

**Let Expressions:**
```lambda
(let x = 5, x + 1, x * 2)      // Single binding
(let a = 1, let b = 2, a + b)  // Multiple bindings
```

**Let Statements:**
```lambda
let x = 42;               // Variable declaration
let y : int = 100;        // With type annotation
let a = 1, b = 2;         // Multiple variables
```

**Public Declarations:**
```lambda
pub PI = 3.14159;         // Export variable
pub fn square(x) => x * x; // Export function
```

## Operators

**Arithmetic:** addition, subtraction, multiplication, division, integer division, modulo, exponentiation
```lambda
+  -  *  /  _/  %  ^
```

**Comparison:** equal, not equal, less than, less equal, greater than, greater equal
```lambda
==  !=  <  <=  >  >=
```

**Logical:** logical and, or, not
```lambda
and  or  not
```

**Type & Set:** type check, membership, range, union, intersection, exclusion
```lambda
is  in  to  |  &  !
```

## Control Flow

**If Expressions (require else):**
```lambda
if (x > 0) "positive" else "non-positive"
if (score >= 90) "A" 
else if (score >= 80) "B" else "C"
```

**If Statements (optional else):**
```lambda
if (x > 0) { "positive" }
if (condition) { something() } else { otherThing() }
```

**For Expressions:**
```lambda
for (x in [1, 2, 3]) x * 2     // Array iteration
for (i in 1 to 5) i * i        // Range iteration  
for (x in data) if (x > 0) x else 0  // Conditional
```

**For Statements:**
```lambda
for item in collection { transform(item) }
```

## Functions

**Function Declaration:**
```lambda
// Function with statement body
fn add(a: int, b: int) -> int { a + b }    
// Function  with expression body
fn multiply(x: int, y: int) => x * y    
// Anonymous function   
let square = (x) => x * x;                 
```

**Function Calls:**
```lambda
add(5, 3)         // Function call
```

## System Functions

**Type Conversion:**
```lambda
int("42")         // String to int
float("3.14")     // String to float  
string(42)        // Value to string
symbol("text")    // String to symbol
```

**Type Inspection:**
```lambda
type(value)       // Get type of value
len(collection)   // Get length
```

**Math Functions:**
```lambda
abs(x)            // Absolute value
min(a, b, c)      // Minimum value
max(a, b, c)      // Maximum value
sum([1, 2, 3])    // Sum of array
avg([1, 2, 3])    // Average of array
round(x)  floor(x)  ceil(x)  // Rounding
```

**Date/Time Functions:**
```lambda
datetime()        // Current date/time
today()           // Current date
justnow()         // Current time
date(dt)          // Extract date part
time(dt)          // Extract time part
```

**Collection Functions:**
```lambda
slice(arr, start, end)    // Array slice
set(arr)                  // Remove duplicates
all([true, false])        // All true?
any([false, true])        // Any true?
```

**I/O Functions:**
```lambda
input(file, format)       // Parse file
print(value)              // Print to console
format(data, type)        // Format output
error(message)            // Create error
```

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

## Modules & Imports

**Import Syntax:**
```lambda
import module_name;               // Basic import
import .relative_module;          // Relative import
import alias: module_name;        // Import with alias
import mod1, mod2, alias: mod3;   // Multiple imports
```

**Module Usage Example:**
```lambda
// In math_utils.ls: 
pub PI = 3.14159; 
pub fn square(x) => x * x;

// In main.ls: 
import math: .math_utils; 
let area = math.PI * math.square(radius);
```
## Error Handling

**Creating Errors:**
```lambda
error("Something went wrong")   // Create error value
```

**Error Checking:**
```lambda
let result = risky_operation();
if (result is error) { print("Error:", result) } 
else { print("Success:", result) }
```

## Advanced Features

**Type Declarations:**
```lambda
type User = {name: string, age: int};   // Object type
type Point = (float, float);            // Tuple type
type Result = int | error;              // Union type
```

**Comprehensions - Complex data processing:**
```lambda
(let data = [1, 2, 3, 4, 5],
 let filtered = (for (x in data) 
   if (x % 2 == 0) x else 0),
 let doubled = (for (x in filtered) x * 2), doubled)
```

## Operator Precedence (High to Low)
1. `()` `[]` `.` - Primary expressions
2. `-` `+` `not` - Unary operators
3. `^` - Exponentiation
4. `*` `/` `_/` `%` - Multiplicative
5. `+` `-` - Additive
6. `<` `<=` `>` `>=` - Relational
7. `==` `!=` - Equality
8. `and` - Logical AND
9. `or` - Logical OR
10. `to` - Range
11. `|` `&` `!` - Set operations
12. `is` `in` - Type operations

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
fn factorial(n: int) -> int {
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
