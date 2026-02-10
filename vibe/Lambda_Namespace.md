# Lambda Markup Namespace Design

## Background

Namespaces solve **name collisions** when mixing markup vocabularies (e.g., SVG inside HTML, MathML inside XHTML). Lambda's document processing pipeline handles XML/HTML/SVG — round-tripping namespaced XML faithfully requires namespace support.

### Prior Art

| Language/System | Namespace Approach |
|---|---|
| **XML/XSLT** | `xmlns:prefix="uri"` declarations, `prefix:name` qualified names |
| **E4X (ECMAScript for XML)** | `namespace ns = "uri"; x.ns::element` — `::` operator |
| **Scala XML literals** | Inherited XML namespaces in embedded XML |
| **C# LINQ to XML** | `XNamespace ns = "uri"; ns + "element"` — `+` operator |
| **Clojure (data.xml)** | Keywords with namespace: `:ns/tag` |
| **JSX/React** | No namespace support (convention-based: `svg`, `math` tags) |

### Design Goal

Support namespaces **minimally** — enough to round-trip XML faithfully and let users define prefixes, but don't make them pervasive. Think HTML5's approach, not XSLT's.

---

## Design: Dot-prefix Namespaces

Lambda uses `.` (dot) as the namespace separator, consistent with member access syntax. Namespace prefixes are globally declared identifiers that are **reserved** — no variable, function, type, or field name may collide with a declared namespace prefix.

### 1. Namespace Declaration

Namespace declarations are **global** statements. They bind a short prefix to a namespace URL (string) or path.

```lambda
// Declare namespace with URL string
namespace svg at 'http://www.w3.org/2000/svg';
namespace xlink at 'http://www.w3.org/1999/xlink';

// Multiple declarations in one statement
namespace svg at 'http://www.w3.org/2000/svg',
          xlink at 'http://www.w3.org/1999/xlink';

// Declare namespace with Lambda path (for local schemas)
namespace myns at .schemas.my_namespace;
```

**Grammar:** `namespace` identifier `at` (symbol | path) `,` ...

### 2. Namespace Usage in Elements

The `ns.name` form is used for element tags, attribute names, and symbol values.

```lambda
namespace svg at 'http://www.w3.org/2000/svg';
namespace xlink at 'http://www.w3.org/1999/xlink';

// Namespaced element tag
<svg.rect width: 100, height: 50>

// Namespaced attribute name
<svg.a xlink.href: "https://example.com"; "Click">

// Namespaced symbol value in attribute
<item type: svg.circle>

// Unqualified names work as today — zero breaking changes
<div class: "container"; "hello">
```

### 3. Namespace in Path and Member Access

Since namespace prefixes are globally reserved names, `ns.name` in path/member expressions is unambiguous:

```lambda
// Accessing namespaced attributes on an element
let el = <svg.rect width: 100, height: 50>
el.width          // => 100 (unqualified attribute)

// Nested path with namespace segment
a.b.svg.c         // svg is a namespace prefix, so svg.c is a namespaced name
```

**Constraint:** No variable, function, type name, or type field name may be the same as a globally declared namespace prefix. If there is a collision, it is reported as a **compile-time error** — the user must choose a different namespace prefix.

```lambda
namespace svg at 'http://www.w3.org/2000/svg';

let svg = 123     // ERROR: 'svg' conflicts with namespace prefix
fn svg() => ...   // ERROR: 'svg' conflicts with namespace prefix
type svg = ...    // ERROR: 'svg' conflicts with namespace prefix
```

### 4. Dynamic Namespaced Symbol Construction

For dynamic access to namespaced attributes, use the two-argument form of `symbol()`:

```lambda
// Construct a namespaced symbol dynamically
let s = symbol("href", 'http://www.w3.org/1999/xlink')

// Use it to access namespaced attributes on elements
let url = elmt[s]

// Compare with static namespaced symbol
s == xlink.href   // => true
```

This enables programmatic construction of namespaced symbols when the name or namespace is not known at compile time.

### 5. Querying Namespaced Elements

```lambda
namespace svg at 'http://www.w3.org/2000/svg';

let doc = input("drawing.svg", 'xml);

// Filter by namespaced tag
let rects = doc | ~ where ~.tag == svg.rect

// Access namespace info
let el = <svg.rect width: 100>
el.tag          // => svg.rect (qualified name as symbol)
```

---

## Internal C Representation

### Name/Symbol Struct

`Name` and `Symbol` are **no longer aliases of `String`**. They become a new struct that pairs a local name with an optional namespace target:

```c
typedef struct Name {
    String* name;        // local name (e.g., "rect", "href") — pooled
    Target* ns;          // namespace target (URL/path), NULL if no namespace
} Name;

typedef Name Symbol;     // Symbol uses the same struct as Name
```

- **`name`**: the local part of the name (e.g., `"rect"` in `svg.rect`), stored as a pooled `String*`
- **`ns`**: pointer to the resolved namespace `Target` (URL or path). `NULL` for unqualified names — zero overhead for the common case.

This reuses the existing `Target` struct (`lambda.h:412`) which already handles both URL and path representations:

```c
typedef struct Target {
    TargetScheme scheme;
    TargetType type;
    const char* original;
    union {
        Url* url;
        Path* path;
    };
} Target;
```

### What Changes

| Location | Before | After | Notes |
|---|---|---|---|
| `TypeElmt.name` | `StrView` | `Name` | Element tag name gains namespace |
| `ShapeEntry.name` | `StrView*` | `Name*` | Attribute/field names gain namespace |
| `AstIdentNode.name` | `String*` | `String*` (unchanged) | Var/fn/type names do not support namespace |
| `NameEntry.name` | `String*` | `String*` (unchanged) | Scope bindings do not support namespace |
| `symbol()` return | `String` tagged as `LMD_TYPE_SYMBOL` | `Name` (new struct) | 2-arg form sets `ns` field |

Unqualified names have `ns == NULL`, preserving backward compatibility.

---

## Grammar Changes Summary

| Area | Change |
|---|---|
| **New statement** | `namespace` identifier `at` (symbol \| path) (`,` ...)? `;` |
| **Element tag** | Allow `identifier.identifier` (namespace.local) in addition to bare identifier/symbol |
| **Attribute name** | Allow `identifier.identifier` for namespaced attributes |
| **Member access** | Namespace-qualified segments in dot paths: `a.b.ns.c` |
| **`symbol()` function** | Add 2-argument form: `symbol(name, namespace_url)` |
| **Name collision** | Compile-time error if any name binding shadows a namespace prefix |

---

## Open Questions

### Q1. Data Model: Where Does Namespace Attach?

Currently the relevant structs store names as flat strings:

| Location            | Current type | Used for              |
| ------------------- | ------------ | --------------------- |
| `TypeElmt.name`     | `StrView`    | Element tag name      |
| `ShapeEntry.name`   | `StrView*`   | Attribute/field names |
| `AstIdentNode.name` | `String*`    | AST identifier names  |
| `NameEntry.name`    | `String*`    | Scope name bindings   |

The proposed `Name { StrView name; Target* target; }` is a clean abstraction, but how does it integrate?

**Options:**
- **(a)** Replace `TypeElmt.name` with `Name` and `ShapeEntry.name` with `Name*` — changes the runtime data model, affects every element/map in memory.
- **(b)** Keep `StrView` for the local name, add a **separate** `Target* ns` field alongside it (e.g., `TypeElmt.ns` and `ShapeEntry.ns`). Additive, doesn't change the `StrView` contract. `NULL` means no namespace — zero overhead for the common case.
- **(c)** Encode namespace in the name string itself (e.g., `"svg.rect"`) and resolve at lookup time — simpler but loses structured access.

**Decision:** **(a) with Name struct using `String*` instead of `StrView`.** Replace `TypeElmt.name` with `Name` and `ShapeEntry.name` with `Name*`. `AstIdentNode.name` and `NameEntry.name` remain `String*` — var/fn/type names do not support namespace.

---

### Q2. Element Built-in Properties: `.tag`, `.namespace`, `.local`

Currently there's **no built-in `.tag` property** on elements. `el.tag` goes through `elmt_get()` which looks up `"tag"` as a regular attribute — if `"tag"` isn't an attribute, it returns `null`.

**Questions:**
- Should `.tag` become a built-in property of elements (like `.length` on lists)?
- If so, what does it return for `<svg.rect>`? A namespaced symbol `'svg.rect'`? A plain symbol `'rect'`?
- Should `.namespace` and `.local` be additional built-in properties?
  - `el.namespace` → `'http://www.w3.org/2000/svg'` (URL as symbol)
  - `el.local` → `'rect'` (local name as symbol)

**Decision:** **KIV.** Deferred to a later phase. Focus on core namespace infrastructure first.

---

### Q3. Dot-chain Ambiguity in Member Access

The AST represents `a.b.svg.c` as nested left-recursive `AstFieldNode`:

```
member(member(member(a, b), svg), c)
```

The parser sees `svg` as just another field access on the result of `a.b`. **How does the compiler know `svg` is a namespace prefix and not a field?**

**Options:**
- **(a) AST-build-time rewriting:** During `build_ast.cpp`, when encountering a `member_expr` chain, check if any segment matches a declared namespace prefix, and rewrite the subtree into a namespaced field access. Complex — requires look-ahead across the chain.
- **(b) Runtime resolution:** `fn_member` checks if the key matches a namespace prefix and defers. Late and can't produce a single namespaced lookup key.
- **(c) Restrict namespace usage:** Only allow `ns.name` in **element literals** and **type declarations**, not in arbitrary member access chains. For runtime access to namespaced attributes, require `el[symbol("href", 'xlink_url')]`.

**This is the critical design question.** The grammar produces a left-recursive tree of dots — recognizing `svg` as a namespace mid-chain requires context that the parser doesn't have.

**Decision:** **(a) AST-build-time rewriting.** Parse `a.b.svg.c` as normal member access. During AST building, check each member field — if it matches a declared namespace prefix, merge it with the following name to form a namespace-qualified `Name`. The tree-sitter grammar does not change for member access; the semantic rewriting happens in `build_ast.cpp`.

---

### Q4. Grammar Details for `namespace` Statement

- Is `namespace` a new keyword? (Currently not reserved in the grammar.)
- Where can it appear? Only at top-level before `content`? Alongside `import`?
- The `at` keyword — is it also used in `for` loops (`for k, v at expr`). Does this create ambiguity?

**Decision:** `namespace` is a new keyword. It appears at **global level only**, alongside `import` statements (before `content`). It is **file-specific** — cannot be imported or exported. The `at` keyword is already used in `for` loops but there is no ambiguity since `namespace` only appears at top-level, never inside expressions.

---

### Q5. Namespaced Symbol Internal Representation

Currently `Symbol` is just a `String` (flat bytes, `typedef String Symbol`). If namespaced symbols carry a namespace URL:

**Options:**
- **(a)** Embed namespace in the symbol string using Clark notation: `"{http://www.w3.org/2000/svg}rect"`. Simple but expensive for comparison and ugly for debugging.
- **(b)** Add a pointer from the symbol to a namespace `Target*`. Requires either a new `NamespacedSymbol` struct extending `String`, or a side table / registry.
- **(c)** Use a global symbol→namespace registry keyed by symbol pointer. Lookup at comparison time.

This also affects **symbol comparison**: `'svg.rect' == 'svg.rect'` must compare both local name AND namespace URL, not just the string content.

**Decision:** **(b) New `Name`/`Symbol` struct with `String* name` + `Target* ns`.** Symbol is no longer `typedef String Symbol`. The new struct carries the namespace target pointer directly. Comparison checks both `name` content and `ns` target equality (by semantic URI, not prefix — see Q9).

---

### Q6. Namespace Scope and Inheritance

- Are namespaces **file-local** or **global** across imports?
- If module A declares `namespace svg at '...'`, does module B (importing A) also see `svg`?
- Can an imported module's elements use namespace prefixes that the importing module hasn't declared?

**Decision:** Namespaces are **file-local**. They cannot be imported or exported. Each file declares its own namespace prefixes independently. Imported module's elements carry resolved namespace targets (via `Name.ns`), not prefixes — so they work correctly regardless of the importing file's prefix declarations.

---

### Q7. `symbol()` Two-Argument Return Type

Currently `symbol()` returns a plain `String` tagged as `LMD_TYPE_SYMBOL`. If `symbol("href", 'xlink_url')` returns a namespaced symbol:

- How is the namespace URL stored in the returned symbol value? (Can't fit in a `String`.)
- Does this require a new type tag, or can the same `LMD_TYPE_SYMBOL` carry optional namespace info?
- Is the namespace URL interned/pooled to enable pointer comparison?

**Decision:** Since `Symbol` is now `Name { String* name; Target* ns; }`, the 2-arg `symbol(name, url)` constructs a `Name` with the `ns` field set to a `Target*` resolved from the URL. Same `LMD_TYPE_SYMBOL` type tag. The `Target*` can be pooled/shared for identical URLs.

---

### Q8. XML Round-Trip Mapping

- **Input:** When reading XML with `input("doc.xml", 'xml)`, are `xmlns:` declarations automatically converted to Lambda namespace declarations? Or do they become regular element attributes?
- **Output:** When formatting to XML, how are Lambda namespaces converted to `xmlns:` declarations? Which element gets the `xmlns:` — the root, or the first element using the prefix?
- **Today's behavior:** Namespaces survive XML round-trips *by accident* — colons are legal in stored name strings (e.g., tag `"soap:Envelope"`, attr `"xmlns:soap"`). There is **no semantic namespace model** currently.

**Decision:** **KIV.** XML parser upgrade deferred. Fix Lambda core namespace support first, then upgrade XML parser to produce `Name`-based elements with resolved namespaces.

---

### Q9. Comparison Semantics

When comparing namespaced symbols or element tags:

- Is comparison **by prefix** (syntactic: `svg.rect == svg.rect`)? Fast but fragile — two different prefixes mapping to the same URI would not be equal.
- Or **by resolved URI** (semantic: same URI = equal regardless of prefix)? Correct per XML semantics but requires URI resolution at comparison time.

**Decision:** **Semantic comparison (by resolved target).** Two namespaced names are equal if their local `name` strings match AND their `ns` targets resolve to the same URI/path. This is correct per XML semantics. Note: `Target` can be in URL or Lambda path form — both must be compared. For non-namespaced names (`ns == NULL`), comparison is by `name` string only (as today).

---

## Decision Summary

| #   | Question                                            | Decision |
| --- | --------------------------------------------------- | -------- |
| Q1  | Where does namespace attach in data model?          | `Name { String* name; Target* ns; }` replaces `TypeElmt.name` and `ShapeEntry.name`. AST/scope names unchanged. |
| Q2  | Built-in `.tag`, `.namespace`, `.local` properties? | KIV — deferred. |
| Q3  | Dot-chain ambiguity resolution?                     | AST-build-time rewriting in `build_ast.cpp`. Parse as member access, then merge ns+name. |
| Q4  | `namespace` grammar details?                        | New keyword, global-level alongside `import`, file-local (no import/export). |
| Q5  | Namespaced symbol internal representation?          | New `Name`/`Symbol` struct: `String* name` + `Target* ns`. No longer `typedef String Symbol`. |
| Q6  | Namespace scope and inheritance?                    | File-local. Cannot be imported or exported. |
| Q7  | `symbol()` two-arg return type?                     | Returns `Name` with `ns` set. Same `LMD_TYPE_SYMBOL` tag. |
| Q8  | XML round-trip mapping?                             | KIV — XML parser upgrade deferred. |
| Q9  | Comparison by prefix or by URI?                     | Semantic comparison by resolved target URI/path. |
