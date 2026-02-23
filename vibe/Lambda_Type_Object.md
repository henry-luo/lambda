# Lambda Object Type — Design Proposal

**Date:** February 23, 2026  
**Status:** Draft  
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
7. [Type Checking — Nominal with Structural Fallback](#type-checking--nominal-with-structural-fallback)
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
| Type checking | Nominal (primary), structural fallback for plain maps |
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

## Type Checking — Nominal with Structural Fallback

### Nominal Checking (Objects)

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

### Structural Fallback (Plain Maps)

Plain maps (without a type name) can still match object types structurally:

```lambda
let m = {x: 1.0, y: 2.0}     // plain map, no type name

(m is Point)    // true  — structural match (all fields present and match types)
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

| Task | Details |
|------|---------|
| Add `LMD_TYPE_OBJECT` | New enum value in `lambda.h`, update `get_type_name()` |
| Define `Object` struct | In `lambda-data.hpp`, extends `Container` with same layout as `Map` |
| Define `TypeObject` | Extends `TypeMap` with `type_name`, `base`, `methods` |
| Define `TypeMethod` | Method entry struct (name, function ptr, is_proc) |
| Update `get_type_id()` | Handle `LMD_TYPE_OBJECT` in all type-dispatch switches |
| Update `is_container()` | Include `LMD_TYPE_OBJECT` |
| Object allocation | `object()` constructor in `lambda-mem.cpp` |

### Phase 2: Grammar & Parser

| Task | Details |
|------|---------|
| Extend `object_type` rule | Add inheritance (`:`) and method declarations |
| Add `object_literal` rule | `{TypeName field: value, ...}` syntax |
| Extend `entity_type` rule | Add method declarations after `;` |
| Regenerate parser | `make generate-grammar` |
| Update `ts-enum.h` | Map new grammar symbols to C enums |

### Phase 3: AST Builder

| Task | Details |
|------|---------|
| `build_object_type()` | Build `TypeObject` from CST, handle fields + methods + defaults |
| `build_object_literal()` | Parse `{TypeName ...}` and emit object construction AST |
| Handle inheritance | Merge parent fields/methods into child `TypeObject` |
| Implicit `this` binding | Resolve bare field names in method bodies to object field access |
| `~` self-reference | Wire `~` to object context inside method bodies |

### Phase 4: Transpiler (C + MIR JIT)

| Task | Details |
|------|---------|
| Object construction codegen | Emit code for `Object*` allocation and field packing |
| Method call codegen | Emit dispatch: `obj.method(args)` → lookup + call |
| `pn` mutation codegen | Emit field write-back for mutable methods |
| Default value codegen | Emit default value evaluation for omitted fields |
| Inherit method dispatch | Walk parent chain for inherited methods |

### Phase 5: Runtime Operations

| Task | Details |
|------|---------|
| `object_get()` / `object_set()` | Field access (reuse map layout) |
| `fn_is()` extension | Nominal type check + parent chain + structural fallback |
| `print()` / `str()` / `json()` | Object serialization with type name |
| Pattern matching | `case TypeName:` in `match` expressions |
| Object wrapping | `{TypeName source, overrides...}` (no spread needed) |
| Object-level constraints | `that (...)` inside type body, compiled + checked at `is` |
| Ref counting | Object deallocation (fields + method table) |

### Phase 6: Tests

| Task | Details |
|------|---------|
| `test/lambda/object_basic.ls` | Object creation, field access, method calls |
| `test/lambda/object_inherit.ls` | Inheritance, method override, `is` checks |
| `test/lambda/object_mutation.ls` | `pn` method mutation |
| `test/lambda/object_defaults.ls` | Default field values |
| `test/lambda/object_pattern.ls` | Pattern matching with objects |
| `test/lambda/object_constraint.ls` | Field-level and object-level constraints |
| `test/lambda/object_element.ls` | Element type with methods |

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
4. **Object identity** — Should two objects with the same type and field values be `==`? Proposed: yes, structural equality (same as maps). Use `===` for identity if needed later.
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
