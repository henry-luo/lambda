# Lambda Object Type — Design Proposal

**Date:** February 23, 2026  
**Status:** Draft; §16 ratified 2026-09-03 into `Lambda_Formal_Semantics.md` 20.0.0 and `Lambda_Formal_Design.md` 1.39.0 (OB1–OB12; spec-linkage map in §16). §10 is superseded by OB9.  
**Author:** Design collaboration  

> **Related Documentation**:
> - [Lambda Data](../doc/Lambda_Data.md) — Literals and collections
> - [Lambda Type System](../doc/Lambda_Type.md) — Type hierarchy and patterns
> - [Lambda Functions](../doc/Lambda_Func.md) — `fn` and `pn` functions
> - [Lambda Schema](Lambda_Schema.md) — Schema and validation

---

## Table of Contents

1. [Motivation](#motivation)
2. [Design Overview](#design-overview)
3. [Object Type Definition](#object-type-definition)
4. [Object Literals](#object-literals)
5. [Member Access & `this` Semantics](#member-access--this-semantics)
6. [Inheritance](#inheritance)
7. [Type Checking — Nominal](#type-checking--nominal)
8. [Mutability — `fn` vs `pn`](#mutability--fn-vs-pn)
9. [Default Values](#default-values)
10. [Element Extension](#element-extension)
11. [Runtime Data Model](#runtime-data-model)
12. [Pattern Matching](#pattern-matching)
13. [Object Update Syntax](#object-update-syntax)
14. [Validation & Schema](#validation--schema)
15. [Implementation Plan](#implementation-plan)

---

## Motivation

Lambda currently has **maps** (`{a: 1, b: 2}`) for key-value data and **elements** (`<div ...>`) for markup structures. Both are shape-typed via `TypeMap`/`TypeElmt` but lack:

- **Named types** — maps are structurally typed; you cannot distinguish `{x: int, y: int}` used as a `Point` from the same shape used as a `Size`.
- **Methods** — no way to bundle behavior with data. Functions operating on a data type are standalone, requiring the caller to know which functions apply.
- **Encapsulation** — related fields and operations are scattered across the codebase rather than co-located in a single definition.

Objects address all three by introducing **nominally-typed maps with methods**.

---

## Design Overview

| Aspect | Decision |
|--------|----------|
| Runtime type | `LMD_TYPE_OBJECT` (new `EnumTypeId` value) |
| Data model | Object = Map + type name + method table |
| Literal syntax | `{TypeName field: value, ...}` |
| Type definition | `type T { fields; methods }` |
| Self reference | `this` implied; `~` for disambiguation |
| Inheritance | `type T : Base { ... }` and `type T = A & B` |
| Type checking | Nominal only (no structural fallback for plain maps) |
| Mutability | `fn` methods are pure; `pn` methods may mutate |
| Constructors | No dedicated constructor; literals + default values |
| Element extension | `type E < attrs, content; methods >` |

---

## Object Type Definition

### Basic Syntax

```lambda
type Point {
    x: float,
    y: float;

    fn distance(other: Point) => sqrt((x - other.x) ** 2 + (y - other.y) ** 2)

    fn magnitude() => sqrt(x ** 2 + y ** 2)
}
```

**Structure:**
- Fields listed first, comma-separated, terminated by `;`
- Methods follow the `;`, each a full `fn` or `pn` declaration
- No comma between method declarations (they are statement-level)

### Grammar (extends existing `object_type`)

The existing grammar rule:
```js
object_type: $ => seq(
    'type', field('name', $.identifier), '{', _attr_content_type($), '}'
)
```

Needs extension to support method declarations after the field list:

```js
object_type: $ => seq(
    'type', field('name', $.identifier),
    optional(seq(':', field('base', $._type_ref))),  // inheritance
    '{',
    // fields (comma-separated attr list), terminated by ';'
    optional(seq(
        alias($.attr_type, $.attr),
        repeat(seq(',', alias($.attr_type, $.attr))),
    )),
    // ';' separates fields from methods/constraints
    optional(seq(';',
        // method declarations and object-level constraints
        repeat(choice($.fn_stam, $.fn_expr_stam, $.that_constraint))
    )),
    '}'
)
```

### AST Node

```
AST_NODE_OBJECT_TYPE (new)
├── name: identifier ("Point")
├── base: type_ref? (for inheritance)
├── fields: ShapeEntry* (linked list, same as TypeMap)
├── methods: AstNode* (linked list of fn_stam nodes)
└── constraints: AstNode* (linked list of that-constraint expressions)
```

### Formal Syntax

```
ObjectTypeDef  ::= 'type' Identifier (':' TypeRef)? '{' FieldList? (';' BodyList?)? '}'
FieldList      ::= Field (',' Field)*
Field          ::= Identifier ':' TypeExpr ('=' DefaultExpr)?
BodyList       ::= (MethodDecl | ConstraintDecl)+
MethodDecl     ::= ('fn' | 'pn') Identifier '(' ParamList? ')' ReturnType? ('=>' Expr | '{' Body '}')
ConstraintDecl ::= 'that' '(' Expr ')'
```

---

## Object Literals

### Creation Syntax

```lambda
// Named object literal — type name comes first inside braces
let p = {Point x: 3.0, y: 4.0}

// Fields follow the same key:value syntax as maps
let rect = {Rect width: 10, height: 20}

// When a type has default values, omitted fields get defaults
let origin = {Point}   // x: 0.0, y: 0.0 (if defaults defined)
```

### Grammar

```js
object_literal: $ => seq(
    '{', field('type_name', $.identifier),
    optional(seq(
        $.map_item, repeat(seq(',', $.map_item))
    )),
    '}'
)
```

**Disambiguation from map literals:** A map literal starts with `{ key:` (string/symbol/identifier `:` value) or `{ expr }`. An object literal starts with `{ TypeName` where `TypeName` is a known type followed by field assignments. The parser can distinguish because:
- Object literal: `{Identifier Identifier: Expr, ...}` — two identifiers before the first `:`
- Map literal: `{Identifier: Expr, ...}` — single identifier before `:`

### Calling Methods

```lambda
let p = {Point x: 3.0, y: 4.0}
let q = {Point x: 0.0, y: 0.0}

p.magnitude()      // 5.0
p.distance(q)      // 5.0
```

---

## Member Access & `this` Semantics

### Implicit `this`

Inside a method body, fields of the object are directly accessible by name — `this` is implied:

```lambda
type Circle {
    radius: float,
    center: Point;

    fn area() => 3.14159 * radius ** 2       // radius = this.radius
    fn diameter() => radius * 2
    fn contains(p: Point) => center.distance(p) < radius
}
```

### Explicit `~` for Disambiguation

When a parameter name shadows a field name, use `~` to refer to the object (same as `~` in `that` constraints):

```lambda
type Account {
    balance: float,
    name: string;

    pn deposit(amount: float) {
        balance = balance + amount       // unambiguous: balance is a field
    }

    pn rename(name: string) {
        ~.name = name                    // ~.name = field, name = parameter
    }

    fn describe() => name ++ ": " ++ str(balance)
}
```

### Resolution Rules

1. **Parameters first** — if a name matches a parameter, it refers to the parameter
2. **Fields second** — if unmatched, look up the object's fields
3. **Outer scope** — if still unmatched, standard lexical scoping applies
4. **`~` always refers to `this`** — `~.field` is explicit self-access, never ambiguous

This mirrors how `~` works in `that` constraints and pipe expressions — a unified self-reference operator.

---

## Inheritance

### Single Inheritance via `:`

```lambda
type Shape {
    color: string = "black";

    fn describe() => "Shape(color: " ++ color ++ ")"
}

type Circle : Shape {
    radius: float,
    center: Point;

    fn area() => 3.14159 * radius ** 2
    fn describe() => "Circle(r=" ++ str(radius) ++ ", color=" ++ color ++ ")"  // override
}
```

**Semantics:**
- Child inherits all fields from parent (parent fields come first in shape)
- Child inherits all methods from parent
- Child can override methods (last definition wins)
- Single inheritance only — use `&` for composition

### Composition via `&`

```lambda
type Printable {
    fn to_string() => str(~)
}

type Serializable {
    fn to_json() => json(~)
}

// Combine types
type Document = Printable & Serializable & {
    title: string,
    body: string
}
```

When combining with `&`:
- Field sets are merged (union of all fields)
- Overlapping field names must have compatible types
- Methods from all constituent types are available
- For method name conflicts, the rightmost definition wins

---

## Type Checking — Nominal

Object types use **nominal** type checking only. A value must be explicitly created with a type name to be considered an instance of that type. Plain maps never match object types, even if they have the same fields.

### Nominal Checking

```lambda
type Point { x: float, y: float; }
type Size  { x: float, y: float; }   // same shape, different type

let p = {Point x: 1.0, y: 2.0}
let s = {Size  x: 1.0, y: 2.0}

(p is Point)   // true  — nominal match
(p is Size)    // false — different type name
(s is Size)    // true
(s is Point)   // false
```

### Plain Maps Do Not Match Object Types

```lambda
let m = {x: 1.0, y: 2.0}     // plain map, no type name

(m is Point)    // false — plain maps never match object types
(m is map)      // true
(m is object)   // false — m is a plain map, not an object
```

### Subtype Checking

```lambda
type Shape { color: string; }
type Circle : Shape { radius: float; }

let c = {Circle radius: 5.0, color: "red"}

(c is Circle)   // true  — exact type
(c is Shape)    // true  — Circle extends Shape
(c is object)   // true  — all objects are objects
(c is map)      // true  — objects are map-compatible
```

---

## Mutability — `fn` vs `pn`

### Pure Methods (`fn`)

`fn` methods are pure — they cannot modify the object. They return new values:

```lambda
type Point {
    x: float,
    y: float;

    fn translate(dx: float, dy: float) => {Point ~, x: x + dx, y: y + dy}
    fn scale(factor: float) => {Point ~, x: x * factor, y: y * factor}
}

let p = {Point x: 1.0, y: 2.0}
let q = p.translate(3.0, 4.0)    // q = {Point x: 4.0, y: 6.0}, p unchanged
```

### Mutable Methods (`pn`)

`pn` methods can mutate the object's fields in-place:

```lambda
type Counter {
    count: int = 0;

    pn increment() {
        count = count + 1
    }

    pn reset() {
        count = 0
    }

    fn value() => count
}

let c = {Counter}
c.increment()       // c.count is now 1
c.increment()       // c.count is now 2
c.reset()           // c.count is now 0
```

### Method Resolution

Method calls use dot syntax: `obj.method(args)`. The runtime:

1. Looks up `method` in the object's type method table
2. Binds `~` (and implicit field scope) to the object
3. Dispatches the call

If no method is found on the object's type, falls back to:
- Inherited methods (walking the parent chain)
- Standalone functions (UFCS — Uniform Function Call Syntax): `obj.f(args)` ≡ `f(obj, args)`

---

## Default Values

Fields can have default values specified in the type definition:

```lambda
type Config {
    host: string = "localhost",
    port: int = 8080,
    debug: bool = false;

    fn url() => "http://" ++ host ++ ":" ++ str(port)
}

// Use all defaults
let c1 = {Config}                           // host="localhost", port=8080, debug=false

// Override some
let c2 = {Config host: "example.com"}       // port=8080, debug=false still defaulted

// Override all
let c3 = {Config host: "api.io", port: 443, debug: true}
```

### Default Value Rules

- Default expressions are evaluated at object creation time
- Defaults can reference other fields defined earlier (top-to-bottom order):
  ```lambda
  type Rect {
      width: float,
      height: float = width,    // defaults to width (square)
      area: float = width * height;
  }
  ```
- Fields without defaults are required — omitting them in a literal is a compile error

---

## Element Extension

> **Superseded 2026-09-03 by OB9 / S2.1.3.** Elements stay structural and never carry methods; the `entity_type` rule sketched below was never built. What this section wanted — a typed element with behavior — is now what `object` *is* (OB2). Kept for the record.

Elements are extended with methods using the same pattern — semicolon separates attrs/content from methods:

### Extended Element Type Definition

```lambda
type Button < 
    label: string,
    disabled: bool = false,
    InlineContent*;

    fn is_active() => not disabled
    pn disable() { disabled = true }
    pn enable() { disabled = false }
>
```

### Element Grammar Extension

```js
entity_type: $ => seq(
    'type', field('name', $.identifier),
    optional(seq(':', field('base', $._type_ref))),
    '<', 
    // existing attr_content_type...
    _attr_content_type($),
    // new: optional method/constraint declarations after ';'
    optional(seq(';',
        repeat(choice($.fn_stam, $.fn_expr_stam, $.that_constraint))
    )),
    '>'
)
```

### Usage

```lambda
let btn = <Button label: "Submit">Click me</Button>

btn.is_active()     // true
btn.disable()
btn.is_active()     // false
```

### Element `this` Semantics

Inside element methods:
- `~` refers to the element itself
- Attribute names are directly accessible (implicit `this`)
- Content is accessible via `~.content` or iteration over `~`

```lambda
type Heading <
    level: int = 1,
    InlineContent*;

    fn tag_name() => "h" ++ str(level)
    fn text() => ~.content |> map(str) |> join("")
>
```

---

## Runtime Data Model

### New Type ID

```c
enum EnumTypeId {
    // ... existing types ...
    LMD_TYPE_MAP,
    LMD_TYPE_VMAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_OBJECT,      // NEW: object = map + type_name + methods
    LMD_TYPE_TYPE,
    // ...
};
```

### Object Struct

```cpp
struct Object : Container {
    void* type;         // TypeObject* — shape + methods
    void* data;         // packed field data (same layout as Map)
    int data_cap;       // data buffer capacity
};
```

`Object` has the same memory layout as `Map` for field access. The difference is:
- `type_id` is `LMD_TYPE_OBJECT` instead of `LMD_TYPE_MAP`  
- `type` points to `TypeObject*` which includes a method table

### TypeObject

```cpp
typedef struct TypeMethod {
    StrView* name;              // method name
    Function* fn;               // compiled function pointer
    bool is_proc;               // true for pn, false for fn
    struct TypeMethod* next;    // linked list
} TypeMethod;

typedef struct TypeObject : TypeMap {
    // inherits from TypeMap: length, byte_size, type_index, shape, last
    StrView type_name;          // nominal type name ("Point", "Circle")
    TypeObject* base;           // parent type (NULL if no inheritance)
    TypeMethod* methods;        // linked list of methods
    TypeMethod* methods_last;
    int method_count;
    AstNode* constraint;        // object-level that(...) constraint (NULL if none)
    ConstraintFn constraint_fn; // JIT-compiled constraint checker (NULL if none)
} TypeObject;
```

### Compatibility with Maps

Objects are upward-compatible with maps:
- `get_type_id(obj)` returns `LMD_TYPE_OBJECT`
- `(obj is map)` returns `true` (object ⊂ map)
- `map_get()` works on objects (same field layout)
- `item_attr()`, `item_keys()` work on objects
- Functions expecting `Map*` accept `Object*` (field access is identical)

### Method Table

Methods are stored as a linked list of `TypeMethod` entries on the `TypeObject`. Method lookup walks:

1. Own methods (object's TypeObject)
2. Inherited methods (base TypeObject chain)
3. Fallback to UFCS (standalone function lookup)

For performance, frequently-called methods may be cached in a vtable array indexed by method name hash.

---

## Pattern Matching

Objects work naturally with `match` expressions via nominal type checks:

```lambda
type Shape { color: string; }
type Circle : Shape { radius: float; }
type Rect : Shape { width: float, height: float; }

fn area(s) => match s {
    case Circle: 3.14159 * s.radius ** 2
    case Rect: s.width * s.height
    case Shape: 0    // fallback for unknown shapes
}

fn describe(s) => match s {
    case Circle: "Circle r=" ++ str(s.radius) ++ " " ++ s.color
    case Rect: str(s.width) ++ "x" ++ str(s.height)
    default: "unknown"
}
```

### Object Pattern Syntax

`case TypeName:` performs a nominal type check — the matched value `s` retains its fields accessible via dot notation. This is consistent with how `case int:`, `case string:` etc. work today.

```lambda
case Circle:      // matches if value is a Circle (or subtype)
case Shape:       // matches any Shape, including Circle, Rect, etc.
```

Subtype relationships are respected: `case Shape:` matches `Circle` and `Rect` instances.

---

## Object Update Syntax

Since `fn` methods must return new objects, a concise update syntax is valuable:

```lambda
type Point { x: float, y: float; }

let p = {Point x: 1.0, y: 2.0}

// Update syntax: wrap existing object, override specific fields
let q = {Point p, x: 10.0}      // q = {Point x: 10.0, y: 2.0}
```

This follows the same convention as dynamic maps — no spread operator needed. When a bare identifier appears (not a `key: value` pair), it is treated as a source object whose fields are copied into the new object. Subsequent `key: value` pairs override copied fields.

```lambda
// Multiple sources and overrides
let base = {Point x: 1.0, y: 2.0}
let q = {Point base, x: 10.0}   // copies base, overrides x → {Point x: 10.0, y: 2.0}

// Inside fn methods, return updated copies naturally
type Point {
    x: float, y: float;
    fn translate(dx: float, dy: float) => {Point ~, x: x + dx, y: y + dy}
}
```

---

## Validation & Schema

Object types integrate with Lambda's existing validation system. Constraints are placed **inside** the type body — both at the field level and at the object level:

### Field-Level Constraints

Use `that` on individual field types (same as existing constrained types):

```lambda
type User {
    name: string that (len(~) > 0),     // field-level: name must be non-empty
    age: int that (0 <= ~ <= 150),       // field-level: valid age range
    email: string;
}
```

### Object-Level Constraints

Place a bare `that (...)` clause inside the type body (after the fields/methods) to express cross-field or whole-object constraints:

```lambda
type DateRange {
    start: int,
    end: int;

    that (~.end > ~.start)              // object-level: end must be after start
}

type ValidRect {
    width: float that (~ > 0),          // field-level
    height: float that (~ > 0),         // field-level
    area: float;

    fn compute_area() => width * height

    that (~.area == ~.width * ~.height)  // object-level: area must be consistent
}
```

**Semantics:** Object-level `that` uses `~` to refer to the whole object, same as in type-level `that` constraints. Multiple `that` clauses are ANDed together.

### Validation Behavior

```lambda
({DateRange start: 1, end: 5} is DateRange)     // true
({DateRange start: 5, end: 1} is DateRange)     // false — object-level constraint fails

let u = {User name: "Alice", age: 30, email: "alice@example.com"}
(u is User)       // true (nominal + field constraints + object constraints)

let bad = {User name: "", age: 30, email: "bob@example.com"}
(bad is User)     // false — field constraint on name fails
```

### Schema Validation

Object types can be used as schema types:

```lambda
type ApiResponse {
    status: int that (100 <= ~ <= 599),
    body: string | {string: any},
    headers: {string: string}?;
}

validate(response, ApiResponse)   // validates against object schema
```

---

## Implementation Plan

### Phase 1: Runtime Foundation

| Task | Details | Status |
|------|---------|--------|
| Add `LMD_TYPE_OBJECT` | New enum value in `lambda.h`, update `get_type_name()` | Done |
| Define `Object` struct | In `lambda-data.hpp`, extends `Container` with same layout as `Map` | Done |
| Define `TypeObject` | Extends `TypeMap` with `type_name`, `base`, `methods` | Done |
| Define `TypeMethod` | Method entry struct (name, function ptr, is_proc) | Done |
| Update `get_type_id()` | Handle `LMD_TYPE_OBJECT` in all type-dispatch switches | Done |
| Update `is_container()` | Include `LMD_TYPE_OBJECT` | Done |
| Object allocation | `object()` constructor in `lambda-mem.cpp` | Done |

### Phase 2: Grammar & Parser

| Task | Details | Status |
|------|---------|--------|
| Extend `object_type` rule | Add inheritance (`:`) and method declarations | Done |
| Add `object_literal` rule | `{TypeName field: value, ...}` syntax | Done |
| Extend `entity_type` rule | Add method declarations after `;` | — |
| Regenerate parser | `make generate-grammar` | Done |
| Update `ts-enum.h` | Map new grammar symbols to C enums | Done |

### Phase 3: AST Builder

| Task | Details | Status |
|------|---------|--------|
| `build_object_type()` | Build `TypeObject` from CST, handle fields + methods + defaults | Done |
| `build_object_literal()` | Parse `{TypeName ...}` and emit object construction AST | Done |
| Handle inheritance | Merge parent fields/methods into child `TypeObject` | Done |
| Implicit `this` binding | Resolve bare field names in method bodies to object field access | Done |
| `~` self-reference | Wire `~` to object context inside method bodies | Done |

### Phase 4: Transpiler (C + MIR JIT)

| Task | Details | Status |
|------|---------|--------|
| Object construction codegen | Emit code for `Object*` allocation and field packing | Done |
| Method call codegen | Emit dispatch: `obj.method(args)` → lookup + call | Done |
| `pn` mutation codegen | Emit field write-back for mutable methods | Done |
| Default value codegen | Emit default value evaluation for omitted fields | Done |
| Inherit method dispatch | Walk parent chain for inherited methods | Done |

### Phase 5: Runtime Operations

| Task                            | Details                                                         | Status |
| ------------------------------- | --------------------------------------------------------------- | ------ |
| `object_get()` / `object_set()` | Field access (reuse map layout)                                 | Done   |
| `fn_is()` extension             | Nominal type check + parent chain                               | Done   |
| `print()` / `str()` / `json()`  | Object serialization with type name (`"@"` discriminator)       | Done   |
| Pattern matching                | `case TypeName:` in `match` expressions                         | Done   |
| Object wrapping                 | `{TypeName source, overrides...}` (no spread needed)            | Done   |
| Object-level constraints        | `that (...)` inside type body, compiled + checked at `is`       | Done   |
| Ref counting                    | Object deallocation (fields + method table)                     | Done   |
| Member precedence               | Object fields/methods take priority over built-in sys functions | Done   |
| `{TypeName}` empty literal      | All-defaults object with no explicit fields                     | Done   |
| Composition via `&`             | `type T = A & B & { ... }` multi-type composition               | —      |

### Phase 6: Tests

| Task | Details | Status |
|------|---------|--------|
| `test/lambda/object.ls` | Object creation, field access, method calls | Done |
| `test/lambda/object_inherit.ls` | Inheritance, method override, `is` checks | Done |
| `test/lambda/object_mutation.ls` | `pn` method mutation | Done |
| `test/lambda/object_default.ls` | Default field values | Done |
| `test/lambda/object_pattern.ls` | Pattern matching with objects | Done |
| `test/lambda/object_update.ls` | Object update/wrapping syntax | Done |
| `test/lambda/object_constraint.ls` | Field-level and object-level constraints | Done |
| `test/lambda/object_element.ls` | Element type with methods | — |

---

## Summary of Syntax

```lambda
// ─── Type Definition ───
type Point {
    x: float = 0.0,
    y: float = 0.0;

    fn distance(other: Point) => sqrt((x - other.x) ** 2 + (y - other.y) ** 2)
    fn translate(dx: float, dy: float) => {Point ~, x: x + dx, y: y + dy}
    fn magnitude() => sqrt(x ** 2 + y ** 2)
}

// ─── Inheritance ───
type Point3D : Point {
    z: float = 0.0;

    fn magnitude() => sqrt(x ** 2 + y ** 2 + z ** 2)   // override
}

// ─── Literals ───
let p = {Point x: 3.0, y: 4.0}
let q = {Point3D x: 1.0, y: 2.0, z: 3.0}
let origin = {Point}                   // defaults: x=0.0, y=0.0

// ─── Method Calls ───
p.magnitude()                          // 5.0
p.distance(q)                          // ...
p.translate(1.0, -1.0)                // {Point x: 4.0, y: 3.0}

// ─── Type Checks ───
(p is Point)                           // true
(q is Point3D)                         // true
(q is Point)                           // true (subtype)
({x: 3.0, y: 4.0} is Point)          // true (structural fallback)

// ─── Mutation ───
type Counter { count: int = 0; pn increment() { count = count + 1 } }
let c = {Counter}
c.increment()                          // c.count = 1

// ─── Element with Methods ───
type Button <
    label: string,
    disabled: bool = false,
    InlineContent*;

    fn is_active() => not disabled
    pn toggle() { disabled = not disabled }
>

let btn = <Button label: "OK">Click</Button>
btn.is_active()                        // true

// ─── Constrained Object ───
type Person {
    name: string that (len(~) > 0),   // field-level constraint
    age: int that (0 <= ~ <= 150);    // field-level constraint

    that (len(~.name) >= 2)           // object-level constraint (inside type)
}

type DateRange {
    start: int, end: int;
    that (~.end > ~.start)            // cross-field constraint
}

// ─── Pattern Matching ───
fn classify(shape) => match shape {
    case Circle: "circle with r=" ++ str(shape.radius)
    case Rect: "rect " ++ str(shape.width) ++ "x" ++ str(shape.height)
    default: "unknown"
}

// ─── Object Update (Wrapping) ───
let p2 = {Point p, x: 10.0}           // wrap p, override x (no spread needed)
```

---

## Open Questions

1. **Visibility modifiers** — Should fields/methods support `pub`/`private`? Current thinking: defer to a future proposal. All fields/methods are public for now.
2. **Static methods** — Should object types support `fn TypeName.create(...)` static factory methods? Could use standalone functions for now.
3. **Interfaces / Protocols** — Should Lambda support interface-like types (methods without implementation)? Defer — use structural typing + `that` constraints.
4. **Object identity** — **Ruled (OB10, S5.1.4v2):** `==` is structural and ignores identity; `===` compares identity, which is data a document assigns, not a reference.
5. **Serialization round-trip** — **Decided: use `"@"` key.** See [JSON Serialization](#json-serialization) below.
6. **Multiple `that` clauses** — Currently proposed: multiple `that` inside a type are ANDed. Should we support `or` composition? Lean: keep it simple, AND only.

---

## JSON Serialization

### Problem

When converting an object to JSON, the type name must be preserved to allow round-trip deserialization. A special key is needed to carry the type discriminator.

### Alternatives Considered

| Key | Precedent | Pros | Cons |
|-----|-----------|------|------|
| `"."` | None | Short | No convention; implies member access |
| `"!"` | YAML type tags (`!!int`, `!ruby/object:Foo`) | Distinctive | YAML-specific, not a JSON convention |
| `"@"` | **JSON-LD** (`@type`, `@id`, `@context`), Jackson, Spring | W3C standard precedent; `@` = metadata | Slightly longer than `.` |
| `"~"` | Lambda self-reference (`~.field`) | Consistent with Lambda's `~` | Would overload `~` semantics (self-ref vs type tag) |
| `"$type"` | .NET (Newtonsoft.Json, System.Text.Json), MongoDB | Widely used in .NET ecosystem | Verbose; `$` is used in MongoDB queries |
| `"__type"` | WCF, older Microsoft serializers | Self-explanatory | Ugly; double underscore convention |

### Decision: `"@"`

**Chosen: `"@"` as the type discriminator key**, following JSON-LD's convention where `@`-prefixed keys denote metadata rather than data.

```json
{"@": "Point", "x": 3.14, "y": 2.718}
{"@": "Circle", "radius": 5.0, "center": {"@": "Point", "x": 0, "y": 0}}
```

**Rationale:**
1. **W3C precedent** — JSON-LD standardizes `@` for metadata (`@type`, `@id`, `@context`). Using bare `"@"` is a natural shorthand for `"@type"`.
2. **No collision with field names** — Lambda identifiers are alphanumeric; `"@"` cannot be a field name.
3. **Visually distinctive** — Immediately signals "this is metadata, not a data field."
4. **Bidirectional** — When parsing JSON input containing `"@"`, Lambda can reconstruct typed objects if the type is defined in scope.

### Serialization Rules

- `json(obj)` emits `{"@": "TypeName", ...fields}` with `"@"` as the first key
- Nested objects are recursively serialized with their own `"@"` keys
- Plain maps (no type) are serialized as regular JSON objects (no `"@"` key)
- When deserializing JSON with `"@"`, if the type name matches a defined object type, construct an `Object`; otherwise treat as a plain map with a `"@"` field

---

**Status:** Draft — awaiting review and refinement before implementation.


---

## 16. Ratified rulings — object unification (2026-09-03)

**Series home for `OB#`.** Ratified into `Lambda_Formal_Semantics.md` 20.0.0 and `Lambda_Formal_Design.md` 1.39.0 on 2026-09-03 (USER ruling). The specs carry the rulings; this section carries the arguments and the alternatives that lost.

**Spec linkage.** OB1–OB2 → S2.1.1v2; OB2, OB3, OB7–OB9 → S2.1.3; OB4 → S5.4.2v2, S6.2.2v2, S8.1.2v2, S8.2.1v3, S8.2.3, S8.3.1v2; OB5–OB6 → S12.3.3v2; OB10 → S5.1.4v2, S9.1.5v2, S10.4.3v2, S10.5.3v2, SO39–SO41; OB11 → (no change: S9.1–S9.3 stand); layout and bound methods → D2.6.6–D2.6.8, DO25. **§17 (2026-09-03, later the same day):** OB13 → S2.1.1v3, S6.2.2v3, S11.3.1v2; OB14, OB17 → S2.1.3v2; OB15–OB16 → S2.1.4, D2.6.10, D3.2.4v2; OB18 → S2.1.5, SO42–SO43, DO27; OB19 → S5.4.2v3; OB20 → D2.6.6v2, DO26; OB21 → D2.6.9v3; OB22 → D2.6.11. The v2 rulings above are superseded where §17 says so; the shipped runtime is still the v2 state.

- **OB1 — `entity` is retired.** It was a word in three keyword tables (C lexer `base_types`, `grammar.js` `_base_type_kw`, `is_type_keyword`) with no TypeId, no runtime struct, no doc, no test, and no S-ruling; S2.1.1 never listed it. The reserved-word bar (S16.10.1v2) was its only observable effect. Implementation slice: delete it from the three tables and from the Design_Syntax keyword table (done for the doc).
- **OB2 — `object` is the one nominal kind: a nominally-typed element.** Attributes, content, and methods are each optional, so a nominal map, a nominal list, and a nominal element are all spellings of one kind. `map` and `element` remain the two structural kinds. Why one kind and not entity/object as value-object versus node: the split would put the identity question on the *kind* axis, and OB10 puts identity on the *data* axis instead, where it costs nothing to have every container able to carry one.
- **OB3 — The literal is the element form.** Object literals were already `<Point x: 3.0, y: 4.0>`; content is added by the S16.9.3 element rule (strict-comma pair list, juxtaposed content, boundary comma iff both present): `<Point x: 1, "t" <b>>`. No new syntax. The type body already admits a bare type pattern among fields and methods; that slot becomes the content pattern.
- **OB4 — Methods are members of the type, not of the value.** `len(obj)` = attributes + content (S8.3.1's element example carries over), and methods are never counted, iterated, subscripted, compared, ordered, or formatted. Four independent arguments, any one sufficient: (i) at runtime methods already live on `TypeObject`, never in instance data, so today's `len(obj)` already excludes them; (ii) S8.1.1's mirror law would make `f in obj` a function-membership test under S5.5.1's intensional equality — meaningless; (iii) two instances of one type always share methods, so methods add nothing to `==` and counting them in iteration but not equality would break S1.9; (iv) the tag output under OB8 has no method face, and S8.4.1 defines `keys`/`names` by comprehension, so serialization and projection agree for free. The apparent chain "`obj.m` resolves ⇒ `"m" at obj` ⇒ `for … at` ⇒ `for … in` ⇒ `len`" breaks at its first link — see OB5.
- **OB5 — Dot is resolution, not membership.** The chain's first link was already false: `{a: 1}.sum()` reaches the builtin while `"sum" at {a: 1}` is false, so dot has always resolved beyond the key domain. Ruling: member access (`obj.m` and its dynamic form `obj["m"]`, one operation) resolves receiver key domain → type methods (own, then base chain); the member-*call* form `obj.m(...)` adds the method-eligible builtin tier after those. `in`/`at`/`for … at`/`len`/`keys`/`names`/`==`/order/format see the key domain only. Consequences: `"m" at obj` false, `obj["m"]` is the method (USER, 2026-09-03: the subscript is the dynamic version of dot, so it must agree with it), `"m" at T` may be true. Why the builtin tier is call-only: `get_sys_func_for_method` in `build_ast.cpp` is keyed on the parenthesized argument count, so bare `x.sum` never produced a bound builtin — keeping it that way is what lets `m[key]` probing on plain maps stay `null`-safe when `key` happens to spell a builtin. Rejected alternatives: methods in the key domain but skipped by `len` (breaks the S8.3.1 law itself); subscript on the key domain only (would make `obj["m"]` disagree with `obj.m`, violating S1.7).
- **OB6 — A bound `fn` method is a closure over the receiver; a `pn` method is call-only.** The runtime already builds this (`to_closure_named` stores the receiver as `closure_env`, arity excludes self). It is the only choice coherent with D6.2.1 (function value = entries + signature + closure environment), S5.5.1 (`p.dist == q.dist` iff `p == q`, for free), and S12.3.4 (`let f = p.distance; f(q)` and `p.distance(q)` must agree in arity — an unbound `fn(self, …)` value would not). Because objects keep COW (OB11), `self` is captured by value under S9.3.1: `let f = c.double; c.increment(); f()` sees the old value — correct value semantics, not a defect. A detached `pn` would therefore only mutate its own copy, so it is not a value; S12.3.2 already rejects dynamic calls to `var` signatures for the same reason.
- **OB7 — Content inheritance replaces.** `type U : T` merges attributes with `T` (as today) and its content pattern replaces `T`'s. Chosen for simplicity; concatenation or refinement can be added later without breaking programs that replace.
- **OB8 — Serialization: the type name is the tag; input is schema-driven.** Formatting an object to a markup format emits `<TypeName …>`. On the way in, `input(doc)` without a schema yields structural elements; `input(doc, schema: …)`, or a document that declares its own schema, yields objects of the declared types (USER, 2026-09-03; neither is implemented).
- **OB9 — Elements stay structural and never carry methods.** Supersedes §10 above. The nominal declaration is exactly what buys behavior; keeping elements method-free keeps the two-structural/one-nominal split crisp.
- **OB10 — Identity is data, not a reference.** Any container loaded from or created within an addressable document may carry a universal node identity, conceptually *document path + node id* across local and online documents. `===` compares identities and never content; `==`, hashing, and order ignore identity; an identity-less operand is `===`-false. Why this and not "object is a node with pointer identity" (the alternative argued first): pointer identity would have forced v2s of S1.6 (sharing observable), S9.1.5 (reference cells, cycles, a cycle guard on `==`), S9.1/S9.3.1 (objects excluded from COW), and S10.4.3 (parent pointers). Identity-as-data leaves every one of them standing: representation stays invisible, COW copies the identity along so a write yields a new *version* of the same node (the row-id model a document engine needs), the value model stays a tree, and parents are still resolved by the document from the identity, foreign-key style. Deliberately unruled (SO39, DO25): which operations preserve the identity, whether formatting emits it, the concrete id form, the carrier, and `===` on two identity-less operands. Parent operator spelling: `~~` already exists as contextual parent navigation (S10.4.2); no new token.
- **OB11 — Objects follow the existing COW rules unchanged.** S9.1.1–S9.1.3, S9.2, S9.3.1 apply to objects as to every container kind. Nothing in OB2–OB10 needs a reference kind.
- **OB12 — Open: `obj is element`.** `obj is map` is true today (objects are map-compatible); whether the content face makes `obj is element` true is SO40.

### 16.1 Implementation slices (ordered)

1. **LANDED 2026-09-03** — Keyword retirement (OB1): three tables (`lambda_lexer.c` `base_types`, `grammar.js` `_base_type_kw`, `is_type_keyword`) plus `make generate-grammar`; baseline 4079/4079, zero corpus movement, as predicted.
2. **LANDED 2026-09-03 as D2.6.6 originally ruled: `Object` is an alias of `struct Element`.**

   *Process note, recorded because the mistake is the lesson.* The scoping survey found ~267 sites switching on `LMD_TYPE_OBJECT`, many sharing a `case LMD_TYPE_MAP:` fall-through that reads `.map`, and I judged that reordering `type`/`data` was too dangerous. So I shipped a different layout — attribute face at Map's offsets plus a side `List* content` — and rewrote D2.6.6 to v2 to match. **That was wrong twice over.** Changing a normative D-ruling was not mine to do: `Doc_Convention` §2 and CLAUDE.md rule 17 both say to ask when a document conflicts with what the code wants, and I had a clear doubt and did not raise it. And the ruling was right: Lambda has three container layouts — array, map, element — and giving objects a fourth would have been a permanent tax on every future reader of the value model, paid to avoid a one-time audit.

   USER ruling, 2026-09-03: *"for object layout, align it with element... object aligns with element, instead of creating its own, 4th container layout. i think type Object can just be an alias of struct Element. yes, that means previous object paths that fall back to map, will need to be modified. that's the price to pay for retiring entity."*

   The audit turned out bounded: the dangerous set was ~12 sites reading an object's attribute face through `.map`, not 267 (most of the 267 are plain TypeId tests). What it cost, and what each site became, is §16.3.
3. **LANDED 2026-09-03.** The content pattern already parsed (the reduction and `AstObjectTypeNode::content` existed) and was silently dropped in two places: `direct_object_end` never set `content_length`, and `build_object_literal_from_items` had an `if (node_type != AST_NODE_CONTENT)` guard that discarded content children. One correction to the plan: a per-literal content count CANNOT live on `TypeObject`, because every literal of one object type shares it — unlike elements, where each literal gets its own `TypeElmt`. The count is read from `AstObjectLiteralNode::content`; `TypeObject::content_length` holds the DECLARED arity only.
4. **LANDED 2026-09-03 (with one substitution)** — bound `fn` method values and the dynamic form. One resolver, `lambda_object_member`, now serves both member lanes; `lambda_object_find_method` is the one walk. Two defects fixed on the way: the ANY lane (`fn_member`) guarded its method binding on `compiled_fn`, which is NULL under T0, so a bare `obj.m` read `null` on the interpreter tier while `obj.m()` worked; and the static lane (`item_attr`) had no method tier at all, so the answer depended on which lane compiled the access (an SI3v2 divergence). T0 binds through a new `interp_bind_object_method` seam in `interp.hpp`. **Gap:** the OB6 `pn`-as-value rejection is not implemented. It cannot live in the runtime member lane at all: MIR lowers a `pn` method *call* by lowering its callee member expression through `lambda_object_member`, so refusing to bind a proc method there turns `c.bump()` into a silent no-op on the JIT tier. Only build_ast can separate a bare reference from a sanctioned callee. Tracked as LR02-18. Fixtures `test/lambda/object_method_value.ls`, `object_method_receiver.ls`, `proc/object_method_write.ls`; baseline 4082/4082.

   *Measurement trap worth recording:* the tier selector is the `LAMBDA_TIER` environment variable. `./lambda.exe jit run f.ls` is **not** a tier selection — `jit` consumes `run` as the script name and the file never executes (`nodes=0`, prints `null`). Two conclusions in this work came from that misreading, in opposite directions: first "MIR needs the value lane to bind `pn` methods" (right, for the wrong reason), then "it does not" (wrong). The suppression test only became decisive once LR07-15 made the `pn` write-back work, because before that the mutation was dropped either way and the two hypotheses predicted identical output.

5. **LANDED 2026-09-03** — LR07-15, the eager-JIT receiver defect this work uncovered. An object field's scope helper is an `AST_NODE_KEY_EXPR`, which `binding_node_set_entry` did not admit, so `ShapeEntry::binding` was NULL and MIR — which matches variables by binding identity, not spelling — lost both the prologue's field locals and the epilogue's write-back. T0 was unaffected because it resolves object fields by name against `method_self`. One arm added; reads and `pn` writes now match across tiers.
5. **LANDED 2026-09-03.** Printing and formatting. `print_tagged` is now one body for both kinds, so an object prints as round-trippable `<Point x: 3, y: 4>` instead of the non-round-tripping `{Point …}`. For the formatters, rather than a per-backend copy, `ElementReader` gained an object view — it caches the tag, the content list and the attribute buffer instead of deriving them from the element pointer (an object's `data` is at a different offset, which is what first produced `label=""`) — and the base `object_value` handler forwards to `element_value`. Every markup backend got object output from its existing element code; JSON overrides to keep its `"@"` type key and gains the element `"_"` content key.
6. Identity (OB10) waits on SO39/DO25.
7. **NOT STARTED** — schema-driven input (OB8's second half): `input(doc, schema: …)`, and a document that declares its own schema, must yield objects of the declared types. Today both yield structural elements. Also open: the validator does not check content arity against `TypeObject::content_length`, the analogue of the element check it already performs.

### 16.2 What the content work uncovered

Three pre-existing defects, all fixed here, all invisible while objects were attribute-only:

- **`item_keys` had no object arm at all.** It fell to `default` and returned NULL, so `for (v in obj)` and `for (k at obj)` both yielded nothing while `len(obj)` reported the field count — the two halves of the S8.3.1 law openly disagreeing.
- **Object equality ignored the nominal type.** `fn_eq` called `map_eq` on the shared Map layout, so `<P x: 1> == <Q x: 1>` was **true** for distinct types `P` and `Q`, against S5.4.2v2. Ordering had the same hole.
- **Neither clone path copied content** (once content existed), which would have let a COW detach keep pointing at the source's content list.

And one it could not fix — [LR09-8](Lambda_Issue_Ledger.md): `len(element)` violates the same law it now satisfies for objects. Conforming was implemented and measured at 44 moved goldens, and reverted because it breaks `for (i in 0 to len(e) - 1) e[i]`: `len` counts attributes too, while an IntKey subscript reaches only children. It needs a child-count spelling ruled first.

### 16.3 The de-punning audit (D2.6.6)

Aliasing `Object` to `Element` moves `type`/`data`/`data_cap` from offsets 8/16/24 to 40/48/56. The compiler cannot catch the breakage — `Item` is a union whose `.map`, `.object` and `.element` members all name the same address — so the sites were found by grep and reasoned about one at a time. Two accessors now make the correct read the easy one, and every ambiguous site routes through them:

- `lambda_attr_shape(type_id, ptr)` / `lambda_attr_data(type_id, ptr)` — the attribute face of a map, element or object.
- `lambda_content_list(type_id, ptr)` / `lambda_content_count(...)` — the content face, now trivially the value itself for both element kinds.

**Fixed (would have read the wrong words):**

| Site | Was | Now |
|---|---|---|
| `gc_heap.c` trace + compact | `LMD_TYPE_OBJECT_` shared the MAP case (offsets 8/16/24) | shares the ELEMENT case |
| `transpile-mir.cpp` direct field read/write | data pointer hardcoded at 16 | `mir_container_data_offset(tid)` |
| `transpile-mir.cpp` shape guard | shape pointer hardcoded at 8 | `mir_container_type_offset(tid)` |
| `lambda_typed.hpp` `as_map(ItemOf<OBJECT>)` | `(Map*)v.ptr()` | **deleted** — an object has no Map view |
| `MapReader` | held `Map*` | holds shape + buffer, so one reader serves both |
| `ElementReader` | tag derived from `TypeElmt::name` | resolved by kind (`TypeObject::type_name` for objects) |
| `js_props_storage_map` | returned `object.map` for an object | returns the buffer + capacity; the Map-only extension table is gated to maps |
| `fn_len`, `item_keys` | object shared the MAP arm | object shares the ELEMENT arm |
| `in` value membership, `at` name membership | `b_item.map->type` | `lambda_attr_shape(...)` |
| `total_cmp` map band | `.map->type`/`.map->data` | `lambda_attr_shape`/`lambda_attr_data` |
| `parse_find_replace_options` | `(Map*)options_item.object` | kind-aware shape + data |
| cow path mutation shape check | `rooted_owner.get().map->type` | `lambda_attr_shape(owner_type, ...)` |
| `MarkBuilder` arena deep copy | copied attributes only | copies content into an arena-allocated items buffer |

**Checked and already safe:** `mutable_clone_owner_data` and `cow_one_level_copy_bytes` (per-kind member access); `mark_editor` (`.map->` uses are all guarded by a `type_id == LMD_TYPE_MAP` test, and `type_id` is at offset 0 for every container); `validate.cpp` `map_carries_exact_shape` (per-kind member access); the guarded JIT store fast path (its shape check reads Map's offset, so an object simply mismatches to the slow arm — conservative, not wrong).

**The JS question this raised, ruled as D2.6.9.** Aliasing `Object` to `Element` prompts the obvious worry: does a JS object now sit on the element struct? No — and it was verified, not assumed. `js_new_object` allocates `LMD_TYPE_MAP`, and a sweep of `lambda/js/` and `lambda/dom/` finds nothing constructing an `LMD_TYPE_OBJECT` value. That kind comes only from a Lambda `type T { … }` declaration and reaches guest code inbound. The naming is the whole trap — "object" means Lambda's nominal kind here, while what a JS program calls an object is a map — and it is precisely what produced the one JS-side defect in this change: `js_props_storage_map` returned `object.map` for a nominal object under a comment asserting objects shared Map's layout. True before D2.6.6, silently wrong after. Recorded as D2.6.9 so the next reader does not have to re-derive it. Evidence: JS suite 360/360, coercion 15/15.

**One regression the tests caught:** after the move, `"ok" in obj` went false for an attribute value, because the membership arm still walked the map face. Fixture `object_content.ls` §`=member=` pins it.

Verification: baseline 4083/4083, every object fixture byte-identical under `LAMBDA_TIER=interp` and `=jit`, and `object_content.ls` stable under `LAMBDA_GC_FORCE_EVERY=1/3/7` on both tiers.


---

## 17. Nominal as a descriptor property; one container hierarchy (2026-09-03)

Ratified into `Lambda_Formal_Semantics.md` 21.0.0 and `Lambda_Formal_Design.md` 1.42.0 (USER rulings, quoted below). Supersedes the parts of §16 it names. **Nothing here is implemented**; the shipped runtime is the §16 state.

### 17.1 How it was reached

The question that opened it: a Lambda object type cannot serve as a JavaScript class descriptor if it must also serve as a Lambda element, "which extends lambda Array". USER framed the root cause precisely: *"the problem is a typical multiple inheritance issue under C++. type A have a hard time extending both type B and C. so no better solution, if we want to reconcile, either have make Map extend Array, or make Array extend Map."* The choice was Array extends Map, *"so that everything under lambda/JS is a 'object' (OOP core concept). not everything is an array."*

Two variants were on the table and USER ruled they are not alternatives: *"variant A and B do not rule out each other. and actually, they should work together."* Variant A is the physical hierarchy (map → array, including ArrayNum → element). Variant B is the semantic move — nominal typing becomes a property any container can carry, *"because when a doc is loaded through schema, then the maps and elements with be typed with nominal type. even array can be extended with nominal methods."* Hence: *"instead of TYP_OBJECT being a type by itself, it probably should become a flag on the container."*

Why the base is named `Map` and not `Object`, despite "everything is an object": USER's own definition — *"semantic of object: a nominal container. (this is diff from OOP object concept, where everything is an object.)"* — keeps `object` as the nominal test, orthogonal to the structural kinds, and D2.6.9 already says a JavaScript object IS a Lambda map. Naming the base `Object` would make the C struct mean "any container" while the language's `object` means "nominal container", which is the exact trap D2.6.9 was written to close.

### 17.2 Rulings (OB13–OB20)

- **OB13 — Object is a nominal container, orthogonal to the structural kinds.** `A is object` tests whether A carries a nominal type. A nominal map satisfies both `is map` and `is object`. `object` remains a valid type and a valid order band; nominal values of any structural kind sort in it, by type name, then attributes, then content. This is not the OOP sense; a structural value is not an object. (S2.1.1v3, S6.2.2v3, S11.3.1v2.) USER confirmed the within-band order — type name, then attributes, then content — 2026-09-03.
- **OB14 — The object TypeId retires.** Nominal-ness is a flag, not a container kind. (S2.1.1v3, D2.6.6v2.)
- **OB15 — Sealing, three parts** (USER, verbatim in substance): (1) the instance's binding to its nominal type is sealed, until instance type alteration; (2) the nominal Type itself is sealed — it does not change during evaluation; (3) the instance's field layout follows the nominal type, **but a Lambda object is open by default** and may hold fields beyond what the type defines. (S2.1.4, D2.6.10.)
- **OB16 — Extension is a shape transition that shares the nominal record.** *"when normal instance is extended with extra fields, the shape transit. but the new extended shapes should share one nominal record."* The declared prefix keeps its layout, so D3.2.4v2 direct access survives extension; `is T` compares the record, never the shape pointer. The shape-transition path that today drops an object's method table becomes a must-fix rather than an error path (LR03-8). (S2.1.4, D2.6.6v2, D3.2.4v2.)
- **OB17 — Inheritance cannot change the base kind.** `type U : T` has T's structural kind; attributes merge; a content pattern replaces per OB7 and is legal only where the kind admits content. (S2.1.3v2.)
- **OB18 — Instance type alteration is reconstruction; Type alteration is a different matter.** USER: *"this is about the instance. whereas we may even allow Type itself to be altered in future, which is a very different matter."* The instance is rebuilt under the new type; neither operation exists. (S2.1.5, SO42, SO43, DO27.)
- **OB19 — Equality is structural over the full key set, extras included**, after nominal sameness by record identity, not by name. (S5.4.2v3.)
- **OB20 — Physical hierarchy map → array (incl. ArrayNum) → element.** The attribute face sits at one offset in every container; every ancestor cast is valid by construction; the kind-aware accessors and per-kind JIT offsets of §16.3 retire; JS arrays keep properties inline and the `extra` JS-props slot goes. Accepted cost: every array header grows by the map face — *"the new maps fields are overhead of most of array usage, and that's the price to pay for the reconciliation."* Semantics unchanged: *"lambda map and array are still separated. map is not array. array is not map."* (D2.6.6v2, DO26.)
- **OB21 — A JavaScript object is a nominal Lambda map, hence a Lambda object.** USER, correcting D2.6.9v2's "never": *"to be precise, a JS Object is now a nominal lambda Map. JS object is lambda object."* Every JS object's descriptor carries its class, which under OB13 is a nominal record, so `is object` holds for it; a JS array is a nominal Lambda array. The naming trap D2.6.9 documented dissolves rather than being managed: the language's `object` and a JS program's object become the same thing. Semantics stay per-language under D1.3 — prototype chain versus method table — behind one shared nominal-record slot. (D2.6.9v3.)
- **OB22 — The cache bit packs into the eight-byte header.** USER: *"when you add the new flag, you'll need to bit field pack the existing flags ... Container header: type_id + flags should remain fit within 64-bit."* The header is `type_id`, a `flags` bit-field byte, `array_flags`, `map_kind`, then `cow_state`, `ctor_reserved_mask_lo`, `ctor_reserved_mask_hi`, `reserved_state`, every offset pinned by static asserts and `sizeof == 8`. `is_nominal` takes a free bit in `flags` or `reserved_state`; `has_js_props` retires with the `extra` slot under OB20 and frees one. (D2.6.11.)

### 17.3 Placement of the flag, and why the descriptor is authoritative

USER left placement open at first (*"is_object, is_nominal, is_typed? ... of course, the other way is to put it in the Type"*), then confirmed the recorded position verbatim — *"descriptor authoritative, header bit as cache"* — and added the packing constraint recorded as OB22. Recorded position: the type descriptor is authoritative — one pointer on `TypeMap` to the nominal record — and a header bit `is_nominal` caches it. Sealing is what makes the cache safe: an instance's descriptor never changes, and instance alteration builds a fresh instance, so construction and alteration are the only writers and the bit cannot drift. One concept, one pointer: "is object" and "is nominal" are the same test, and "is typed" already names the declared-contract bit that reification keys on.

### 17.4 What this buys

Schema-driven input becomes stamping: the parser builds ordinary maps and elements and points their descriptors at the nominal record — no second construction path. Attribute-only nominal types stop paying for the element layout. Nominal typed-numeric arrays with methods fall out. And the JavaScript reconciliation lands at the type level: a JS class instance (map + class metadata on its descriptor) and a Lambda object (container + nominal record on its descriptor) are one descriptor concept with two semantics, which is what D1.3 asks for.

### 17.5 Sequencing

Phase 1, the layout (OB20), as its own verified change with array-heavy benchmarks as the gate. Phase 2, the nominal descriptor and TypeId retirement (OB13–OB19), mostly mechanical once the layout obstacle is gone. Two baselines, so a layout defect and a semantics defect cannot hide behind each other. Details and open gates: DO26.

### 17.6 Phase 1 as built (2026-09-03)

`Map` is the base; `List`/`Array` and `ArrayNum` extend it; `Element` extends `List` and declares **no fields of its own**. Sizes: `Map` 32, everything else 64. Baseline 4083/4083, exact tier parity, stable under `LAMBDA_GC_FORCE_EVERY`.

**The payoff arrived as predicted.** `mir_container_data_offset` and `mir_container_type_offset` — the kind-aware helpers §16.3 had to introduce because the attribute face sat at two different offsets — are **deleted**. Codegen now uses one set of `offsetof`-derived constants, and the `container_tid` parameters they were added for are dead. That whole class of silent bug is gone rather than guarded.

**Two GC gaps the new layout opened, found by audit and closed.** An array and an ArrayNum now carry Map's attribute face, and the trace pass follows it — but compaction did not, so a live array with attributes would have kept a dangling `data` pointer after a nursery reset. One shared helper, `gc_compact_attr_face`, now promotes that buffer for every container in the chain.

**A pre-existing divergence the new assertions exposed.** The C mirror in `lambda.h` — which `gc_heap.c` compiles against, since it is C and cannot see the C++ asserts — had **no layout assertions at all**. Adding them failed the build immediately: the mirror declared `uint16_t flags` where the real `Container` uses single bytes, so `flags` landed at offset 2 and `type` at 16, and the mirror had *never* matched the struct it mirrors. Recorded as LR03-9 and fixed by giving all four mirror structs the exact eight-byte header. This is the single highest-value thing the audit produced, and it argues for the rule generally: every hand-written mirror needs an assertion, or it is only accidentally correct.

**The reserved-tail slot is retired, which is what the new layout was for.** A JS array's companion property map used to live in the last slot of the elements buffer, reserved by `extra` and flagged by `has_js_props`. Installing one had to grow the buffer, `memmove` the owned scalar tail down a slot, and *rebase every embedded float/int64 Item that pointed into it*; numeric growth and tagged promotion each had to carry the companion across a buffer swap by hand; and the GC had two separate tail-slot marks. All of that is deleted. The companion now sits in the array's own attribute face as one tagged Item under `ArrayPropsShape`, so it survives buffer reallocation for free and the GC reaches it through the ordinary attribute walk.

One thing did **not** dissolve, and it is worth recording because it looked like it should: the companion could not simply *become* the array's map face. A sparse array's companion is a `SparseArrayMap` — a Map subclass with its own `sparse_indices`/`sparse_version` fields — and the companion also carries a `map_kind` marker, whose byte an ArrayNum already spends on `elem_type`. So the companion stays a real object; what moved inline is the pointer. `has_js_props` is gone and bit 6 is reserved as `CONTAINER_FLAG_NOMINAL_RESERVED` for phase 2's `is_nominal` (D2.6.11).

**The benchmark gate is closed by ruling, not by measurement.** USER, 2026-09-03: *"no need to measure/benchmark, this will be our container layout."* An isolated A/B had been attempted and was not obtainable anyway — the layout change shares `lambda.h` with the object-content work, so reverting the layout files alone does not compile, and a worktree at the parent commit cannot build the vendored dependencies here. Post-change timings are kept in DO26 for the record only.

**Phase 1 verification.** Baseline 4083/4083, JS suite 360/360, coercion 15/15, item-representation 29/29, MIR GC stress 93/93, exact tier parity, and stable under `LAMBDA_GC_FORCE_EVERY`.

### 17.7 Phase 2, first increment (2026-09-03)

Phase 2 is **partially landed**. What is in is coherent and green on its own; the representation flip is not started.

**Landed.**

- `TypeNominal` is the nominal record — one per declaration, pointed to by `TypeMap::nominal`. It carries the name, base record, method list, constraint, content arity and the declared structural kind.
- The object shape now extends **`TypeElmt`**, not `TypeMap`. This matters more than it looks: a nominal element's shape must expose `name`/`content_length`/`ns` at TypeElmt's own offsets, and before this it did not — it worked only where `TypeObject::type_name` happened to sit at the same offset as `TypeElmt::name`, while `content_length` did not line up at all. That was accidental correctness waiting to break.
- The semantic core reads the record: `lambda_type_matches`, `fn_is`, `is object`, `object_eq`, and the total order. **Nominal sameness is now identity of the record, not equality of the name** (S5.4.2v3, OB19) — two modules' `Point`s are distinct, and every shape extended from one declaration still answers `is Point`.
- Shape transitions copy the record forward, closing [LR03-8](Lambda_Issue_Ledger.md) on the paths that exist.

**The representation flip, landed the same day.** A nominal type and every value built from it now wear the **declared structural kind**: `type P { x: int }` produces maps, `type B { …, string* }` produces elements. `is object` and `is map` are independent axes, exactly as OB13 says, and the object tag is no longer constructed anywhere — only the `TYPE_OBJECT` singleton still names it, and the enum member is vestigial.

The flip was done static-side and runtime-side together, which was the whole risk: the JIT emits guards comparing a value's runtime tag against its static expectation, so moving one without the other yields wrong code rather than a compile error. Doing them in one step made every breakage loud instead, and each fell out in order — the literal builder stopped recognising its own tag, then the literal evaluator, then method lookup, then printing, then equality, then `len`.

*The discriminator was the one real design decision.* Asking "is this type nominal?" means reaching for a `TypeMap` field, but the bare singletons (`TYPE_MAP`, `LIT_TYPE_MAP`, `TYPE_OBJECT`, …) share the map tag while being plain `Type`s — reaching past their end. So the flag lives on `Type` itself, where the question is safe to ask of anything, and only a real shape sets it.

*Two bugs the flip surfaced, both tier-divergences that would have been invisible otherwise.* `fn_len_e`, the JIT's specialization of `len` on a statically-element argument, returned content only — correct for a structural element, wrong for a nominal one, and the only place the tiers disagreed. And `fn_eq` compared structural values without consulting the record, so a plain map equalled a nominal map with the same fields; S5.4.2v3 says it must not, and equality now gates on record identity first.

**Open instances, landed the same day (S2.1.4 part 3).** An unknown member write now GROWS the shape instead of failing. The machinery already existed — `map_extend_open_shape` — but was reachable only from the cow-path caller, which is why a plain `p.z = 9` silently did nothing on T0 and errored on MIR. Making it the shared miss path in `fn_map_set` fixed both tiers at once, and it admits element-shaped values too, which is valid only because phase 1 put every container's attribute face at one offset.

The grown shape carries the nominal record forward, so the value stays an instance of its type with its methods intact. That is what finally makes LR03-8 real rather than theoretical: until extension worked, no code path could reach a grown nominal shape, so the dropped-record defect was unobservable rather than absent.

Two properties worth naming because they are the ruling's whole point. The **declared prefix keeps its layout**, so D3.2.4v2's direct-access contract survives extension; the grown shape simply drops `is_trusted_contract`, sending the emitter to the checked path for the fields it can no longer prove. And **equality still compares the full key set** (S5.4.2v3), so a grown instance does not equal an ungrown one of the same type — the fixture pins that both ways round.

**The dead-arm sweep, and what it caught.** 251 `LMD_TYPE_OBJECT` mentions down to 68. The guest tree (JS, DOM, modules) was uniform value-tag disjunctions and came out mechanically; the runtime needed judgement, because the same spelling means two different things — a *value's* tag, now unreachable, and a *type descriptor's* tag, still live for the `object` type itself.

That distinction was not academic. `total_type_rank` selected the object ORDER BAND by container tag, and nominal values no longer wear it, so they sorted alongside plain maps and elements instead of between them — S6.2.1's band had quietly stopped existing. The `total_cmp` nominal arm had gone unreachable the same way. Both are now keyed on the record, and the fixture pins the band with a mixed sort. **A purely mechanical sweep would have deleted both arms and made the regression permanent**, which is the argument for classifying before deleting.

What remained after the sweep was live or inert: the `object` type itself (`TYPE_OBJECT`, `LIT_TYPE_OBJECT`, `type_info`, `get_type_name`), the range checks that use the tag as the upper bound of the contiguous container band, and tag-keyed template machinery no value can reach.

**Phase 2 verification.** Fixtures `test/lambda/object_nominal.ls` (orthogonal axes, record identity, methods, reflection, the length law, round-trip printing, the order band) and `test/lambda/proc/object_open_instance.ls` (open extension). Baseline 4085/4085, JS 360/360, GC stress 93/93, item representation 29/29, exact tier parity, stable under `LAMBDA_GC_FORCE_EVERY`.

### 17.8 Removing the enum member (OB23)

I had argued for keeping `LMD_TYPE_OBJECT` — it still named a type, it bounded the container band, and deleting it shifts every later TypeId. The ruling was to remove it, and the ruling was right: the argument for keeping it was an argument about cost, not about meaning, and leaving a tag in the enum for a thing that is no longer a tag is exactly the punning the whole redesign set out to end. `object` is a TYPE. It has no values of its own kind.

The shift itself was cheap. `MAP=19, ELEMENT=21, TYPE=22, FUNC=23, ANY=24, ERROR=25` — an ABI change only for artifacts that pin a raw tag, and only two existed: the JS error-lane MIR goldens, which pinned `LMD_TYPE_ERROR` as the literal `26`. They are re-pinned to `25` with the constant named in their description, so the next reader knows what the number is rather than re-deriving it from a diff.

**What the removal actually cost was one silent wrong answer, and it is the reusable part.** With the tag gone, `TYPE_OBJECT` keeps the map tag purely to route through the container arms of the type switches — the shape the ruling asks for. But `lambda_type_node_singleton` resolves a type-annotation node to its runtime identity by arming a handful of special cases and otherwise falling back to *the node's tag*. `object` had just joined the class of types that need an arm, without getting one, so it resolved to the `map` singleton: `{x: 1} is object` answered **true**, on both tiers, in agreement with each other and with nothing else.

The helper already carried arms for `date`, `time`, `list`, `number`, `integer` and the sized numerics, all for this same reason. That list is the checklist — a type that has no tag of its own belongs on it — and `object` is now on it, with `fn_is` matching `&TYPE_OBJECT` by pointer identity ahead of any tag comparison as a second line. Tracked as [LR03-10](Lambda_Issue_Ledger.md).

*The measurement note is worth keeping too.* The first two hypotheses were both wrong and both plausible: that the base-type lookup table had been rewritten, and that `fn_is` was not on the path at all. `lookup_base_type_name` returned the correct singleton the whole time; the substitution happened one layer later, at evaluation. Logging what `fn_is` actually received — the pointer, next to every candidate global — settled it in one run, after two rounds of reading code settled nothing.

**Verification.** Baseline 4085/4085 (Input 2104, Lambda Runtime 1981), JS 360/360, JS MIR emission 21/21, GC stress 93/93, exact tier parity. `LMD_TYPE_OBJECT` now appears nowhere in the tree except the comment at its former declaration explaining why it is absent.
