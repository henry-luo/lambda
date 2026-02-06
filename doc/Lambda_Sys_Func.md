# Lambda System Functions Reference

This document provides comprehensive documentation for all built-in system functions in Lambda Script.

## Table of Contents

1. [Type Functions](#type-functions)
2. [Mathematical Functions](#mathematical-functions)
3. [Statistical Functions](#statistical-functions)
4. [Date/Time Functions](#datetime-functions)
5. [Collection Functions](#collection-functions)
6. [Vector Functions](#vector-functions)
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
len([1, 2, 3])     // 3
len("hello")       // 5
```

---

## Mathematical Functions

Basic mathematical operations.

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
```

---

## Statistical Functions

Functions for statistical analysis on collections.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `sum(vec)` | Sum of elements | `sum([1, 2, 3])` | `6` |
| `avg(vec)` | Arithmetic mean | `avg([1, 2, 3])` | `2.0` |
| `mean(vec)` | Alias for avg | `mean([1, 2, 3])` | `2.0` |
| `median(vec)` | Median value | `median([1, 3, 2])` | `2` |
| `variance(vec)` | Population variance | `variance([1, 2, 3])` | `0.666...` |
| `deviation(vec)` | Standard deviation | `deviation([1, 2, 3])` | `0.816...` |
| `quantile(vec, p)` | p-th quantile | `quantile([1,2,3,4], 0.5)` | `2.5` |
| `prod(vec)` | Product of elements | `prod([2, 3, 4])` | `24` |

```lambda
sum([1, 2, 3, 4])           // 10
avg([1, 2, 3, 4])           // 2.5
mean([1, 2, 3, 4])          // 2.5
median([1, 3, 2, 4, 5])     // 3
variance([1, 2, 3])         // 0.666...
deviation([1, 2, 3])        // 0.816...
quantile([1, 2, 3, 4], 0.5) // 2.5
prod([2, 3, 4])             // 24
```

---

## Element-wise Math Functions

Functions that apply to each element of a collection and return a collection of the same size. Also work on scalar values.

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `sqrt(x)` | Square root | `sqrt([1, 4, 9])` | `[1, 2, 3]` |
| `log(x)` | Natural logarithm | `log([1, 2.718...])` | `[0, 1]` |
| `log10(x)` | Base-10 logarithm | `log10([1, 10, 100])` | `[0, 1, 2]` |
| `exp(x)` | Exponential (e^x) | `exp([0, 1, 2])` | `[1, e, e²]` |
| `sin(x)` | Sine | `sin([0, 1.57...])` | `[0, 1]` |
| `cos(x)` | Cosine | `cos([0, 1.57...])` | `[1, 0]` |
| `tan(x)` | Tangent | `tan([0, 0.785...])` | `[0, 1]` |
| `abs(x)` | Absolute value | `abs([-1, 2, -3])` | `[1, 2, 3]` |
| `round(x)` | Round | `round([1.4, 1.6])` | `[1, 2]` |
| `floor(x)` | Floor | `floor([1.7, 2.3])` | `[1, 2]` |
| `ceil(x)` | Ceiling | `ceil([1.2, 2.8])` | `[2, 3]` |
| `sign(x)` | Sign (-1, 0, 1) | `sign([-5, 0, 3])` | `[-1, 0, 1]` |

```lambda
sqrt([1, 4, 9])            // [1, 2, 3]
log([1, 2.718281828])      // [0, 1]
log10([1, 10, 100])        // [0, 1, 2]
exp([0, 1])                // [1, 2.718...]
sin([0, 3.14159/2])        // [0, 1]
cos([0, 3.14159/2])        // [1, 0]
abs([-1, 2, -3])           // [1, 2, 3]
sign([-5, 0, 3])           // [-1, 0, 1]
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

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `reverse(vec)` | Reverse order | `reverse([1, 2, 3])` | `[3, 2, 1]` |
| `sort(vec)` | Sort ascending | `sort([3, 1, 2])` | `[1, 2, 3]` |
| `sort(vec, 'desc)` | Sort descending | `sort([1, 2, 3], 'desc)` | `[3, 2, 1]` |
| `unique(vec)` | Remove duplicates (preserves order) | `unique([1, 2, 2, 3])` | `[1, 2, 3]` |
| `concat(v1, v2)` | Concatenate vectors | `concat([1, 2], [3, 4])` | `[1, 2, 3, 4]` |
| `take(vec, n)` | First n elements | `take([1, 2, 3], 2)` | `[1, 2]` |
| `drop(vec, n)` | Drop first n elements | `drop([1, 2, 3], 1)` | `[2, 3]` |
| `zip(v1, v2)` | Pair elements | `zip([1, 2], [3, 4])` | `[(1, 3), (2, 4)]` |

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
unique([1, 2, 2, 3, 3])    // [1, 2, 3]
concat([1, 2], [3, 4])     // [1, 2, 3, 4]
take([1, 2, 3, 4], 2)      // [1, 2]
drop([1, 2, 3, 4], 2)      // [3, 4]
zip([1, 2], ["a", "b"])    // [(1, "a"), (2, "b")]

fill(3, 0)                 // [0, 0, 0]
range(0, 10, 2)            // [0, 2, 4, 6, 8]
```

---

## Vector Functions

Functions for vector/array computations. These support **element-wise operations** on numeric vectors.

### Aggregation

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `sum(vec)` | Sum of elements | `sum([1, 2, 3])` | `6` |
| `prod(vec)` | Product of elements | `prod([2, 3, 4])` | `24` |
| `min(vec)` | Minimum element | `min([3, 1, 2])` | `1` |
| `max(vec)` | Maximum element | `max([3, 1, 2])` | `3` |

### Index Operations

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `argmin(vec)` | Index of minimum | `argmin([3, 1, 2])` | `1` |
| `argmax(vec)` | Index of maximum | `argmax([3, 1, 2])` | `0` |

### Cumulative Operations

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `cumsum(vec)` | Cumulative sum | `cumsum([1, 2, 3])` | `[1, 3, 6]` |
| `cumprod(vec)` | Cumulative product | `cumprod([1, 2, 3])` | `[1, 2, 6]` |

### Linear Algebra

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `dot(a, b)` | Dot product | `dot([1,2,3], [4,5,6])` | `32` |
| `norm(vec)` | Euclidean norm | `norm([3, 4])` | `5` |

```lambda
// Aggregation
sum([1, 2, 3, 4])          // 10
prod([2, 3, 4])            // 24

// Index operations
argmin([5, 2, 8, 1])       // 3 (index of 1)
argmax([5, 2, 8, 1])       // 2 (index of 8)

// Cumulative
cumsum([1, 2, 3, 4])       // [1, 3, 6, 10]
cumprod([1, 2, 3, 4])      // [1, 2, 6, 24]

// Linear algebra
dot([1, 2, 3], [4, 5, 6])  // 32 (1*4 + 2*5 + 3*6)
norm([3, 4])               // 5 (sqrt(9 + 16))
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
| `sqrt` | 1 | Square root |
| `log` | 1 | Natural log |
| `log10` | 1 | Base-10 log |
| `exp` | 1 | Exponential |
| `sin` | 1 | Sine |
| `cos` | 1 | Cosine |
| `tan` | 1 | Tangent |

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
| `min` | 1-2 | Minimum |
| `max` | 1-2 | Maximum |

### Vector Functions
| Function | Args | Description |
|----------|------|-------------|
| `cumsum` | 1 | Cumulative sum |
| `cumprod` | 1 | Cumulative product |
| `argmin` | 1 | Index of min |
| `argmax` | 1 | Index of max |
| `dot` | 2 | Dot product |
| `norm` | 1 | Euclidean norm |
| `fill` | 2 | Fill vector |
| `range` | 3 | Range with step |

### Collection Functions
| Function | Args | Description |
|----------|------|-------------|
| `slice` | 3 | Extract slice |
| `set` | 1+ | Remove duplicates |
| `all` | 1 | All truthy |
| `any` | 1 | Any truthy |
| `reverse` | 1 | Reverse order |
| `sort` | 1-2 | Sort |
| `unique` | 1 | Unique elements |
| `concat` | 2 | Concatenate |
| `take` | 2 | Take first n |
| `drop` | 2 | Drop first n |
| `zip` | 2 | Zip vectors |

### I/O Functions (Pure)
| Function | Args | Description |
|----------|------|-------------|
| `input` | 1-2 | Parse file/URL |
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
