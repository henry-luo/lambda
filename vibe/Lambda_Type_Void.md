# Should Lambda Introduce `void` as a Return Type?

This document analyzes whether Lambda Script should introduce a `void` type for procedural functions (`pn`), or continue using `null` as the return type/value for side-effect-only functions.

## Summary

**No, introducing `void` is not necessary.** Lambda's current approach of using `null` works well and aligns with the language's expression-oriented design.

---

## Background

In Lambda, procedural functions (`pn`) can perform side effects like file I/O, mutable state changes, and imperative control flow. The question is: what should these functions return when they have no meaningful result?

Currently, Lambda uses `null`:
```lambda
pn save_report(data) {
    data |> "/tmp/report.json"
    null   // Implicit or explicit: no meaningful return
}
```

---

## Pros and Cons Analysis

### Using `null` as Return Type/Value (Current Approach)

#### Pros

1. **Simplicity** — No new type needed; `null` already exists and is well-understood

2. **Uniform type system** — Every expression has a value; `pn` functions always return something (`null` if nothing explicit)

3. **Composability** — `null` can flow through pipes and expressions naturally:
   ```lambda
   pn_action() | some_check
   ```

4. **Optional typing** — `null` integrates with `T?` (optional) types seamlessly:
   ```lambda
   fn may_fail() int?  // Returns int or null
   ```

5. **Consistent semantics** — "No meaningful result" and "absence of value" share the same representation

6. **Error handling** — Clean distinction between success and failure:
   ```lambda
   // In runtime: result != 0 ? ItemError : ItemNull
   // Success = null, Failure = error
   ```

#### Cons

1. **Semantic ambiguity** — `null` can mean "no value", "not found", or "void return" — different meanings conflated

2. **Type safety** — Can accidentally use a `null` return where a real value was expected

3. **Documentation** — Less self-documenting; `void` would clearly signal "don't use the return value"

---

### Introducing `void` Type

#### Pros

1. **Clarity** — Explicitly signals "this function is called for side effects only"

2. **Type safety** — Compiler can warn if you try to use a `void` return:
   ```lambda
   let x = print("hi")  // Could be a compile error with void
   ```

3. **Familiar** — Developers from C/C++/Java/TypeScript backgrounds expect `void`

#### Cons

1. **Complexity** — Adds another type to the type system

2. **Breaks uniformity** — Lambda's "everything is an expression" philosophy is violated

3. **Runtime representation** — What value does `void` actually hold? Still needs internal representation

4. **Edge cases** — Creates awkward type combinations:
   - What is `[void]`?
   - What is `void?`?
   - What is `void | int`?

5. **Minimal benefit** — In a scripting language with type inference, the benefit is marginal

---

## Recommendation

**Stick with `null` for Lambda.** Here's why:

### 1. Lambda is Expression-Oriented

Even `pn` functions return a value (the last expression). This is consistent with functional language heritage where every construct produces a value.

### 2. The `null` Pattern is Idiomatic

Many modern languages use a unit/null value rather than true `void`:
- Kotlin: `Unit`
- Swift: `Void` = `()`
- Rust: `()`
- Haskell: `()`

### 3. Current Error Handling is Clean

```lambda
pn write_file(path, data) {
    // ... side effects ...
    null  // Success, no meaningful return
}

// Or with error handling:
pn write_file(path, data) null^error { ... }
```

### 4. Type Annotations Provide Clarity When Needed

If explicit documentation is desired, use type annotations:
```lambda
pn save_data(data) null { ... }    // Explicitly returns null
pn process(data) int { ... result } // Returns int
```

---

## When `void` Might Be Valuable

The only scenario where `void` would provide significant value is if you want **compile-time enforcement** that prevents using the return value:

```lambda
pn log(msg) void { print(msg) }

let x = log("hello")  // Compile error: cannot use void value
```

However, this adds complexity for a scripting language where flexibility is typically valued over strictness.

---

## Conclusion

Lambda's use of `null` for "no meaningful return" is:
- Simpler to implement and understand
- Consistent with the expression-oriented design
- Sufficient for practical use cases
- Aligned with modern language trends (unit types)

Introducing `void` would add complexity without proportional benefit for Lambda's use cases as a data processing and scripting language.
