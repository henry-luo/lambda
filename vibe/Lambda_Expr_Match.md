# Proposal: `match` Expression for Lambda Script

> **Status**: Draft Proposal
> **Date**: 2026-02-11
> **Related Documentation**:
> - [Lambda Expressions](../doc/Lambda_Expr_Stam.md) — Expressions and statements
> - [Lambda Type System](../doc/Lambda_Type.md) — Type hierarchy and patterns
> - [Lambda Functions](../doc/Lambda_Func.md) — Function declarations
> - [Lambda Error Handling](../doc/Lambda_Error_Handling.md) — Error handling

---

## Table of Contents

1. [Motivation](#motivation)
2. [Design Goals](#design-goals)
3. [Prior Art](#prior-art)
4. [Syntax Specification](#syntax-specification)
   - [Expression Form](#expression-form)
   - [Statement Form](#statement-form)
5. [Case Patterns](#case-patterns)
   - [Type Patterns](#type-patterns)
   - [Literal Patterns](#literal-patterns)
   - [Or-Patterns](#or-patterns)
   - [Default Arm](#default-arm)
6. [The `~` Current-Item Reference](#the--current-item-reference)
7. [Semantics](#semantics)
   - [Evaluation Order](#evaluation-order)
   - [Exhaustiveness](#exhaustiveness)
   - [Type Narrowing](#type-narrowing)
   - [Value of Match](#value-of-match)
8. [Integration with Existing Features](#integration-with-existing-features)
9. [Comprehensive Examples](#comprehensive-examples)
10. [Grammar Sketch](#grammar-sketch)
11. [Alternatives Considered](#alternatives-considered)
12. [Future Extensions](#future-extensions)
13. [Open Questions](#open-questions)

---

## Motivation

Lambda currently handles conditional branching with `if`/`else if` chains and type checks via `is`:

```lambda
fn describe(value: int | string | bool) => {
    if (value is int) "integer: " ++ string(value)
    else if (value is string) "text: " ++ value
    else "boolean: " ++ string(value)
}
```

This approach has several drawbacks as programs grow in complexity:

1. **Verbosity** — Repeated `else if (value is ...)` chains are noisy and error-prone.
2. **No exhaustiveness checking** — The compiler cannot verify that all variants of a union type have been handled. A missing branch silently becomes a runtime bug.
3. **No literal dispatch** — Matching against a set of constant values requires manual equality checks: `if (x == 1) ... else if (x == 2) ...`.
4. **Readability** — Complex multi-way branching is hard to read and maintain.

A dedicated `match` construct addresses all of these.

---

## Design Goals

| Goal | Description |
|------|-------------|
| **Concise** | Less boilerplate than `if`/`else if` chains for multi-way branching. |
| **Exhaustive** | The compiler can verify all cases of a union type or literal set are covered. |
| **Expression-first** | Match is an expression that produces a value, consistent with Lambda's expression-oriented design. Also available as a statement for procedural contexts. |
| **`~` for access** | The matched value is accessed via `~` in arm bodies, consistent with Lambda's pipe semantics. No new destructuring syntax needed. |
| **Type-expression cases** | Case arms use Lambda's existing type expressions (including literal values as types), not a separate pattern language. |
| **Consistent** | Reuses Lambda's existing `is` type-check semantics and `~` current-item convention. |
| **Familiar** | Draws from well-proven designs in Rust, Swift, Scala, Kotlin, and C#. |

---

## Prior Art

The design draws selectively from the best aspects of established pattern matching systems:

| Language | Key Influence |
|----------|---------------|
| **Rust** | Exhaustiveness checking, or-patterns (`\|`), match-as-expression. Rust's match is the gold standard for safety: every possible value must be handled or a compile error is raised. |
| **Kotlin** | `when` expression with type checks and literal branches, `else` default branch. Kotlin demonstrates clean multi-way branching without destructuring overhead. |
| **Swift** | Enum matching, exhaustive switch. Swift shows how pattern matching integrates naturally with a type-safe language. |
| **Scala** | Type patterns with `case x: Type`, `case _` default. Scala demonstrates matching against rich algebraic data types. |
| **OCaml/F#** | Exhaustiveness as a first-class compiler feature, clean `\| pattern -> expr` syntax. |
| **C# 8+** | Switch expressions as expressions rather than statements, type patterns, `_` discard. C# shows how to add pattern matching to a language retrofitted with it. |

**Key takeaways adopted**:
- Or-patterns via `|` within a single arm (Rust, OCaml).
- Exhaustiveness checking enforced by the compiler (Rust, OCaml, Swift).
- Match-as-expression producing a value (Rust, Scala, C# switch expression, Kotlin `when`).
- `default` as catch-all (like Kotlin `else`, C# `_`, Go/C `default`).
- `~` for current-item access (unique to Lambda, consistent with pipe semantics).

---

## Syntax Specification

### Unified Match Expression

Match has a single, unified syntax with required braces. Each arm can be in **expression form** or **statement form**. The match construct works in both functional and procedural contexts.

```
match <expr> {
    case <type_expr>: <expr>            // expression arm
    case <type_expr> { <statements> }   // statement arm
    default: <expr>                     // default expression arm
    default { <statements> }            // default statement arm
}
```

**Expression arms** (with `:`):

```lambda
fn classify(n: int) => match n {
    case 0: "zero"
    case int: if (~ > 0) "positive" else "negative"
}

let label = match status {
    case 'ok': "success"
    case 'warn': "warning"
    case 'error': "failure"
}
```

**Statement arms** (with `{ }`):

```lambda
pn handle_event(event) {
    match event.kind {
        case 'click' {
            var count = state.clicks;
            count = count + 1;
            update_state({clicks: count})
        }
        case 'keypress' {
            if (event.key == "Escape") return null;
            process_key(event.key)
        }
        default {
            // ignore other events
        }
    }
}
```

**Mixed arms** — expression and statement arms can be freely combined:

```lambda
fn describe(shape) => match shape.tag {
    case 'circle' {
        let area = 3.14159 * shape.r ^ 2;
        "circle with area " ++ string(area)
    }
    case 'rect' {
        let area = shape.w * shape.h;
        "rectangle with area " ++ string(area)
    }
    default: "unknown shape"
}
```

With parenthesized scrutinee (also valid):

```lambda
let result = match (get_status()) {
    case 200: "OK"
    case 404: "Not Found"
    default: "Other"
}
```

### Syntactic Notes

- **Braces are required** — `match <expr> { ... }` always uses `{ }` to delimit the arm block.
- Parentheses around the scrutinee are **optional** (unlike `if` and `for`).
- **Expression arms** use `case <type_expr>: <expr>` — the `:` is the arm separator.
- **Statement arms** use `case <type_expr> { <stmts> }` — curly braces delimit the arm body.
- Expression and statement arms can be **freely mixed** within one match.
- `default` is the catch-all arm keyword. It matches anything.
- No comma or semicolon between arms — arms are delimited by `case` and `default`.
- **Fall-through does not exist.** Each arm is independent (like Rust, unlike C `switch`).
- **`~`** refers to the matched value in arm bodies, just like in pipe expressions.

---

## Case Patterns

Case arms use **type expressions** as patterns. In Lambda's type system, literal values are valid type expressions — a value `200` can act as a singleton type that matches only that exact value. This unifies type matching and value matching into a single mechanism.

### Type Patterns

Match on the runtime type using any type expression:

```lambda
match data {
    case int: ~ * 2
    case string: ~.upper()
    case [int]: sum(~)
    default: error("unsupported")
}
```

Complex type expressions work as patterns:

```lambda
match value {
    case int | float: "numeric: " ++ string(~)
    case string: "text: " ++ ~
    case [any]: "array of " ++ string(len(~))
    case {name: string, age: int}: ~.name ++ " is " ++ string(~.age)
    default: "other"
}
```

### Literal Patterns

Since literal values are type expressions in Lambda, they work directly as case patterns. The case checks equality against the literal value.

**Integer literals:**

```lambda
match code {
    case 200: "OK"
    case 301: "Moved"
    case 404: "Not Found"
    case 500: "Server Error"
    default: "Unknown"
}
```

**Boolean and null literals:**

```lambda
match value {
    case true: "yes"
    case false: "no"
    case null: "nothing"
}
```

**String literals:**

```lambda
match command {
    case "help": show_help()
    case "version": show_version()
    case "quit": exit()
    default: error("unknown command: " ++ ~)
}
```

**Symbol literals** (quoted with `'...'`):

```lambda
match direction {
    case 'north': (0, 1)
    case 'south': (0, -1)
    case 'east': (1, 0)
    case 'west': (-1, 0)
    default: (0, 0)
}
```

### Or-Patterns

Combine multiple type expressions into a single arm using `|`. The arm matches if **any** of the patterns match.

```lambda
match day {
    case 'mon' | 'tue' | 'wed' | 'thu' | 'fri': "weekday"
    case 'sat' | 'sun': "weekend"
}
```

```lambda
match code {
    case 200 | 201 | 204: "success"
    case 301 | 302: "redirect"
    case 400 | 403 | 404: "client error"
    case 500 | 502 | 503: "server error"
    default: "unknown"
}
```

Or-patterns also work with type expressions:

```lambda
match value {
    case int | float: "number: " ++ string(~)
    case string | symbol: "text-like: " ++ string(~)
    default: "other"
}
```

> **Note**: `|` in case patterns denotes alternatives, which is consistent with Lambda's union type syntax `int | string`. A `case int | string:` arm matches values that are `int` or `string` — exactly the same as checking `~ is int | string`.

### Default Arm

The `default` keyword serves as the catch-all arm. It matches any value not matched by previous arms.

```lambda
match status {
    case 'ok': handle_ok()
    case 'error': handle_error()
    default: handle_unknown(~)     // ~ is the unmatched value
}
```

- `default` must be the **last** arm (if present).
- When the scrutinee type is `any` or an infinite set, `default` is **required** for exhaustiveness.
- For finite union types, `default` is optional if all cases are explicitly covered.

---

## The `~` Current-Item Reference

Instead of introducing destructuring syntax in patterns, `match` reuses Lambda's existing `~` convention from pipe expressions. Inside any match arm, `~` refers to the **matched value** (the scrutinee).

This keeps the pattern language simple — cases are just type expressions — while the body expression has full access to the matched value's structure via Lambda's standard member access, indexing, and pipe operators.

### Basic Access

```lambda
match data {
    case int: ~ * 2                    // arithmetic on the matched int
    case string: ~.upper()             // method call on the matched string
    case [int]: sum(~)                 // pass the matched array to a function
    default: string(~)
}
```

### Field Access

```lambda
match event {
    case {type: symbol, x: int, y: int}: handle_click(~.x, ~.y)
    case {type: symbol, key: string}: handle_key(~.key)
    default: null
}
```

### Nested Pipe Within Arms

Since `~` already refers to the matched value, pipe expressions within match arms work naturally:

```lambda
match data {
    case [int]: ~ | ~ ^ 2 | sum       // square each element, then sum
    case [string]: ~ | ~.upper()       // uppercase each string
    default: null
}
```

### Index Access

```lambda
match args {
    case [string]: execute(~.0, ~[1 to -1])   // first element + rest
    default: show_help()
}
```

### Why `~` Instead of Destructuring

| Aspect | Destructuring Patterns | `~` Current-Item |
|--------|----------------------|------------------|
| **Complexity** | New pattern sub-language for maps, arrays, lists, elements | Zero new syntax — reuses existing member access |
| **Learning curve** | Significant: nested patterns, rest patterns, field shorthand | Minimal: same `~` as in pipe expressions |
| **Power** | Bind multiple inner values simultaneously | Access any depth via `~.a.b.c`, `~[0]`, etc. |
| **Consistency** | Unique to match | Same convention as `\|` pipe, `where` filter |
| **Implementation** | Complex: pattern compiler, backtracking | Simple: `~` is a lexical binding |

The `~` approach trades some conciseness in deeply nested extraction for significant simplicity, consistency, and a smaller language surface area.

---

## Semantics

### Evaluation Order

1. The **scrutinee** expression is evaluated exactly once.
2. Arms are tested **top to bottom**, in source order.
3. For each arm, the type expression is checked (via `is` semantics).
4. The **first** arm whose type expression matches is selected.
5. `~` is bound to the scrutinee value in the arm body.

### Exhaustiveness

The compiler performs **exhaustiveness analysis** to verify all possible values of the scrutinee are covered:

- **Union types**: Every constituent type must be handled. `int | string | null` requires arms covering all three (or a `default`).
- **Boolean**: Must cover `true` and `false` (or include `default`).
- **Nullable**: `T?` requires handling both `T` and `null`.
- **Open types**: A `default` arm is required when the value space is not finite (integers, strings, etc.).

```lambda
// ✅ Exhaustive — all union members covered
fn fmt(v: int | string) => match v {
    case int: "int"
    case string: "str"
}

// ❌ Compile error — missing `null` case
fn fmt(v: int?) => match v {
    case int: "int"
    // Error: match is not exhaustive; missing case: null
}

// ✅ Fixed with default or explicit null arm
fn fmt(v: int?) => match v {
    case int: string(~)
    case null: "none"
}
```

When the scrutinee type is `any` or an infinite set (e.g., `int`, `string`), a `default` arm is required.

### Type Narrowing

Within a matched arm, `~` has its type **narrowed** to the matched type:

```lambda
fn process(value: int | string | [int]) => match value {
    case int: ~ * 2                 // ~: int — arithmetic is safe
    case string: ~.upper()          // ~: string — string methods available
    case [int]: sum(~)              // ~: [int] — collection functions work
}
```

This is consistent with Lambda's existing type guard behavior in `if (value is T) { ... }` blocks.

### Value of Match

- **Expression arms** (`case T: expr`): The match expression evaluates to the value of the selected arm's expression. All expression arms should produce compatible types (unified via standard type inference).
- **Statement arms** (`case T { stmts }`): The statements in the selected arm's block are executed. In expression context, the match result is `null` for statement arms.

If no arm matches (which should be prevented by exhaustiveness checking), the match expression evaluates to `null`.

---

## Integration with Existing Features

### With Pipes

Match expressions compose with Lambda's pipe operator:

```lambda
events | match ~ {
    case {type: symbol, key: string}: "pressed " ++ ~.key
    default: "other"
}
```

### With For Expressions

```lambda
for (item in data)
    match item {
        case int: ~ * 2
        case string: len(~)
        default: 0
    }
```

### With Error Handling

```lambda
fn process(input: string) int^ {
    let parsed = parse(input)?;
    match parsed {
        case {value: int}: ~.value
        case {value: string}: raise error("expected int, got string")
        default: raise error("unexpected structure")
    }
}
```

### With Let Bindings

```lambda
let category = match type(data) {
    case int: 'numeric'
    case float: 'numeric'
    case string: 'text'
    default: 'other'
};
```

### With String Patterns

Lambda's string patterns integrate as type expressions in cases:

```lambda
string Email = \w+ "@" \w+ "." \a[2, 6]
string Phone = \d[3] "-" \d[3] "-" \d[4]

match contact_info {
    case Email: "email: " ++ ~
    case Phone: "phone: " ++ ~
    case string: "unknown format: " ++ ~
}
```

---

## Comprehensive Examples

### JSON Value Processor

```lambda
fn to_html(json) => match json {
    case null: "<em>null</em>"
    case bool: "<strong>" ++ string(~) ++ "</strong>"
    case number: "<span class='num'>" ++ string(~) ++ "</span>"
    case string: "<span class='str'>\"" ++ ~ ++ "\"</span>"
    case [any]: "<ul>" ++ (~ | "<li>" ++ to_html(~) ++ "</li>" | concat) ++ "</ul>"
    case map: "<dl>" ++ (~ | "<dt>" ++ string(~#) ++ "</dt><dd>" ++ to_html(~) ++ "</dd>" | concat) ++ "</dl>"
}
```

### HTTP Status Handler

```lambda
fn describe_status(code: int) => match code {
    case 200: "OK"
    case 201: "Created"
    case 204: "No Content"
    case 301 | 302: "Redirect"
    case 400: "Bad Request"
    case 401: "Unauthorized"
    case 403: "Forbidden"
    case 404: "Not Found"
    case 500: "Internal Server Error"
    default: "Unknown (" ++ string(~) ++ ")"
}
```

### AST Interpreter

```lambda
fn eval(node) => match node.op {
    case 'num': node.value
    case 'add': eval(node.left) + eval(node.right)
    case 'mul': eval(node.left) * eval(node.right)
    case 'neg': -eval(node.expr)
    case 'if': if (eval(node.cond)) eval(node.then) else eval(node.else)
    default: error("unknown node: " ++ string(~))
}
```

### HTTP Router

```lambda
fn route(req) => match req.method {
    case 'GET' {
        match req.path {
            case "/": home_page()
            case "/api/users": list_users()
            default: {status: 404, body: "not found"}
        }
    }
    case 'POST' {
        match req.path {
            case "/api/users": create_user(req.body)
            default: {status: 404, body: "not found"}
        }
    }
    default {
        {status: 405, body: "method not allowed"}
    }
}
```

### Symbol Dispatch

```lambda
fn color_of(level) => match level {
    case 'info': "blue"
    case 'warn': "yellow"
    case 'error': "red"
    case 'debug': "gray"
    default: "white"
}
```

### Element Transformation

```lambda
fn transform(elem) => match elem.tag {
    case 'h1': <h2; elem.children>
    case 'div': <section; for (c in elem.children) transform(c)>
    case 'img': <figure; <img src: elem.src>>
    default: elem
}
```

### Data Type Dispatch

```lambda
fn summarize(data) => match data {
    case int | float: {type: 'number', value: ~}
    case string: {type: 'text', length: len(~), value: ~}
    case [any]: {type: 'array', count: len(~), items: ~ | summarize(~)}
    case map: {type: 'object', keys: keys(~), fields: ~ | summarize(~)}
    case null: {type: 'null'}
    default: {type: string(type(~))}
}
```

### Procedural Event Loop

```lambda
pn process_events(events) {
    for event in events {
        match event.kind {
            case 'init' {
                var state = init_state();
                log("initialized")
            }
            case 'data' {
                validate(event.payload);
                store(event.payload)
            }
            case 'error' {
                log("error: " ++ event.message);
                if (event.fatal) return null
            }
            case 'shutdown' {
                cleanup();
                return 'done'
            }
            default {
                log("unknown event: " ++ string(event.kind))
            }
        }
    }
}
```

---

## Grammar Sketch

Below is the actual Tree-sitter grammar implementation for `match`:

```js
// Match expression — unified form with required braces
// match expr { case_arms }
// Each arm can be expression form (case T: expr) or statement form (case T { stmts })
match_expr: $ => seq(
    'match', field('scrutinee', $._expression),
    '{',
    repeat1(choice($.match_arm_expr, $.match_arm_stam,
                   $.match_default_expr, $.match_default_stam)),
    '}'
),

// Expression arm:  case <type_expr>: <expr>
match_arm_expr: $ => prec.right(seq(
    'case', field('pattern', $._type_expr),
    ':', field('body', $._expression)
)),

// Default expression arm:  default: <expr>
match_default_expr: $ => prec.right(seq(
    'default', ':', field('body', $._expression)
)),

// Statement arm:  case <type_expr> { <stmts> }
match_arm_stam: $ => seq(
    'case', field('pattern', $._type_expr),
    '{', field('body', $.content), '}'
),

// Default statement arm:  default { <stmts> }
match_default_stam: $ => seq(
    'default', '{', field('body', $.content), '}'
),
```

---

## Alternatives Considered

### 1. `when` Keyword Instead of `match`

Some languages use `when` (Kotlin) or `cond` (Clojure). `match` was chosen because:
- It is the most widely recognized keyword for pattern matching (Rust, Scala, Python, F#, Elixir).
- `when` in Lambda could be confused with temporal/conditional semantics.

### 2. `case ... of` Syntax (Haskell/Elm Style)

```
case expr of
    pattern -> body
```

Rejected because:
- `->` is not used in Lambda (Lambda uses `=>` and `:`).
- `of` adds a keyword without clear benefit.

### 3. C-Style `switch` with Fall-Through

Rejected outright. Fall-through is a well-known source of bugs. Lambda's match has no fall-through, consistent with Rust, Swift, and Scala.

### 4. Using `if is` Chains (Status Quo)

Keeping the current `if (x is T) ... else if (x is U) ...` approach was considered but rejected because it cannot provide exhaustiveness checking or or-patterns — features essential to safe and expressive data processing.

### 5. `=>` Instead of `:` for Expression Arms

Using `case <pattern> => <expr>` was considered (paralleling anonymous functions). `:` was chosen instead because:
- It is more concise.
- It mirrors the `key: value` convention already used in maps.
- `=>` could be ambiguous with function expressions inside match arms.

### 6. `_` Wildcard Instead of `default`

Using `case _: ...` as the catch-all was considered (Rust, Scala convention). `default` was chosen because:
- `default` is a keyword that reads clearly as English: "in the default case, do this."
- `_` is typically a binding/discard pattern; `default` more clearly signals "everything else."
- `default` is familiar from C/C++/Java/Go `switch` statements.

### 7. Destructuring Patterns

Full destructuring patterns (map `{a, b}`, array `[head, ...tail]`, element `<tag attr: val>`) were considered but rejected in favor of the `~` current-item approach because:
- Lambda already has rich member access (`~.field`, `~[i]`, `~.a.b.c`) and pipe expressions.
- Destructuring introduces a significant new sub-language for patterns.
- The `~` convention is already familiar from pipe expressions.
- The simpler design is easier to implement, learn, and maintain.

---

## Future Extensions

The following features are deferred to future proposals. They require Lambda's type expression system to support these constructs first.

### Guard Clauses (`where`)

Arms could include a `where` boolean condition that must be true for the arm to match, reusing Lambda's existing `where` keyword:

```lambda
// Future syntax — not yet supported
match n {
    case int where ~ > 0 and ~ < 100: "small positive"
    case int where ~ >= 100: "large positive"
    case 0: "zero"
    default: "negative: " ++ string(~)
}
```

Guard clauses would enable refining a type/literal match with arbitrary boolean conditions. When added, guarded arms would **not** count toward exhaustiveness (since the guard can fail). In the meantime, use `if`/`else` inside arm bodies to achieve the same effect:

```lambda
// Current workaround
match n {
    case int: if (~ > 0 and ~ < 100) "small positive"
             else if (~ >= 100) "large positive"
             else "negative: " ++ string(~)
    case 0: "zero"
}
```

### Range Patterns (`to`)

Arms could match against a range of values using Lambda's `to` operator:

```lambda
// Future syntax — not yet supported
match score {
    case 90 to 100: "A"
    case 80 to 89: "B"
    case 70 to 79: "C"
    case 60 to 69: "D"
    default: "F"
}
```

Range patterns would be inclusive on both ends, consistent with Lambda's `to` semantics. This requires Lambda's type expression system to support range types as first-class type expressions.

---

## Open Questions

1. **Match in pipe context**: Should there be a shorthand for `items | match ~ ...`? E.g., implicit scrutinee when piped. Deferred to a future proposal for syntactic sugar.

2. **String pattern matching with captures**: Should string patterns (e.g., `Email`, `Phone`) support binding sub-matches (capture groups) in match arms? This could integrate with Lambda's string pattern system but adds complexity. Deferred.

3. **Nested match expressions**: Should `match` be valid inside the expression of another `case` arm (nested match)? This should work naturally since `match` is an expression, but indentation-sensitive parsing may need attention.

4. **Exhaustiveness for structural map types**: For `case {name: string, age: int}:`, should the compiler consider this a match against any map with those fields, or an exact-shape match? Proposed answer: structural (has at least those fields), consistent with Lambda's open structural typing.

---

*This proposal introduces `match` as a first-class expression and statement in Lambda, bringing compile-time exhaustiveness, type-expression-based case dispatch, and clean multi-way branching to the language. The design prioritizes consistency with Lambda's existing syntax (`is` type checks, `~` current-item, `:` key-value) while keeping the pattern language minimal — cases are type expressions, not a new sub-language. Guard clauses (`where`) and range patterns (`to`) are deferred to future work pending type expression support.*
