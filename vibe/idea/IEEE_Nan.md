# IEEE 754: Why NaN ≠ NaN

## The Rule

In IEEE 754 floating-point arithmetic:

```
NaN == NaN  → false
NaN != NaN  → true
```

NaN is **unordered** — every comparison with NaN returns false, except `!=` which returns true.

```
NaN < 1.0   → false
NaN > 1.0   → false
NaN <= NaN  → false
NaN >= NaN  → false
NaN == NaN  → false
NaN != NaN  → true
```

## What is NaN?

NaN ("Not a Number") represents the result of an **undefined or unrepresentable** floating-point operation:

| Operation | Result |
|-----------|--------|
| `0.0 / 0.0` | NaN |
| `∞ - ∞` | NaN |
| `∞ × 0` | NaN |
| `sqrt(-1)` | NaN |
| `log(-1)` | NaN |
| `0 × ∞` | NaN |

NaN is not a value — it is a **signal** that something went wrong.

## Design Rationale

William Kahan, the primary architect of IEEE 754, gave these reasons:

### 1. No Meaningful Equality

Two NaN values don't represent the same thing:

```
a = 0.0 / 0.0    → NaN  (indeterminate form)
b = sqrt(-1)      → NaN  (complex result)
c = log(-1)       → NaN  (domain error)
```

Calling `a == b == c` would be mathematically nonsensical. They are all undefined, but for completely different reasons.

### 2. Simple NaN Detection

Before `isnan()` was widely available in C libraries, the **only** portable way to detect NaN was:

```c
if (x != x) {
    // x is NaN
}
```

If `NaN == NaN` were true, there would be no simple expression to test for NaN using only comparison operators.

### 3. Propagation as a Poison Signal

NaN is designed to **propagate** through computations:

```
NaN + 1     → NaN
NaN * 100   → NaN
max(NaN, 5) → NaN  (in most implementations)
```

Making NaN unequal to everything ensures that any comparison involving NaN signals an anomaly rather than silently passing.

### 4. Safe Default for Algorithms

Consider a search algorithm:

```
found = false
for x in data:
    if x == target:
        found = true
```

If `target` is NaN and `NaN == NaN` were true, the algorithm would "find" NaN in any dataset containing NaN. This is almost certainly not the intended behavior — the data is corrupt, and the search should fail.

## The Trade-off

IEEE 754 **intentionally sacrifices reflexivity** — the mathematical axiom that `x == x` for all `x`.

| Property | Standard Math | IEEE 754 |
|----------|:---:|:---:|
| Reflexivity: `x == x` | ✓ always | ✗ fails for NaN |
| Symmetry: `x == y ↔ y == x` | ✓ | ✓ |
| Transitivity | ✓ | ✓ (vacuously, since NaN never equals anything) |

The committee judged that **reliable NaN detection** was more valuable than preserving reflexivity for a sentinel value that represents "no meaningful number."

## Consequences in Practice

### Gotcha: Collections and Deduplication

```python
s = {float('nan'), float('nan')}
len(s)  # 2  — both NaNs kept because neither equals the other
```

### Gotcha: Sorting

NaN has no defined ordering. Sorting arrays containing NaN produces **undefined behavior** in many implementations.

### Gotcha: Hash Maps

Some languages hash NaN to a consistent value but equality fails, creating phantom entries that can never be retrieved by lookup.

## How Lambda Handles It

Lambda follows IEEE 754 for `==` but provides `is nan` for explicit detection:

```lambda
nan == nan         // false  (IEEE 754)
nan is nan         // true   (Lambda's NaN check)
(0/0) is nan       // true
1.0 is nan         // false

// Replace NaN in data
[1, nan, 3] | (if (~ is nan) 0 else ~)   // [1, 0, 3]
```

This gives users an intentional, readable way to detect NaN without relying on the `x != x` idiom.
