# Lambda Name Syntax Unification

## Background

Lambda currently has inconsistent quoting rules for names across different contexts:

| Context | Single-Quote `'sym'` | Double-Quote `"str"` | Unquoted `ident` |
|---------|----------------------|----------------------|------------------|
| Element name | `<'div'>` ✓ | `<"div">` ✗ | `<div>` ✓ |
| Map key | `{'key': val}` ✓ | `{"key": val}` ✓ | `{key: val}` ✓ |
| Attribute name | `'attr': val` ✓ | `"attr": val` ✓ | `attr: val` ✓ |

Element names already ban double-quoted strings, but map keys and attributes allow both quoting styles. This creates an asymmetry with no clear semantic justification.

## Two Options

### Option 1: Ban `"string"` in name/key positions (Chosen)

Restrict all name positions (element names, map keys, attribute names) to **unquoted identifiers** or **single-quoted symbols**. Double-quoted strings are reserved exclusively for string data/content.

```lambda
// Allowed
{key: val}          // unquoted identifier
{'key': val}        // single-quoted symbol
<div attr: val>     // unquoted
<'div' 'attr': val> // single-quoted

// Not allowed
{"key": val}        // double-quoted key
<"div" "attr": val> // double-quoted name
```

**Pros:**
- **Semantic clarity.** A single, language-wide rule: `'quoted'` = interned symbol/name, `"quoted"` = string data. No ambiguity about which quote style to use for names.
- **Consistency.** Element names already enforce this rule. Option 1 extends the same rule to map keys and attributes, completing a pattern already in progress.
- **Foundation for unicode identifiers.** Single-quoted symbols can be used uniformly across the entire language for names that aren't valid unquoted identifiers:
  ```lambda
  let '变量' = 42
  let result = data.'字段'.'子字段'
  <'文档' '标题': "Hello">
  fn '计算'(x) => x * 2
  ```
- **O(1) equality.** Symbols are interned — pointer comparison for all name positions, never byte-by-byte string comparison.

**Cons:**
- **No JSON-as-source compatibility.** `{"key": val}` is no longer valid Lambda source. Users must write `{key: val}` or `{'key': val}` instead.

### Option 2: Allow `"string"` in element names

Extend double-quoted string support to element names, making all name positions accept both quote styles.

```lambda
<"div">             // now allowed
<"div" "attr": val> // allowed
{"key": val}        // still allowed
```

**Pros:**
- **JSON compatibility.** `{"key": val}` remains valid Lambda source, matching JSON object syntax.

**Cons:**
- **Two ways to quote names.** Both `'name'` and `"name"` work in name positions, with no guidance on when to use which. This creates style fragmentation.
- **Blurred semantic boundary.** The distinction between interned symbols and string data becomes unclear. Are `"key"` and `'key'` the same? Different? The answer depends on context, which is confusing.
- **Blocks unicode identifier unification.** If `"..."` means "string" in some positions and "name" in others, it cannot serve as a consistent quoting mechanism for identifiers.

## Decision: Option 1

We adopt **Option 1** — single-quoted symbols and unquoted identifiers are the only valid name syntax. Double-quoted strings are reserved for string data.

### Rationale

1. **The grammar already leans this way.** Element names ban double-quoted strings. Option 1 completes the pattern rather than reversing it.

2. **Clear semantic rule.** One rule for the entire language:

   | Syntax | Meaning | Interned | Comparison |
   |--------|---------|----------|------------|
   | `unquoted` | identifier → symbol | Yes | O(1) pointer |
   | `'quoted'` | symbol (name) | Yes | O(1) pointer |
   | `"quoted"` | string (data) | No | O(n) byte |

3. **JSON compatibility is a non-issue.** Lambda processes JSON via its input pipeline (`read("data.json")`). No mainstream language requires source literals to be valid JSON — even JavaScript doesn't (JSON keys must be double-quoted, JS object keys don't). The migration from `{"key": val}` to `{key: val}` in Lambda source is trivial.

4. **Enables future unicode identifiers.** A unified `'symbol'` quoting mechanism works everywhere — variables, paths, element names, keys, function names — without conflicting with string semantics.

## Extended Name Syntax

Beyond banning `"string"` from name positions, we unify and extend naming across the language in two dimensions:

### 1. Multi-Segment Dotted Names

Currently `ns_identifier` is limited to exactly two segments: `ns.name`, both `$.identifier`. We extend this to **arbitrary depth**, where each segment is either `$.identifier` or `$.symbol`:

```
dotted_name := (identifier | symbol) ('.' (identifier | symbol))*
```

Dotted names are accepted in all name positions that previously accepted `ns_identifier`:

```lambda
// Element name
<a.b.'c' ...>

// Attribute key
<div a.b.'c': val>

// Map key
{a.b.'c': val}

// Object name and key
{a.b.'c' a.b.'c': val}
```

This replaces the current two-segment `ns_identifier` rule. The existing `ns_identifier` usages (e.g. `<svg.rect>`, `svg.width: 100`) continue to work as before — they are simply the two-segment case of the generalized form.

### 2. Symbol Support in Declarations

Single-name positions in declarations (variable bindings, function names, parameter names, type names) are extended to accept `$.symbol` as an alternative to `$.identifier`:

```lambda
// Variable binding
let 'name' = val
let a, 'b' = expr

// Function declaration — name and parameters
fn 'calculate'('input', 'scale') => 'input' * 'scale'
pn '处理'('数据') { ... }

// Type declaration
type '名前' = int | string
type 'Circle' { radius: float }
```

Symbol names and identifier names are **syntactically interchangeable** — `'name'` and `name` refer to the same binding. The symbol form is required only when the name is not a valid ASCII identifier (e.g. unicode names, names with hyphens):

```lambda
fn greet(x) => x * 2       // declared with identifier
'greet'(5)                  // called with symbol — same function

fn '挨拶'(x) => x * 2      // unicode name — symbol required
'挨拶'(5)                   // called with symbol
```

## Unified Name Grammar

After all changes, the name system follows a single rule:

```
name_segment := identifier | symbol         // 'sym' or ident
dotted_name  := name_segment ('.' name_segment)*
```

| Position | Accepts | Examples |
|----------|---------|----------|
| Element name | `dotted_name` | `<div>`, `<'div'>`, `<a.b.'c'>` |
| Attribute key | `dotted_name` | `key:`, `'key':`, `a.b.'c':` |
| Map key | `name_segment` or `string`¹ | `{key:}`, `{'key':}`, `{"it's":}` |
| Object literal name | `name_segment` | `{Obj ...}`, `{'Obj' ...}` |
| `let` binding | `name_segment` | `let x`, `let 'x'` |
| `fn` / `pn` name | `name_segment` | `fn foo`, `fn 'foo'` |
| `fn` parameter | `name_segment` | `(x, y)`, `('x', 'y')` |
| `type` name | `name_segment` | `type T`, `type 'T'` |
| Path access | `name_segment` | `a.b`, `a.'b'` |

¹ `dotted_name` is **not** accepted in `map_item` or `object_literal` because Tree-sitter's PEG parser greedily consumes dotted expressions (e.g. `{state.counters, key: val}` would parse `state.counters` as a dotted name instead of a member-access expression). `"string"` is retained as a fallback for map keys that contain characters incompatible with symbol syntax (e.g. keys containing single quotes like `"'"`).  `build_key_string` performs raw byte extraction between quote delimiters without escape processing, so symbol keys with escape sequences would not round-trip correctly.

### Grammar Changes Required

**Remove `$.string` from most name positions:**
- `attr_name`: removed `$.string`
- `attr_type`: removed `$.string`
- `map_item`: **retains** `$.string` as fallback for keys containing `'` or escape sequences

**Extend `ns_identifier` → `dotted_name`:**
- Replaced two-segment `ns_identifier` with arbitrary-depth `dotted_name`
- Each segment accepts `$.identifier` or `$.symbol`
- `dotted_name` used in: `element` tag, `attr_name`
- `dotted_name` **not** used in: `map_item`, `object_literal` (PEG ambiguity with spread/member-access expressions)

**Add `$.symbol` to declaration names:**
- `assign_expr` (`let`): accept `choice($.identifier, $.symbol)` for `name` fields
- `fn_stam` / `fn_expr_stam`: accept `choice($.identifier, $.symbol)` for `name` field
- `parameter`: accept `choice($.identifier, $.symbol)` for `name` field
- `type_assign` / `entity_type` / `object_type`: accept `choice($.identifier, $.symbol)` for `name` field
- `object_literal`: accept `choice($.identifier, $.symbol)` for `type_name` field
- `named_argument`: accept `choice($.identifier, $.symbol)` for `name` field

## Implementation Notes

### build_ast.cpp: `node_name_text()` helper
A helper function `node_name_text(TSNode)` was added to extract name text from identifier or symbol nodes uniformly. For symbols, it strips the surrounding quotes (`str++; length -= 2`). Used at all name extraction sites.

### build_key_string: raw extraction limitation
`build_key_string()` extracts map key text by raw byte copy between the first and last quote characters, with **no escape sequence processing**. This applies to both `SYM_STRING` and `SYM_SYMBOL`. Consequence: `"'"` stores `'` (1 char) correctly, but a hypothetical symbol `'\''` would store raw `\'` (2 chars) — semantically different. This is why `$.string` is retained in `map_item`.

A `// todo: handle string and symbol escape` comment marks this for future work.

### Migration
422 replacements across 25 `.ls` files converted `"key":` → `key:` or `'key':`. Three keys in `lambda/package/latex/symbols.ls` were reverted to `"string"` syntax because they contain single-quote characters that cannot be represented as symbols without escape processing.
