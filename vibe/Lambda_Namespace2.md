# Lambda Namespace Design v2 — Simplified

## Motivation

The [original namespace proposal](Lambda_Namespace.md) introduced a dedicated `namespace` keyword, a new `Name { String*, Target* }` struct that replaced `StrView` in `ShapeEntry` and `TypeElmt`, and namespace-qualified attribute keys (`ns.attr`). This raised many open questions (Q1–Q9) about data model changes, dot-chain ambiguity, symbol comparison semantics, and scope rules.

This v2 design simplifies drastically by applying two key insights:

1. **`import` is already a namespace mechanism.** Lambda already has `import alias: path` — adding a second namespace mechanism (`namespace` keyword) is redundant.
2. **Attribute/field keys should remain simple strings.** Instead of attaching a namespace to every attribute key, namespaced attributes are stored as a **sub-map** under the namespace prefix key. This keeps `ShapeEntry.name` as a plain `StrView*` — zero data model change for fields.

---

## Design Overview

### 1. Namespace Declaration = Import Statement

No new keyword. Reuse `import` with three forms:

```lambda
// (a) Bare URI — no Lambda schema, just a namespace identifier for XML round-tripping
import svg: 'http://www.w3.org/2000/svg';
import xlink: 'http://www.w3.org/1999/xlink';

// (b) Lambda path — imports a Lambda schema document defining element/type declarations
import html: .schemas.html;
import ui: .components.ui;

// (c) Remote Lambda schema — fetched over HTTP(S) using Lambda's existing path syntax
import svg: http.'https://example.com/schemas/svg.ls';
```

**Semantics:**

| Form | Target | Behavior |
|---|---|---|
| `import ns: 'URI'` | Symbol (URL string) | Bare namespace binding. No schema loaded. Used for XML fidelity. |
| `import ns: path` | Lambda path (relative/absolute) | Loads a `.ls` schema — exports types, elements, functions as today. |
| `import ns: http.'URL'` | HTTP path | Loads a remote `.ls` schema. |

The alias `ns` becomes a **namespace prefix** and also a **module alias** (when backed by a schema). This unifies both concepts:

- As a namespace prefix: qualifies element tags and attribute groups.
- As a module alias: gives access to exported types and functions (when schema-backed).

**Bare URI imports** (`import ns: 'URI'`) do not load any module — they simply register a namespace prefix→URI mapping, used for XML round-trip fidelity and namespaced element construction.

**Grammar change:** The existing `import_module` rule already supports `alias: module` syntax. Extend it to also accept a symbol (quoted string) as the module target for bare URIs. The `namespace_decl` rule can be removed.

```
import_module: $ => choice(
    field('module', choice($.absolute_name, $.relative_name, $.symbol)),
    seq(field('alias', $.identifier), ':',
      field('module', choice($.absolute_name, $.relative_name, $.symbol)))
),
```

This already accepts `import svg: 'http://...'` via the `$.symbol` alternative. **No grammar change needed for the import statement itself.**

---

### 2. Namespaced Element and Object Tags

The `ns.name` form qualifies element tags and object type names:

```lambda
import svg: 'http://www.w3.org/2000/svg';

// Namespaced element literal
<svg.rect width: 100, height: 50>

// Namespaced object literal (map with namespace-qualified type)
{svg.circle cx: 50, cy: 50, r: 25}
```

The tag `svg.rect` is stored in `TypeElmt.name` as the full dotted string `"svg.rect"` — same as today's `ns_identifier` handling in `build_ast.cpp`. The `TypeElmt.ns` field points to the resolved `Target*` from the import.

**No new grammar rule needed** — `ns_identifier` (already defined as `identifier '.' identifier`) handles this.

---

### 3. Namespaced Attributes = Sub-map Grouping

This is the core simplification. Instead of making each attribute key carry a namespace, **namespaced attributes are nested under the namespace prefix as a sub-map**.

#### Syntax and Equivalence

The dotted attribute form is **syntactic sugar**, desugared at AST build time:

```lambda
// SHORTHAND: ns.attr as attribute key
<svg.a xlink.href: "https://example.com", xlink.title: "Example">

// EQUIVALENT NORMALIZED FORM: ns as key, map as value
<svg.a xlink: {href: "https://example.com", title: "Example"}>
```

**Rule:** `<x ns.attr: val>` is desugared to `<x ns: {attr: val}>`.

This generalizes to deeper nesting:

```lambda
// Multi-level dotted key
<x a.b.c.attr: val>

// Desugars to nested maps
<x a: {b: {c: {attr: val}}}>
```

Multiple attributes with the same namespace prefix are grouped into one sub-map:

```lambda
// Multiple dotted attrs with same prefix
<svg.a xlink.href: "url", xlink.title: "t", class: "link">

// Desugars to:
<svg.a xlink: {href: "url", title: "t"}, class: "link">
```

#### AST Build-Time Desugaring

The desugaring happens in `build_ast.cpp` during `build_elmt()` and `build_map()`. When the parser produces an `attr` node whose `attr_name` is an `ns_identifier`:

1. Split the dotted key into segments: `a.b.c.attr` → `["a", "b", "c", "attr"]`
2. Create a chain of nested map literals: `a: {b: {c: {attr: val}}}`
3. Merge attributes sharing the same top-level prefix into one sub-map

The **internal AST always stores the normalized (nested) form**. The dotted attribute syntax is purely surface-level sugar.

#### Why This Is Better

| Aspect | Original (v1) | Simplified (v2) |
|---|---|---|
| `ShapeEntry.name` type | `Name { String*, Target* }` — new struct | `StrView*` — **no change** |
| Attribute lookup | Namespace-aware comparison | Normal string/symbol lookup |
| Querying `xlink` attrs | Filter all attrs by namespace | `el.xlink` → returns the sub-map |
| Dot-chain ambiguity (Q3) | AST rewriting to detect ns mid-chain | **Eliminated** — `el.xlink.href` is normal nested access |
| Comparison (Q9) | Semantic URI comparison on every name | Only on element tags |
| Memory overhead | `Target*` on every `ShapeEntry` | `Target*` only on `TypeElmt` |

---

### 4. Accessing Namespaced Data

```lambda
import svg: 'http://www.w3.org/2000/svg';
import xlink: 'http://www.w3.org/1999/xlink';

let el = <svg.a xlink.href: "https://example.com", xlink.title: "Ex", class: "link">

// Access regular attribute
el.class              // => "link"

// Access namespaced attribute — just nested member access
el.xlink.href         // => "https://example.com"
el.xlink.title        // => "Ex"

// Get all xlink attributes as a map
el.xlink              // => {href: "https://example.com", title: "Ex"}

// Element tag
el.tag                // => svg.a (future: built-in property)
```

No new access syntax needed. Standard dot notation handles everything.

---

### 5. Dynamic Namespaced Symbol Construction

For programmatic construction when the namespace is not known at compile time:

```lambda
// Construct a namespaced symbol dynamically
let s = symbol("rect", 'http://www.w3.org/2000/svg')

// Compare with static namespaced tag
s == svg.rect         // => true (semantic comparison by URI)
```

The `symbol(name, ns_url)` two-argument form returns a `Symbol` with an attached `Target*`. This is only needed for **element tags** — attribute keys are plain strings accessed through sub-maps.

---

### 6. XML Round-Trip

#### Input (XML → Lambda)

When reading XML with `input("doc.xml", 'xml)`:

```xml
<svg:rect xmlns:svg="http://www.w3.org/2000/svg"
          xlink:href="url" xmlns:xlink="http://www.w3.org/1999/xlink"/>
```

Produces:

```lambda
<svg.rect xlink: {href: "url"}>
```

The XML parser:
1. Maps `xmlns:` prefixes to namespace URIs
2. Produces `ns.name` element tags with `TypeElmt.ns` set
3. Groups namespace-prefixed attributes into sub-maps
4. Drops `xmlns:` declarations (they become implicit in the Lambda namespace model)

#### Output (Lambda → XML)

When formatting to XML, the serializer:
1. Collects all namespace prefixes used in the document
2. Emits `xmlns:prefix="uri"` declarations on the root (or first-use element)
3. Flattens sub-maps back to `prefix:attr="val"` attributes

```lambda
import svg: 'http://www.w3.org/2000/svg';
import xlink: 'http://www.w3.org/1999/xlink';

let doc = <svg.rect xlink: {href: "url"}>
format(doc, 'xml)
```

Outputs:

```xml
<svg:rect xmlns:svg="http://www.w3.org/2000/svg"
          xmlns:xlink="http://www.w3.org/1999/xlink"
          xlink:href="url"/>
```

---

## Internal C Representation Changes

### What Changes

| Location | Before | After | Notes |
|---|---|---|---|
| `TypeElmt.name` | `StrView` | `StrView` (unchanged) | Stores full `"svg.rect"` string |
| `TypeElmt.ns` | `Target*` (added in v1) | `Target*` | Resolved from import declaration |
| `ShapeEntry.name` | `StrView*` | `StrView*` (unchanged) | **No change** — plain string keys |
| `AstImportNode` | Loads `.ls` modules | Also handles bare URI | Add `is_namespace` flag for bare URI imports |
| `AstIdentNode.name` | `String*` | `String*` (unchanged) | Var/fn/type names unchanged |
| `NameEntry.name` | `String*` | `String*` (unchanged) | Scope bindings unchanged |
| `symbol()` return | `String` tagged `LMD_TYPE_SYMBOL` | `Symbol { String*, Target* }` | Only for element tags, not attributes |

### Namespace Registry

Each script maintains a namespace registry mapping prefix strings to resolved `Target*`:

```c
// In Transpiler or Script struct
typedef struct NamespaceEntry {
    String* prefix;     // "svg", "xlink", etc.
    Target* target;     // resolved URI or schema path
    bool is_bare;       // true for bare URI imports (no schema)
} NamespaceEntry;
```

During AST building, when an `ns_identifier` is encountered (element tag or attribute key), the prefix is looked up in this registry to:
- Resolve `TypeElmt.ns` for element tags
- Verify the prefix is a declared namespace (compile-time error if not)

---

## Grammar Changes Summary

| Area | Change |
|---|---|
| **`namespace_decl`** | **Removed.** No longer needed. |
| **`import_module`** | Already supports `alias: symbol` — no change needed. |
| **Element tag** | `ns_identifier` already supports `ns.name` — no change needed. |
| **Attribute name** | `attr_name` already allows `ns_identifier` — no change, but semantics change to desugar. |
| **`build_ast.cpp`** | Desugar `ns.attr: val` → `ns: {attr: val}` in `build_elmt()` and `build_map()`. |
| **`symbol()` function** | Add 2-argument form: `symbol(name, namespace_url)` for element tags. |
| **Name collision** | Compile-time error if any variable/function/type name shadows a namespace prefix. |

---

## Comparison: v1 vs v2

| Aspect | v1 (Original) | v2 (Simplified) |
|---|---|---|
| **Declaration** | New `namespace` keyword + `at` syntax | Reuse `import` — no new keyword |
| **`ShapeEntry` change** | `StrView*` → `Name*` on every attribute | **No change** |
| **`TypeElmt` change** | `StrView` → `Name` | `ns` field only (additive) |
| **Dot-chain ambiguity** | AST rewriting to detect ns mid-chain | **Eliminated** |
| **Attribute model** | Flat keys with namespace pointers | Sub-maps (nested normal maps) |
| **Attribute querying** | Namespace-aware comparison | Plain `el.ns.attr` member access |
| **Symbol comparison** | Semantic URI comparison on every symbol | Only on element tags |
| **Memory overhead** | `Target*` on every `ShapeEntry` | `Target*` only on `TypeElmt` |
| **Grammar changes** | New keyword, new statement, new comparison | Remove `namespace_decl`, rest reused |
| **Data model complexity** | New `Name` struct pervasive | Minimal — sub-maps use existing types |

---

## Resolved Design Questions

The original proposal had 9 open questions (Q1–Q9). Here's how v2 resolves or eliminates them:

| # | Original Question | v2 Resolution |
|---|---|---|
| Q1 | Where does namespace attach in data model? | **Only on `TypeElmt.ns`.** `ShapeEntry` is unchanged. Attributes use sub-maps. |
| Q2 | Built-in `.tag`, `.namespace`, `.local`? | **Deferred** (same as v1). |
| Q3 | Dot-chain ambiguity in member access? | **Eliminated.** `el.xlink.href` is normal nested access — no AST rewriting for member chains. |
| Q4 | `namespace` grammar details? | **Eliminated.** No `namespace` keyword. Reuse `import`. |
| Q5 | Namespaced symbol representation? | **Simplified.** Only element tags carry `Target*`. Attribute keys are plain strings. |
| Q6 | Namespace scope and inheritance? | **Follows `import` semantics.** File-local, no re-export. Already defined. |
| Q7 | `symbol()` two-arg return type? | Same as v1: `Symbol { String*, Target* }`. Only used for element tags. |
| Q8 | XML round-trip mapping? | **Cleaner.** Sub-map ↔ XML prefixed attributes is a natural mapping. |
| Q9 | Comparison by prefix or URI? | **Semantic (by URI)**, but only for element tags. Attribute comparison is plain string — no namespace involved. |

---

## Examples

### SVG Inside HTML

```lambda
import svg: 'http://www.w3.org/2000/svg';
import xlink: 'http://www.w3.org/1999/xlink';

let doc = <html;
  <body;
    <svg.svg width: 200, height: 200;
      <svg.circle cx: 100, cy: 100, r: 50, fill: "blue">
      <svg.a xlink: {href: "https://example.com"};
        <svg.text x: 10, y: 50; "Click me">
      >
    >
  >
>
```

### Schema-Backed Namespace

```lambda
import ui: .schemas.ui_components;

// ui exports types — can construct validated elements
let btn = <ui.button variant: "primary", size: "lg"; "Submit">

// ui exports functions
let grid = ui.layout(columns: 3, gap: 16)
```

### Multi-Level Dotted Attributes

```lambda
import data: .schemas.data;

// Deep dotted keys desugar to nested maps
<data.record a.b.c: 42, a.b.d: "hello", x: true>

// Equivalent to:
<data.record a: {b: {c: 42, d: "hello"}}, x: true>
```

### XML Processing

```lambda
import svg: 'http://www.w3.org/2000/svg';
import xlink: 'http://www.w3.org/1999/xlink';

let doc = input("drawing.svg", 'xml)

// Filter elements by namespaced tag
let rects = doc | ~ where ~.tag == svg.rect

// Access namespaced attributes via sub-map
let urls = doc | ~ where ~.xlink != null | ~.xlink.href
```
