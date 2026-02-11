# Lambda Script Syntax

This document covers the fundamental syntax elements of Lambda Script: comments, basic structure, identifiers, names, symbols, and namespaces.

## Table of Contents

1. [Comments](#comments)
2. [Basic Structure](#basic-structure)
3. [Whitespace and Line Breaks](#whitespace-and-line-breaks)
4. [Identifiers](#identifiers)
5. [Names and Symbols](#names-and-symbols)
6. [Namespaces](#namespaces)

---

## Comments

Lambda supports both single-line and multi-line comments:

```lambda
// Single-line comment

/*
   Multi-line comment
   can span multiple lines
*/
```

Comments are ignored by the compiler and used for documentation purposes.

---

## Basic Structure

Lambda Script files contain expressions and statements:

```lambda
// Expressions (return values)
42
"hello world"
[1, 2, 3]

// Statements (declarations and control flow)
let x = 10;
pn main() {
	if (x > 5) {
	    print("x is greater than 5")
	}
}
```

### Expressions vs Statements

- **Expressions** evaluate to a value and can be used anywhere a value is expected
- **Statements** compute values or perform actions
- Most constructs in Lambda are expressions, enabling functional composition

### Expression vs Statement Forms

Several key constructs have both expression and statement forms:

| Construct | Expression Form            | Notes                          | Statement Form           | Notes                              |
| --------- | -------------------------- | ------------------------------ | ------------------------ | ---------------------------------- |
| **if**    | `if (cond) expr else expr` | Returns value; `else` required | `if cond { ... }`        | Executes block; `else` optional    |
| **let**   | `let x = val; expr`        | Binds `x` in following expr    | `let x = val`            | Binds `x` in current block of code |
| **for**   | `for (x in items) expr`    | Returns list of results        | `for x in items { ... }` | Executes block for each            |
| **fn**    | `fn name(x) => expr`       | Expr as function body          | `fn name(x) { ... }`     | Statements as function body        |

```lambda
// Expression forms (return values)
let result = if (x > 0) "positive" else "negative"
let doubled = (for (n in nums) n * 2)
let add = fn(a, b) => a + b

// Statement forms (execute actions)
if x > 0 { print("positive") }
for n in nums { print(n) }
pn greet(name) { print("Hello, " ++ name) }
```

### Statement Types

Lambda has two categories of statements:

| Type           | Description                                                                | Allowed In         |
| -------------- | -------------------------------------------------------------------------- | ------------------ |
| **Functional** | Flow control or compute values (`let`, `if`, `for`, `return`)              | Both `fn` and `pn` |
| **Procedural** | Carry out side-effect actions (`var`, assignment, `while`, I/O operations) | Only `pn`          |

**Functional statements** are pure — they compute values without side effects:

```lambda
// Allowed in both fn and pn
let x = compute(data)
if x > 0 { "positive" } else { "negative" }
for item in items { transform(item) }
```

**Procedural statements** perform actions that modify state or interact with the outside world:

```lambda
// Only allowed in pn (procedural functions)
var counter = 0
counter = counter + 1
data |> "/tmp/output.json"
io.mkdir("./output")
```

This distinction enforces functional purity in `fn` functions while allowing controlled side effects in `pn` procedures.

---

## Whitespace and Line Breaks

- Whitespace is generally ignored except in strings
- Line breaks can separate statements
- Semicolons (`;`) terminate statements but are optional when followed by a line break

```lambda
// These are equivalent:
let a = 1; let b = 2;

let a = 1
let b = 2
```

---

## Identifiers

Identifiers are names used for variables, functions, types, and other declarations.

### Rules

- Must start with a letter (`a-z`, `A-Z`) or underscore (`_`)
- Can contain letters, digits (`0-9`), and underscores
- Are case-sensitive (`foo` and `Foo` are different)
- Cannot be reserved keywords

```lambda
// Valid identifiers
let name = "Alice"
let _private = 42
let camelCase = true
let snake_case = false
let PascalCase = "type"

// Invalid identifiers
// let 123abc = 1      // Cannot start with digit
// let my-var = 1      // Hyphens not allowed (use my_var)
// let let = 1         // Reserved keyword
```

### Reserved Keywords

The following are reserved and cannot be used as identifiers:

```
let   pub   fn    pn    if    else   for   while
in    to    by    where order  group  limit offset
and   or    not   is    as    true   false null
type  import namespace raise  var   break  continue  return
```

### Reserved Type Names

Built-in type names are also reserved:

```
int    int64   float   decimal  bool   string  symbol  binary
list   array   map     element  range  path    type    any
null   error   datetime
```

---

## Names and Symbols

Lambda distinguishes between **strings**, **symbols**, and **names** — each serving different purposes in the language.

### Strings

Strings are UTF-8 text values enclosed in double quotes:

```lambda
"hello world"
"line1\nline2"        // with escape sequences
"unicode: 你好"       // Unicode supported
```

### Symbols

Symbols are interned identifiers enclosed in single quotes. They are used for:
- Attribute keys in maps and elements
- Tag names in elements
- Format specifiers
- Enumeration-like values

```lambda
// Symbol literals
'hello'
'json'
'content-type'

// Common uses
let format = 'json'
let tag = 'div'
{name: "Alice", type: 'user'}
```

**Key differences from strings:**
- Symbols are interned (only one copy exists in memory)
- Faster equality comparison (pointer comparison)
- Used for structural identifiers, not arbitrary text

### Symbol Operations

```lambda
// Equality
'hello' == 'hello'     // true (same symbol)
'hello' == 'world'     // false

// Type checking
type('hello')          // symbol
'hello' is symbol      // true
'hello' is string      // false

// Conversion
string('hello')        // "hello"
symbol("hello")        // 'hello'

// String operations work on symbols
len('hello')           // 5
'hello' ++ 'world'     // 'helloworld'
starts_with('hello', 'hel')  // true
```

---

## Namespaces

Namespaces solve name collisions when mixing markup vocabularies (e.g., SVG inside HTML, MathML inside XHTML). Lambda uses `.` (dot) as the namespace separator, consistent with member access syntax.

### Namespace Declaration

Namespace declarations are global statements that bind a short prefix to a namespace URL:

```lambda
// Single namespace declaration
namespace svg: 'http://www.w3.org/2000/svg'

// Multiple namespace declarations
namespace svg: 'http://www.w3.org/2000/svg', xlink: 'http://www.w3.org/1999/xlink'
```

**Syntax:** `namespace` prefix `:` url (`,` prefix `:` url)*

### Namespace Prefixes as Reserved Names

Once declared, namespace prefixes are **reserved** — no variable, function, type, or field name may use the same name:

```lambda
namespace svg: 'http://www.w3.org/2000/svg'

let svg = 123       // ERROR: 'svg' conflicts with namespace prefix
fn svg() => ...     // ERROR: 'svg' conflicts with namespace prefix
```

### Namespaced Element Tags

Use `ns.name` form for namespaced element tags:

```lambda
namespace svg: 'http://www.w3.org/2000/svg'

// Namespaced element tags
<svg.rect>
<svg.circle>
<svg.path>

// Unqualified tags work as before
<div>
<span>
```

### Namespaced Attributes

Attribute names can also be namespaced:

```lambda
namespace svg: 'http://www.w3.org/2000/svg'
namespace xlink: 'http://www.w3.org/1999/xlink'

// Namespaced attributes
<svg.rect svg.width: 100, svg.height: 50>
<svg.a xlink.href: "https://example.com"; "Click">

// Mixed namespaced and regular attributes
<svg.rect id: "myRect", svg.width: 100>
```

### Namespaced Member Access

When accessing namespaced attributes on elements, use the `e.ns.attr` form:

```lambda
namespace svg: 'http://www.w3.org/2000/svg'

let elem = <svg.rect svg.width: 100, svg.height: 50>

// Access namespaced attributes
elem.svg.width      // 100
elem.svg.height     // 50
```

### Namespaced Symbols

The `ns.name` form in expression context creates a qualified symbol:

```lambda
namespace svg: 'http://www.w3.org/2000/svg'

// Qualified symbols
svg.rect           // 'svg.rect' (with namespace target attached)
svg.circle         // 'svg.circle'

// Comparison
svg.rect == svg.rect     // true (same namespace and name)
svg.rect == svg.circle   // false (different name)

// String representation
string(svg.rect)         // "svg.rect"
```

### Dynamic Namespaced Symbol Construction

For programmatic construction of namespaced symbols, use the two-argument `symbol()` function:

```lambda
// Construct namespaced symbol dynamically
let s = symbol("href", 'http://www.w3.org/1999/xlink')

// Use for dynamic attribute access
let url = elem[s]

// The symbol carries namespace identity
type(s)            // symbol
```

### Namespace Scope

Namespaces are **file-local** — they cannot be imported or exported. Each file declares its own namespace prefixes independently:

```lambda
// file_a.ls
namespace svg: 'http://www.w3.org/2000/svg'
pub elem = <svg.rect svg.width: 100>

// file_b.ls
import a: .file_a
// 'svg' prefix is NOT available here
// but a.elem still has correct namespace data

namespace s: 'http://www.w3.org/2000/svg'  // can use different prefix
<s.circle>  // works fine
```

### Comparison Semantics

Namespaced symbols are compared by both local name AND namespace URL (semantic comparison):

```lambda
namespace svg: 'http://www.w3.org/2000/svg'
namespace s: 'http://www.w3.org/2000/svg'  // same URL, different prefix

svg.rect == s.rect    // true (same namespace URL)
svg.rect == 'svg.rect'  // false (one has namespace, one doesn't)
```

### Example: SVG Document

```lambda
namespace svg: 'http://www.w3.org/2000/svg'
namespace xlink: 'http://www.w3.org/1999/xlink'

let drawing = <svg.svg svg.width: 200, svg.height: 200;
    <svg.rect svg.x: 10, svg.y: 10, svg.width: 80, svg.height: 80, fill: "blue">
    <svg.circle svg.cx: 150, svg.cy: 50, svg.r: 40, fill: "red">
    <svg.a xlink.href: "https://example.com";
        <svg.text svg.x: 100, svg.y: 150; "Click me">
    >
>
```

---

## See Also

- **[Lambda_Data.md](Lambda_Data.md)** — Literals, arrays, lists, maps, elements
- **[Lambda_Type.md](Lambda_Type.md)** — Type system and type annotations
- **[Lambda_Expr_Stam.md](Lambda_Expr_Stam.md)** — Expressions and statements
- **[Lambda_Func.md](Lambda_Func.md)** — Functions and closures
