# Lambda Functions

This document covers Lambda's function system, including pure functional (`fn`) and procedural (`pn`) functions, parameters, closures, and higher-order functions.

> **Related Documentation**:
> - [Lambda Reference](Lambda_Reference.md) — Language overview
> - [Lambda Sys Func Reference](Lambda_Sys_Func.md) — Built-in system functions
> - [Lambda Expressions](Lambda_Expr_Stam.md) — Expressions and statements
> - [Lambda Type System](Lambda_Type.md) — Type annotations

---

## Table of Contents

1. [Function Overview](#function-overview)
2. [Function Declarations](#function-declarations)
3. [Function Parameters](#function-parameters)
4. [Function Calls](#function-calls)
5. [Method-Style Calls](#method-style-calls)
6. [Closures](#closures)
7. [Higher-Order Functions](#higher-order-functions)
8. [Procedural Functions](#procedural-functions)

---

## Function Overview

Lambda supports two kinds of functions:

| Kind | Keyword | Characteristics |
|------|---------|-----------------|
| **Functional** | `fn` | Pure, immutable, expression-based |
| **Procedural** | `pn` | Mutable state, side effects, imperative |

### Quick Comparison

```lambda
// Pure function — no side effects
fn double(x: int) => x * 2

// Procedural function — can have side effects
pn save_result(data) {
    data |> "/tmp/output.json"
}
```

---

## Function Declarations

### Function of Statements

```lambda
// Statements as function body

fn add(a: int, b: int) int {
    a + b
}

fn greet(name: string) string {
    "Hello, "
    name
    "!"
}

fn process(data) {
    let filtered = data where ~ > 0;
    let doubled = filtered | ~ * 2;
    doubled
}
```

### Function of Expression

```lambda
// Single expression body
fn multiply(x: int, y: int) => x * y

fn square(n: int) => n ** 2
```

### Anonymous Functions

```lambda
// Lambda expressions
(x: int) => x * 2

fn (x: int, y: int) { ... }

// With inferred types
(x) => x * 2
```


---

## Function Parameters

### Required Parameters

Parameters with type annotations are required:

```lambda
fn greet(name: string) => "Hello, " ++ name
fn add(a: int, b: int) => a + b

add(5, 3)       // 8
add(5)          // Error: missing required parameter
```

### Optional Parameters

Use `?` before the type to make a parameter optional:

```lambda
fn greet(name: string, title?: string) => {
    if (title) title ++ " " ++ name
    else name
}

greet("Alice")           // "Alice"
greet("Alice", "Dr.")    // "Dr. Alice"
```

**Note**: `a?: T` means the parameter is optional (may be `null`). This is different from `a: T?` where the type is nullable but the parameter is required.

### Default Parameter Values

Parameters can have default values:

```lambda
fn greet(name = "World") => "Hello, " ++ name ++ "!"
fn power(base: int, exp: int = 2) => base ** exp

greet()              // "Hello, World!"
greet("Lambda")      // "Hello, Lambda!"
power(3)             // 9 (3**2)
power(2, 10)         // 1024 (2**10)
```

Default expressions can reference earlier parameters:

```lambda
fn make_rect(width: int, height = width) => {
    width: width, height: height
}

make_rect(10)        // {width: 10, height: 10}
make_rect(10, 20)    // {width: 10, height: 20}
```

### Named Arguments

Arguments can be passed by name in any order:

```lambda
fn create_user(name: string, age: int, active: bool = true) => {
    name: name, age: age, active: active
}

// All equivalent:
create_user("Alice", 30, true)
create_user(name: "Alice", age: 30, active: true)
create_user(age: 30, name: "Alice")  // Order independent

// Skip optional parameters
create_user("Bob", 25)               // active defaults to true
```

**Rules**:
- Positional arguments must come before named arguments
- Named arguments can appear in any order
- Cannot provide the same argument both positionally and by name

### Variadic Parameters

Use `...` to accept any number of additional arguments:

```lambda
fn sum_all(...) => sum(varg())
fn printf(fmt: string, ...) => format(fmt, varg())

sum_all(1, 2, 3, 4, 5)       // 15
sum_all()                     // 0
printf("%s is %d", "x", 42)  // "x is 42"
```

Access variadic arguments with `varg()`:

| Call | Returns |
|------|---------|
| `varg()` | List of all variadic arguments |
| `varg(n)` | The nth variadic argument (0-indexed) |

```lambda
fn first_or_default(default, ...) => {
    if (len(varg()) > 0) varg(0)
    else default
}

first_or_default(0, 1, 2, 3)   // 1
first_or_default(0)             // 0
```

### Parameter Order

Parameters must be declared in this order:

```
required → optional (?) → defaults → variadic (...)
```

```lambda
// Valid
fn valid(req: int, opt?: int, def: int = 10, ...) => ...

// Invalid
fn invalid(opt?: int, req: int) => ...  // Error
```

### Parameter Mismatch Handling

| Situation | Behavior |
|-----------|----------|
| Missing required argument | Compile-time error |
| Missing optional argument | Filled with `null` |
| Missing default argument | Evaluates default expression |
| Extra arguments (no variadic) | Warning, discarded |

---

## Function Calls

### Basic Calls

```lambda
add(5, 3)
greet("Alice")
calculate(2.5, 3.0, "add")
```

### Chained Calls

```lambda
// Direct chaining
process(filter(sort(data)))

// Method-style chaining (preferred)
data.sort().filter(x => x > 0).process()
```

### Partial Application

```lambda
// Create specialized functions
fn add(a: int, b: int) => a + b

let add5 = (x) => add(5, x)
add5(3)   // 8
```

### Method-Style Calls

System functions can be called using method syntax:

```lambda
// Traditional prefix style
len(arr)
sum([1, 2, 3])
slice("hello", 0, 3)

// Method style (equivalent)
arr.len()
[1, 2, 3].sum()
"hello".slice(0, 3)

// Array operations
[3, 1, 4, 1, 5].sort()          // [1, 1, 3, 4, 5]
[3, 1, 4, 1, 5].sum()           // 14
[3, 1, 4, 1, 5].unique()        // [3, 1, 4, 5]

// Chained
[5, 3, 1, 4, 2].sort().reverse()  // [5, 4, 3, 2, 1]

// Type conversion
42.string()                      // "42"
"123".int()                      // 123
3.14.floor()                     // 3
```

#### Supported Functions

| Category | Prefix Style | Method Style |
|----------|--------------|--------------|
| **Type** | `len(arr)` | `arr.len()` |
| **Type** | `type(val)` | `val.type()` |
| **Type** | `string(42)` | `42.string()` |
| **String** | `slice(s, 0, 5)` | `s.slice(0, 5)` |
| **String** | `contains(s, "x")` | `s.contains("x")` |
| **Collection** | `reverse(arr)` | `arr.reverse()` |
| **Collection** | `sort(arr)` | `arr.sort()` |
| **Collection** | `take(arr, 3)` | `arr.take(3)` |
| **Stats** | `sum(nums)` | `nums.sum()` |
| **Stats** | `avg(nums)` | `nums.avg()` |
| **Math** | `abs(x)` | `x.abs()` |
| **Math** | `sqrt(x)` | `x.sqrt()` |
#### Method Chaining

Method syntax enables fluent operations:

```lambda
// Chained method calls
let result = data
    .filter(x => x > 0)
    .map(x => x * 2)
    .sort()
    .take(10)
    .sum()

// Equivalent nested calls (harder to read)
let result = sum(take(sort(map(filter(data, x => x > 0), x => x * 2)), 10))
```

---

## Closures

Closures are functions that capture variables from their enclosing scope.

### Basic Closure

```lambda
let multiplier = 3
let triple = (x) => x * multiplier
triple(5)  // 15
```

### Capturing Multiple Variables

```lambda
fn make_linear(slope: int, intercept: int) {
    fn eval(x) => slope * x + intercept
    eval
}

let line = make_linear(2, 10)
line(5)    // 20 (2*5 + 10)
line(10)   // 30 (2*10 + 10)
```

### Nested Closures

```lambda
fn level1(a) {
    fn level2(b) {
        fn level3(c) {
            fn level4(d) => a + b + c + d
            level4
        }
        level3
    }
    level2
}

level1(1)(2)(3)(4)   // 10
```

### Closures with Let Bindings

```lambda
fn make_counter(start: int) {
    let initial = start * 2
    fn count(step) => initial + step
    count
}

let counter = make_counter(5)
counter(3)   // 13 (5*2 + 3)
```

### Mutable Captures in Procedural Closures

When a closure defined inside a `pn` function assigns to a captured `var`, the captured variable becomes a **writable copy**. Each closure instance gets its own independent mutable copy in its environment — writes persist across calls to that closure but do not affect the outer scope or other closures.

```lambda
pn main() {
    var count = 0
    let counter = fn() {
        count = count + 1    // writes to closure's own copy
        count
    }
    print(counter())   // 1
    print(counter())   // 2
    print(count)       // 0 — outer variable unchanged
}
```

```lambda
pn main() {
    var x = 0
    let inc1 = fn() { x = x + 1; x }
    let inc10 = fn() { x = x + 10; x }
    print(inc1())    // 1  — inc1's copy: 0 → 1
    print(inc10())   // 10 — inc10's copy: 0 → 10 (independent)
}
```

**Capture semantics:**
- Captures are **by value** — each closure gets a snapshot at creation time
- `var` captures are mutable (writable copy); `let` captures are read-only
- Writes inside a closure do **not** propagate back to the outer scope
- Named closures must be called through a variable (`let f = name; f()`) to pass the environment

---

## Higher-Order Functions

Functions that take or return functions.

### Functions as Arguments

```lambda
fn apply(f, x) => f(x)
fn map_array(arr, f) => (for (x in arr) f(x))

apply((x) => x * 2, 5)           // 10
map_array([1, 2, 3], (x) => x ** 2)  // [1, 4, 9]
```

### Functions as Return Values

```lambda
fn compose(f, g) => (x) => g(f(x))

let add1 = (x) => x + 1
let double = (x) => x * 2

let add1_then_double = compose(add1, double)
add1_then_double(5)   // 12 (double(add1(5)))
```

### Common Higher-Order Patterns

```lambda
// Filter
fn filter(arr, pred) => arr where pred(~)

// Map
fn map(arr, f) => arr | f(~)

// Reduce/Fold
fn reduce(arr, init, f) => {
    let result = init
    for (x in arr) {
        result = f(result, x)
    }
    result
}

// Usage
filter([1, 2, 3, 4, 5], (x) => x > 2)      // [3, 4, 5]
map([1, 2, 3], (x) => x * 2)               // [2, 4, 6]
reduce([1, 2, 3, 4], 0, (a, b) => a + b)   // 10
```

---

## Procedural Functions

Procedural functions (`pn`) allow mutable state, side effects, and imperative control flow.

### Declaration

```lambda
pn counter() {
    var x = 0
    while (x < 5) {
        x = x + 1
    }
    x   // Returns 5
}
```

### Key Differences from `fn`

| Feature           | `fn` (Functional) | `pn` (Procedural)               |
| ----------------- | ----------------- | ------------------------------- |
| Mutable variables | No                | Yes (`var`)                     |
| Assignment        | No                | Yes (`x = value`)               |
| While loops       | No                | Yes                             |
| Break/Continue    | No                | Yes                             |
| Early return      | No                | Yes (`return`)                  |
| File output       | No                | Yes (`output()`, `\|>`, `\|>>`) |
| Side effects      | No                | Allowed                         |

### Assignment

Assignment is available in `pn` functions. Only `var` variables can be reassigned — `let` bindings and function parameters are immutable.

#### Variable Assignment

```lambda
pn example() {
    var x = 42
    x = 3.14       // OK: type widening (int → float)
    x = "hello"    // OK: type widening (float → string)

    var y: int = 10
    y = 20         // OK: same type
    y = "oops"     // ERROR E201: type mismatch (int expected)
}
```

Variables declared with `var` (without a type annotation) support **type widening** — the variable's runtime type changes automatically to accommodate the new value. Variables with explicit type annotations (`var x: int`) enforce the declared type.

#### Array Element Assignment

```lambda
pn example() {
    let arr = [1, 2, 3]    // typed as int array
    arr[1] = 99            // OK: same type
    arr[0] = 3.14          // OK: auto-converts array to generic
    arr[-1] = "hello"      // OK: negative indexing supported
}
```

When a value of a different type is assigned to a typed array (e.g., float into an int array), the array is automatically converted to a generic array that can hold any type.

#### Map Field Assignment

```lambda
pn example() {
    let obj = {name: "Alice", age: 30}
    obj.age = 31           // OK: same type
    obj.age = "thirty"     // OK: field type changes, shape rebuilt
    obj.name = null        // OK: any type transition
}
```

Map field assignment automatically rebuilds the map's shape metadata when the value type changes, ensuring structural consistency.

#### Element Mutation

```lambda
pn example() {
    let elem = <div class: "main"; "Hello">
    elem.class = "updated"       // attribute assignment
    elem[0] = "Goodbye"          // child assignment by index
}
```

Elements support both attribute mutation (via dot notation) and child mutation (via index).

### Implicit Return

The last expression in a `pn` function is its return value:

```lambda
pn add_one(x: int) {
    x + 1    // Returned
}

pn factorial(n: int) {
    var result = 1
    var i = 1
    while (i <= n) {
        result = result * i
        i = i + 1
    }
    result   // Returned
}
```

### Early Return

```lambda
pn find_first_even(nums: [int]) {
    var i = 0
    while (i < len(nums)) {
        if (nums[i] % 2 == 0) {
            return nums[i]   // Early exit
        }
        i = i + 1
    }
    null   // Not found
}
```

### File Output in Procedural Functions

```lambda
pn save_report(data) {
    // Using pipe output operators
    data |> "/tmp/report.json"
    
    // Append to log
    {event: "saved", time: now()} |>> "/tmp/events.log"
    
    // Using output function
    output(data, "/tmp/backup.json", {atomic: true})
}
```

### Running Procedural Scripts

```bash
./lambda.exe run script.ls    # Executes main() procedure
```

The `main()` procedure serves as the entry point.

---

## System Functions Reference

Lambda provides extensive built-in functions. See [Lambda_Sys_Func.md](Lambda_Sys_Func.md) for complete documentation.

### Function Categories

| Category | Examples | Description |
|----------|----------|-------------|
| **Type** | `type`, `len`, `int`, `float`, `string` | Type conversion and inspection |
| **Math** | `abs`, `round`, `floor`, `ceil`, `sqrt`, `log` | Mathematical operations |
| **Statistical** | `sum`, `avg`, `median`, `variance` | Statistical analysis |
| **Collection** | `slice`, `reverse`, `sort`, `unique`, `concat` | Collection manipulation |
| **String** | `upper`, `lower`, `trim`, `split`, `join` | String operations |
| **I/O** | `input`, `format`, `print`, `output`, `exists` | Input/output |
| **Date/Time** | `datetime`, `date`, `time`, `today`, `now` | Date and time |

---

This document covers Lambda's function system. For built-in function details, see [Lambda_Sys_Func.md](Lambda_Sys_Func.md). For expressions, see [Lambda Expressions](Lambda_Expr_Stam.md).
