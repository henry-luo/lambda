# Lambda Data: Literals and Collections

This document covers Lambda's literal values, collection types, and expressions for composing data structures.

> **Related Documentation**:
> - [Lambda Reference](Lambda_Reference.md) — Language overview and syntax
> - [Lambda Type System](Lambda_Type.md) — Type hierarchy and patterns
> - [Lambda Expressions](Lambda_Expr_Stam.md) — Expressions and statements

---

## Table of Contents

1. [Data Types Overview](#data-types-overview)
2. [Primitive Literals](#primitive-literals)
   - [Numeric Literals](#numeric-literals)
   - [String Literals](#string-literals)
   - [Symbol Literals](#symbol-literals)
   - [Binary Literals](#binary-literals)
   - [DateTime Literals](#datetime-literals)
     - [DateTime Sub-Types](#datetime-sub-types)
     - [DateTime Member Properties](#datetime-member-properties)
     - [DateTime Member Functions](#datetime-member-functions)
     - [DateTime Constructors](#datetime-constructors)
   - [Boolean and Null Literals](#boolean-and-null-literals)
3. [Path Literals](#path-literals)
4. [Collections](#collections)
   - [Lists](#lists)
   - [Arrays](#arrays)
   - [Maps](#maps)
   - [Elements](#elements)
   - [Ranges](#ranges)
5. [Collection Comprehensions](#collection-comprehensions)
6. [Data Composition Expressions](#data-composition-expressions)

---

## Data Types Overview

Lambda Script has a rich type system with both primitive and composite types:

### Primitive Types

| Type       | Description                 | Example               |
| ---------- | --------------------------- | --------------------- |
| `null`     | Null/void value             | `null`                |
| `bool`     | Boolean values              | `true`, `false`       |
| `int`      | 56-bit signed integers      | `42`, `-123`          |
| `float`    | 64-bit floating point       | `3.14`, `1.5e-10`     |
| `decimal`  | Arbitrary precision decimal | `123.456n`,`456.789N` |
| `string`   | UTF-8 text strings          | `"hello"`             |
| `symbol`   | Interned identifiers        | `'symbol'`            |
| `binary`   | Binary data                 | `b'\xDEADBEEF'`       |
| `datetime` | Date and time values        | `t'2025-01-01'`       |

### Composite Types

| Type       | Description                 | Example                                 |
| ---------- | --------------------------- | --------------------------------------- |
| `list`     | Immutable ordered sequences | `(1, 2, 3)`                             |
| `array`    | Mutable ordered collections | `[1, 2, 3]`                             |
| `map`      | Key-value mappings          | `{key: "value"}`                        |
| `element`  | Structured markup elements  | `<tag attr: value; content>`            |
| `range`    | Numeric ranges              | `1 to 10`                               |
| `path`     | File paths and URLs         | `/etc.hosts`, `https.'api.example.com'` |
| `function` | Functions                   | `(x) => x + 1`                          |
| `type`     | Type descriptors            | `int`, `string`                         |


### Special Types

- `any` — Any type (top type)
- `error` — Error values
- `number` — Numeric union type (`int | float`)

---

## Primitive Literals

### Boolean and Null Literals

```lambda
true
false
null
```

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
123.456n       // decimal128
-789.012N      // unlimited precision decimal
```

**Special float values**:
- `inf` — Positive infinity
- `-inf` — Negative infinity
- `nan` — Not a number

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

**Supported escape sequences**:

| Escape | Character |
|--------|-----------|
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\\` | Backslash |
| `\"` | Double quote |

### Symbol Literals

Symbols are interned identifiers, often used as keys or tags:

```lambda
'identifier
'symbol-name
'CamelCase
'json
'markdown
```

**Symbol vs String**:
- Symbols are interned (only one copy exists in memory)
- Comparison is O(1) pointer equality
- Used for type tags, format identifiers, map keys

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

DateTime literals use the `t'...'` syntax:

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

#### DateTime Sub-Types

Lambda has three datetime-related types in a sub-type hierarchy:

```
         datetime
        /        \
     date        time
```

- `date` — Date-only values (year, month, day)
- `time` — Time-only values (hour, minute, second, millisecond)
- `datetime` — Full date and time values

```lambda
// Type checking
t'2025-04-26' is date        // true  (date-only precision)
t'2025-04-26' is datetime    // true  (date <: datetime)
t'10:30:00' is time          // true  (time-only precision)
t'10:30:00' is datetime      // true  (time <: datetime)
t'2025-04-26T10:30' is date  // false (has both date and time)
t'2025-04-26T10:30' is time  // false (has both date and time)
```

#### DateTime Member Properties

Access datetime components using dot notation:

**Date Properties:**

```lambda
let dt = t'2025-04-26T10:30:45.123+05:30'

dt.year          // 2025       (int)
dt.month         // 4          (int, 1–12)
dt.day           // 26         (int, 1–31)
dt.weekday       // 6          (int, 0=Sunday, 6=Saturday)
dt.yearday       // 116        (int, 1–366, day of year)
dt.week          // 17         (int, ISO week number 1–53)
dt.quarter       // 2          (int, 1–4)
```

**Time Properties:**

```lambda
dt.hour          // 10         (int, 0–23)
dt.minute        // 30         (int, 0–59)
dt.second        // 45         (int, 0–59)
dt.millisecond   // 123        (int, 0–999)
```

**Timezone Properties:**

```lambda
dt.timezone      // 330        (int, offset in minutes from UTC)
dt.utc_offset    // '+05:30    (symbol, formatted offset)
dt.is_utc        // false      (bool)
```

**Meta Properties:**

```lambda
dt.unix          // 1745635845123 (int, Unix timestamp in milliseconds)
dt.is_date       // true       (bool, has date component)
dt.is_time       // true       (bool, has time component)
dt.is_leap_year  // false      (bool)
dt.days_in_month // 30         (int, days in the datetime's month)
```

**Extraction & Conversion Properties:**

```lambda
dt.date          // t'2025-04-26'          (extract date part)
dt.time          // t'10:30:45'            (extract time part)
dt.utc           // t'2025-04-26T05:00:45Z' (convert to UTC)
dt.local         // convert to local timezone
```

#### DateTime Member Functions

**Formatting:**

```lambda
let dt = t'2025-04-26T10:30:45'

// Custom format patterns
dt.format("YYYY-MM-DD")              // "2025-04-26"
dt.format("DD/MM/YYYY")              // "26/04/2025"
dt.format("hh:mm A")                 // "10:30 AM"
dt.format("YYYY-MM-DD hh:mm:ss")     // "2025-04-26 10:30:45"
dt.format("MMM DD, YYYY")            // "Apr 26, 2025"

// Predefined format names (symbol argument)
dt.format('iso)                      // "2025-04-26T10:30:45"
dt.format('human)                    // "April 26, 2025 10:30 AM"
dt.format('date)                     // "2025-04-26"
dt.format('time)                     // "10:30:45"
```

**Format Pattern Tokens:**

| Token | Meaning | Example |
|-------|---------|---------|
| `YYYY` | 4-digit year | `2025` |
| `YY` | 2-digit year | `25` |
| `MM` | 2-digit month | `04` |
| `M` | Month without padding | `4` |
| `MMM` | Abbreviated month name | `Apr` |
| `MMMM` | Full month name | `April` |
| `DD` | 2-digit day | `26` |
| `D` | Day without padding | `26` |
| `ddd` | Abbreviated weekday | `Sat` |
| `dddd` | Full weekday | `Saturday` |
| `hh` | 2-digit hour (24h) | `10` |
| `h` | Hour without padding | `10` |
| `HH` | 2-digit hour (12h) | `10` |
| `mm` | 2-digit minute | `30` |
| `ss` | 2-digit second | `45` |
| `SSS` | 3-digit millisecond | `000` |
| `A` | AM/PM | `AM` |
| `Z` | UTC offset | `+00:00` |
| `ZZ` | UTC offset compact | `+0000` |

#### DateTime Constructors

**`datetime(...)` — Full Constructor:**

```lambda
// Current date and time
datetime()                           // current UTC datetime

// Parse from string
datetime("2025-04-26T10:30:00")      // parse ISO 8601 string

// From components
datetime(2025, 4, 26)                // date only: t'2025-04-26'
datetime(2025, 4, 26, 10, 30)        // date + time: t'2025-04-26 10:30'
datetime(2025, 4, 26, 10, 30, 45)    // full: t'2025-04-26 10:30:45'

// From Unix timestamp
datetime(1714100000)                 // from Unix timestamp (seconds)

// From map
datetime({year: 2025, month: 4, day: 26, hour: 10, minute: 30})
```

**`date(...)` — Date Constructor:**

```lambda
// Current date
date()                   // same as today()

// Extract from datetime
date(some_datetime)      // extract date part

// From components
date(2025, 4, 26)        // t'2025-04-26'
date(2025, 4)            // t'2025-04'   (year-month)
date(2025)               // t'2025'      (year only)

// Parse from string
date("2025-04-26")       // parse date string
```

**`time(...)` — Time Constructor:**

```lambda
// Current time
time()                   // same as justnow()

// Extract from datetime
time(some_datetime)      // extract time part

// From components
time(10, 30)             // t'10:30'
time(10, 30, 45)         // t'10:30:45'
time(10, 30, 45, 500)    // t'10:30:45.500'

// Parse from string
time("10:30:45")         // parse time string
```

**Related Functions:**

```lambda
today()                  // current date (DATE_ONLY precision)
justnow()                // current time (TIME_ONLY precision)
```

---

## Path Literals

The `path` type represents file system paths and URLs in a unified, platform-independent way. Paths use **dot notation** for segment separation.

### Path Syntax

```lambda
// Absolute file paths (start with /)
/etc.hosts                    // /etc/hosts
/home.user.documents          // /home/user/documents
/usr.local.bin.lambda         // /usr/local/bin/lambda

// Relative paths (start with . or ..)
.config.json                  // ./config.json
.src.main.ls                  // ./src/main.ls
..parent.file                 // ../parent/file

// Quoted segments (for names containing dots or special chars)
/var.log.'app.log'            // /var/log/app.log
.data.'my-file.json'          // ./data/my-file.json
/home.user.'Documents and Settings'  // Spaces in names

// HTTP/HTTPS URLs
http.api.github.com.users     // http://api.github.com/users
https.example.com.data        // https://example.com/data
https.httpbin.org.json        // https://httpbin.org/json

// System paths
sys.env.HOME                  // Environment variable $HOME
sys.env.PATH                  // Environment variable $PATH
sys.platform                  // Operating system platform

// Wildcards in paths
.src.*                        // Single-level: all in ./src
.test.**                      // Recursive: all under ./test
/var.log.'*.log'              // Pattern in filename
```

### Path Schemes

| Scheme             | Root Syntax | Example                  | Resolves To                 |
| ------------------ | ----------- | ------------------------ | --------------------------- |
| Absolute file      | `/`         | `/etc.hosts`             | `/etc/hosts`                |
| Relative (current) | `.`         | `.src.main`              | `./src/main`                |
| Relative (parent)  | `..`        | `..shared.lib`           | `../shared/lib`             |
| HTTP               | `http`      | `http.'api.example.com'` | `http://api.example.com`    |
| HTTPS              | `https`     | `https.'secure.api'`     | `https://secure.api`        |
| System             | `sys`       | `sys.env.PATH`           | System environment variable |

### Wildcards

Paths support glob-style wildcards for pattern matching:

```lambda
// Single-level wildcard (*)
.src.*                        // All items directly in ./src
/var.log.*                    // All items in /var/log

// Recursive wildcard (**)
.test.**                      // All items recursively under ./test
/home.user.documents.**       // All files recursively
```

### Path Concatenation

Paths can be concatenated using the `++` operator:

```lambda
let base = /home.user
let config = base ++ "config" ++ "settings.json"
// Result: /home.user.config.'settings.json'

let project = .src
let file = project ++ "main.ls"
// Result: .src.'main.ls'
```

### Path vs String

| Feature | Path | String |
|---------|------|--------|
| Syntax | `/etc.hosts` | `"/etc/hosts"` |
| Type | `path` | `string` |
| URL support | Native | Requires parsing |
| Wildcards | Built-in (`*`, `**`) | Manual |
| Lazy loading | Yes | No |
| Platform-independent | Yes | Separator varies |

```lambda
// Path literal - cross-platform, supports URLs
let p = /home.user.config

// String - platform-specific
let s = "/home/user/config"

// Both work with input()
let data1 = input(p, 'json)
let data2 = input(s, 'json)
```

### Path Operations

```lambda
// Check existence
exists(/etc.hosts)            // true or false
exists(.config.json)          // true or false

// Load content
let content = input(/etc.hosts, 'text)    // Load file content
let data = input(https.api.example.com.data, 'json)  // Fetch URL
```

### System Info Paths (`sys.*`)

The `sys` path scheme provides cross-platform access to system information. Values are **lazily resolved** — data is only fetched when accessed.

#### System Info Categories

| Path | Type | Description |
|------|------|-------------|
| `sys.os` | Map | Operating system information |
| `sys.cpu` | Map | CPU/processor information |
| `sys.memory` | Map | Memory statistics |
| `sys.proc` | Map | Process information |
| `sys.home` | Path | User home directory |
| `sys.temp` | Path | System temporary directory |
| `sys.time` | Map | Current time and timezone |
| `sys.lambda` | Map | Lambda runtime information |
| `sys.locale` | Map | Locale settings |

#### OS Information (`sys.os.*`)

```lambda
sys.os.name              // "Darwin", "Linux", "Windows"
sys.os.version           // "23.2.0", "6.5.0", "10.0.22631"
sys.os.kernel            // Full kernel version string
sys.os.platform          // "darwin", "linux", "windows"
sys.os.hostname          // Machine hostname
```

#### CPU Information (`sys.cpu.*`)

```lambda
sys.cpu.model            // "Apple M2", "Intel Core i7-12700K"
sys.cpu.cores            // Number of CPU cores (e.g., 8)
sys.cpu.architecture     // "arm64", "x86_64"
```

#### Memory Information (`sys.memory.*`)

```lambda
sys.memory.total         // Total memory in bytes
sys.memory.free          // Free memory in bytes
sys.memory.used          // Used memory in bytes
```

#### Process Information (`sys.proc.*`)

```lambda
// Current process
sys.proc.self.pid        // Current process ID
sys.proc.self.ppid       // Parent process ID
sys.proc.self.uid        // User ID (Unix)
sys.proc.self.gid        // Group ID (Unix)
sys.proc.self.argv       // Command-line arguments as list
sys.proc.self.cwd        // Current working directory (as Path)
sys.process              // Alias for sys.proc.self

// Environment variables
sys.proc.self.env        // Map of all environment variables
sys.proc.self.env.PATH   // Specific variable: $PATH
sys.proc.self.env.HOME   // Specific variable: $HOME

// System uptime
sys.proc.uptime          // System uptime in seconds
```

#### Directory Paths (`sys.home`, `sys.temp`)

These return actual `path` values that can be used for file operations:

```lambda
sys.home                 // User home directory (e.g., /Users/alice)
sys.temp                 // Temp directory (e.g., /tmp)

// Path composition with sys paths
let config = sys.home ++ ".config" ++ "app.json"
// Result: /Users/alice/.config.'app.json'

let tempfile = sys.temp ++ "output.txt"
// Result: /tmp.'output.txt'
```

#### Time Information (`sys.time.*`)

```lambda
sys.time.now             // Current datetime
sys.time.zone            // Timezone name (e.g., "America/Los_Angeles")
sys.time.offset          // UTC offset in seconds
```

#### Lambda Runtime (`sys.lambda.*`)

```lambda
sys.lambda.version       // Lambda version (e.g., "0.9.0")
sys.lambda.build         // Build date
sys.lambda.features      // List of enabled features
```

#### Full Example

```lambda
// Build a system report
let report = {
    os: sys.os.name ++ " " ++ sys.os.version,
    cpu: sys.cpu.model,
    cores: sys.cpu.cores,
    memory_gb: sys.memory.total / (1024 ^ 3),
    home: sys.home,
    user: sys.proc.self.env.USER
}

// Check if running on macOS
if (sys.os.platform == "darwin") {
    // macOS-specific code
}

// Access environment variables
let path_dirs = split(sys.proc.self.env.PATH, ":")
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

// Empty list
()

// Single-element list
(42,)

// Nested lists
((1, 2), (3, 4), (5, 6))
```

#### List Access

```lambda
let lst = (1, 2, 3);
lst[0]    // First element: 1
lst[1]    // Second element: 2
```

#### List Operations

```lambda
len(lst)           // Length: 3
head(lst)          // First: 1
tail(lst)          // Rest: (2, 3)
lst ++ (4, 5)      // Concatenate: (1, 2, 3, 4, 5)
```

### Arrays

Mutable ordered collections:

```lambda
// Array creation
[1, 2, 3]
["a", "b", "c"]
[true, false, null]

// Empty array
[]

// Mixed-type arrays
[42, "hello", 3.14, true, null]

// Nested arrays
[[1, 2], [3, 4], [5, 6]]
```

#### Array Access

```lambda
let arr = [10, 20, 30];
arr[0]     // 10
arr[1]     // 20
arr[-1]    // 30 (last element)
arr[-2]    // 20 (second to last)
```

#### Array Slicing

```lambda
let arr = [0, 1, 2, 3, 4, 5];
arr[1 to 4]      // [1, 2, 3, 4] — elements 1 to 4
```

#### Array Operations

```lambda
len(arr)           // 3
arr ++ [4, 5]      // Concatenate: [10, 20, 30, 4, 5]
reverse(arr)       // [30, 20, 10]
sort(arr)          // [10, 20, 30]
unique([1,1,2,2])  // [1, 2]
```

### Maps

Key-value mappings with structural typing:

```lambda
// Map creation
{key: "value"}
{name: "Alice", age: 30, active: true}

// Mixed key types
{"string_key": 1, symbol_key: 2}

// Nested maps
{
    user: {name: "Bob", email: "bob@example.com"},
    settings: {theme: "dark", notifications: true}
}

// Empty map
{}
```

#### Map Access

```lambda
let person = {name: "Charlie", age: 25};
person.name      // "Charlie" — dot notation
person["name"]   // "Charlie" — bracket notation
person.age       // 25

// Safe key access, like JS optional chaining ?. (rerturn `null` if key missing)
person.address   // null (key doesn't exist)
```

#### Map Operations

```lambda
len(person)               // 2
keys(person)              // ['name, 'age]
values(person)            // ["Charlie", 25]

// Spread operator for merging
let updated = {...person, age: 26}
// {name: "Charlie", age: 26}
```

### Elements

Structured markup elements with attributes and content, used for document processing:

```lambda
// Basic element
<tag>

// Element with attributes
<div id: "main", class: "container">

// Element with content (after semicolon)
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

#### Element Access

```lambda
let el = <div class: "main"; "content">;
el.tag       // 'div
el.class     // "main" (attribute access)
el[0]        // "content" (content/child access)
```

### Ranges

Numeric sequences for iteration:

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

## Collection Comprehensions

### For Expressions

For expressions iterate over collections and return new collections:

```lambda
// Array iteration
(for (x in [1, 2, 3]) x * 2)  // [2, 4, 6]

// Range iteration
(for (i in 1 to 5) i * i)     // [1, 4, 9, 16, 25]

// Conditional iteration
(for (num in [1, 2, 3, 4, 5])
    if (num % 2 == 0) num else null)  // [null, 2, null, 4, null]
```

### Extended For-Expression Clauses

For expressions support additional clauses for filtering, sorting, and limiting:

```lambda
// Where clause — filter
for (x in [1, 2, 3, 4, 5] where x > 2) x
// Result: (3, 4, 5)

// Let clause — intermediate bindings
for (x in [1, 2, 3], let squared = x * x) squared + 1
// Result: (2, 5, 10)

// Order by clause — sort
for (x in [3, 1, 4, 1, 5] order by x) x
// Result: (1, 1, 3, 4, 5)

// Limit and offset — pagination
for (x in [1, 2, 3, 4, 5] limit 3 offset 1) x
// Result: (2, 3, 4)

// Combined clauses
for (x in [10, 5, 15, 20, 25, 3],
     let squared = x * x
     where x > 5
     order by squared desc
     limit 2)
  squared
// Result: (625, 400)
```

### Pipe Expressions

The pipe operator enables fluent data transformation:

```lambda
// Map over collection
[1, 2, 3] | ~ * 2           // [2, 4, 6]

// Extract field from each item
users | ~.name              // ["Alice", "Bob", ...]

// Chained transformations
[1, 2, 3, 4, 5]
    | ~ ^ 2                 // square each
    | ~ + 1                 // add 1
// Result: [2, 5, 10, 17, 26]

// Filter with where
[1, 2, 3, 4, 5] where ~ > 3  // [4, 5]
```

---

## Data Composition Expressions

### Spread Operator

The spread operator (`...`) expands collections:

```lambda
// Array spread
let a = [1, 2, 3]
let b = [0, ...a, 4]        // [0, 1, 2, 3, 4]

// Map spread (merge)
let base = {x: 1, y: 2}
let extended = {...base, z: 3}  // {x: 1, y: 2, z: 3}

// Override values
let updated = {...base, x: 10}  // {x: 10, y: 2}
```

### Concatenation

```lambda
// Array concatenation
[1, 2] ++ [3, 4]            // [1, 2, 3, 4]

// String concatenation
"hello" ++ " " ++ "world"   // "hello world"

// List concatenation
(1, 2) ++ (3, 4)            // (1, 2, 3, 4)

// Path concatenation
/home.user ++ "config"      // /home.user.config
```

### Conditional Construction

```lambda
// Conditional in collections
let items = [
    1,
    2,
    if (condition) 3 else null,  // Conditionally include
    4
]

// Conditional in maps
let config = {
    name: "app",
    debug: if (dev_mode) true else false,
    ...if (has_extra) extra_config else {}
}
```

### Nested Data Construction

```lambda
// Building complex structures
let report = {
    title: "Sales Report",
    generated: datetime(),
    summary: {
        total: sum(sales | ~.amount),
        count: len(sales),
        average: avg(sales | ~.amount)
    },
    items: (for (sale in sales) {
        id: sale.id,
        amount: sale.amount,
        formatted: "$" ++ string(sale.amount)
    })
}
```

---

## Type Coercion and Conversion

### Implicit Coercion

Lambda performs limited implicit coercion:

```lambda
// Int to float in mixed arithmetic
1 + 2.5                     // 3.5 (int promoted to float)

// String concatenation coerces other types
"value: " ++ 42             // "value: 42"
```

### Explicit Conversion

Use conversion functions for explicit type conversion:

```lambda
// To string
string(42)                  // "42"
string(true)                // "true"
string([1, 2, 3])           // "[1, 2, 3]"

// To int
int("123")                  // 123
int(3.14)                   // 3

// To float
float("3.14")               // 3.14
float(42)                   // 42.0

// To symbol
symbol("hello")             // 'hello

// To array
array((1, 2, 3))            // [1, 2, 3]

// To list
list([1, 2, 3])             // (1, 2, 3)
```

---

This document covers the foundational data types and structures in Lambda. For type system details, see [Lambda Type System](Lambda_Type.md). For expressions and control flow, see [Lambda Expressions](Lambda_Expr_Stam.md).
