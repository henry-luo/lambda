# Lua Feature Comparison & Coroutine Proposal for Lambda

## Overview

This document compares Lambda Script with Lua, identifies features Lua supports that Lambda lacks, and proposes adding coroutines to Lambda for improved data processing capabilities.

---

## Lua Features Not Present in Lambda

### 1. Coroutines (Most Notable)

Lua has built-in cooperative multitasking with `coroutine.create`, `coroutine.yield`, and `coroutine.resume`:

```lua
-- Lua coroutine example
co = coroutine.create(function()
    for i = 1, 3 do
        print("co", i)
        coroutine.yield()
    end
end)
coroutine.resume(co)  -- prints "co 1"
coroutine.resume(co)  -- prints "co 2"
```

This enables generators, async patterns, and state machines elegantly. Lambda has no equivalent.

### 2. Metatables & Metamethods

Lua allows customizing behavior of tables (operator overloading, custom indexing):

```lua
-- Lua metatable example
mt = { __add = function(a, b) return a.value + b.value end }
t1 = setmetatable({value = 10}, mt)
t2 = setmetatable({value = 20}, mt)
print(t1 + t2)  -- 30
```

Lambda has no mechanism for operator overloading on custom types.

### 3. Dynamic Code Execution

Lua can load and execute code at runtime:

```lua
f = load("return 2 + 2")
print(f())  -- 4

dofile("script.lua")  -- execute file
```

Lambda compiles scripts but doesn't expose runtime code generation to users.

### 4. Tail Call Optimization

Lua guarantees tail call optimization, enabling infinite recursion in tail position:

```lua
function factorial(n, acc)
    if n == 0 then return acc end
    return factorial(n - 1, n * acc)  -- guaranteed TCO
end
```

Lambda's reference doesn't mention TCO guarantees.

### 5. Weak References

Lua supports weak tables for caching without preventing garbage collection:

```lua
cache = setmetatable({}, {__mode = "v"})  -- weak values
```

---

## Coroutine Proposal for Lambda

### Why Coroutines Matter for Data Processing

| Capability | Without Coroutines | With Coroutines |
|------------|-------------------|-----------------|
| **Memory** | Load all data upfront | Process item-by-item |
| **Infinite sequences** | Impossible | Natural |
| **Pipeline composition** | Eager, creates intermediates | Lazy, zero intermediates |
| **Early termination** | Wasteful (computes all) | Efficient (stops immediately) |
| **State machines** | Verbose callback soup | Clean sequential code |

For a data processing language like Lambda, coroutines would be transformative—especially for handling large datasets that don't fit in memory.

---

## Use Case Examples

### 1. Generator Pattern - Lazy Infinite Sequences

**Lua implementation:**
```lua
-- Lua: Generate infinite Fibonacci sequence
function fibonacci()
    local a, b = 0, 1
    while true do
        coroutine.yield(a)
        a, b = b, a + b
    end
end

-- Usage: Only compute what you need
fib = coroutine.wrap(fibonacci)
print(fib())  -- 0
print(fib())  -- 1
print(fib())  -- 1
print(fib())  -- 2
-- Memory: O(1) - no infinite list created!
```

**Current Lambda limitation:**
```lambda
// Must either:
// 1. Compute entire list upfront (memory expensive)
let fibs = (for (i in 1 to 1000000) fibonacci(i))  // computes ALL

// 2. Or use explicit index tracking (verbose)
fn fib_at(n: int) => ...  // recomputes from scratch each time
```

---

### 2. Streaming Pipeline - Process Large Files

**Lua implementation:**
```lua
-- Lua: Stream lines from huge file without loading all into memory
function read_lines(filename)
    for line in io.lines(filename) do
        coroutine.yield(line)
    end
end

function filter_lines(source, pattern)
    for line in source do
        if line:match(pattern) then
            coroutine.yield(line)
        end
    end
end

function take(source, n)
    local count = 0
    for item in source do
        if count >= n then return end
        coroutine.yield(item)
        count = count + 1
    end
end

-- Pipeline: Read 10GB file, find first 10 matching lines
-- Memory usage: O(1) - only one line in memory at a time!
pipeline = take(
    filter_lines(
        coroutine.wrap(function() read_lines("huge.log") end),
        "ERROR"
    ),
    10
)

for line in pipeline do
    print(line)
end
```

**Current Lambda limitation:**
```lambda
// Must load entire file, then filter, then slice
let lines = input("huge.log", 'lines')           // loads ALL 10GB!
let errors = (for (l in lines) if (match(l, "ERROR")) l)
let first10 = slice(errors, 0, 10)
```

---

### 3. Lazy Evaluation - Compute On-Demand

**Lua implementation:**
```lua
-- Lua: Expensive computations only when needed
function expensive_sequence()
    local i = 0
    while true do
        i = i + 1
        -- Simulate expensive computation
        local result = complex_calculation(i)
        coroutine.yield(result)
    end
end

-- Only computes 3 values, not infinite!
gen = coroutine.wrap(expensive_sequence)
local first = gen()   -- computes #1
local second = gen()  -- computes #2
local third = gen()   -- computes #3
-- Stop here - #4, #5, ... never computed
```

---

### 4. State Machines - Clean Protocol Implementation

**Lua implementation:**
```lua
-- Lua: HTTP request parser as state machine
function http_parser()
    -- State 1: Parse request line
    local request_line = coroutine.yield("WANT_LINE")
    local method, path = request_line:match("(%w+) (%S+)")
    
    -- State 2: Parse headers
    local headers = {}
    while true do
        local line = coroutine.yield("WANT_LINE")
        if line == "" then break end
        local key, value = line:match("([^:]+): (.+)")
        headers[key] = value
    end
    
    -- State 3: Parse body if Content-Length
    local body = nil
    if headers["Content-Length"] then
        body = coroutine.yield("WANT_BYTES", tonumber(headers["Content-Length"]))
    end
    
    return {method = method, path = path, headers = headers, body = body}
end
```

---

### 5. Producer-Consumer - Decoupled Processing

**Lua implementation:**
```lua
-- Lua: Decouple data production from consumption
function json_objects(filename)
    -- Yields parsed objects one at a time
    local buffer = ""
    for chunk in read_chunks(filename, 4096) do
        buffer = buffer .. chunk
        while true do
            local obj, rest = try_parse_json(buffer)
            if obj then
                coroutine.yield(obj)
                buffer = rest
            else
                break
            end
        end
    end
end

-- Consumer doesn't know/care about chunking, buffering, parsing
for obj in coroutine.wrap(json_objects)("data.jsonl") do
    process(obj)
end
```

---

## Proposed Lambda Syntax

### Generator Functions

Introduce `gen fn` for generator functions and `yield` keyword:

```lambda
// Generator function with yield
gen fn fibonacci() {
    var a = 0, b = 1
    while (true) {
        yield a
        (a, b) = (b, a + b)
    }
}

// Create generator instance
let fib = fibonacci()
fib()  // 0
fib()  // 1
fib()  // 1
fib()  // 2
```

### Lazy Pipeline Combinators

```lambda
// Filter generator
gen fn filter(source, predicate) {
    for (item in source) {
        if (predicate(item)) yield item
    }
}

// Take generator
gen fn take(source, n) {
    var count = 0
    for (item in source) {
        if (count >= n) return
        yield item
        count = count + 1
    }
}

// Map generator
gen fn map(source, transform) {
    for (item in source) {
        yield transform(item)
    }
}
```

### Complete Example: Process Large Log File

```lambda
// Generator for reading lines lazily
gen fn lines(filename: string) {
    // Implementation would use lazy file reading
    for (line in lazy_read_lines(filename)) {
        yield line
    }
}

// Usage: Process 10GB log, find first 10 errors - O(1) memory!
let errors = take(
    filter(lines("huge.log"), (l) => contains(l, "ERROR")),
    10
)

for (line in errors) {
    print(line)
}
```

### Infinite Sequences

```lambda
// Natural numbers (infinite)
gen fn naturals(start: int = 0) {
    var n = start
    while (true) {
        yield n
        n = n + 1
    }
}

// Prime numbers (infinite)
gen fn primes() {
    var n = 2
    while (true) {
        if (is_prime(n)) yield n
        n = n + 1
    }
}

// First 100 primes - only computes what's needed
let first_100_primes = take(primes(), 100)
```

---

## Implementation Considerations

### Runtime Support

1. **Coroutine State**: Each generator needs its own execution context (stack, locals)
2. **Yield Points**: Compiler must track yield locations for resumption
3. **Memory**: Generator state must be heap-allocated and reference-counted

### Integration with For Loops

Generators should work seamlessly with existing `for` expressions:

```lambda
// Generators should be iterable
gen fn countdown(n: int) {
    while (n > 0) {
        yield n
        n = n - 1
    }
}

// Works with for expressions
let doubled = (for (x in countdown(5)) x * 2)  // [10, 8, 6, 4, 2]

// Works with for statements
for (x in countdown(5)) {
    print(x)
}
```

### Type System

Generator functions should have a distinct type:

```lambda
// Generator type syntax
type IntGenerator = gen () => int
type FilteredGenerator = gen (source: gen () => T, pred: (T) => bool) => T

// Type annotation
let fib: gen () => int = fibonacci()
```

---

## Alternative Approaches

### 1. Iterator Protocol (Python-style)

Instead of coroutines, define an iterator interface:

```lambda
type Iterator<T> = {
    next: () => T | null,
    has_next: () => bool
}

fn make_fibonacci() -> Iterator<int> {
    var a = 0, b = 1
    {
        next: () => {
            let result = a
            (a, b) = (b, a + b)
            result
        },
        has_next: () => true
    }
}
```

**Pros**: Simpler to implement
**Cons**: Verbose, harder to compose

### 2. Lazy Lists (Haskell-style)

Use thunks for lazy evaluation:

```lambda
type LazyList<T> = null | (T, () => LazyList<T>)

fn lazy_range(start: int) -> LazyList<int> {
    (start, () => lazy_range(start + 1))
}
```

**Pros**: Pure functional
**Cons**: More memory overhead, awkward syntax

See [Lazy Lists Deep Dive](#lazy-lists-deep-dive) section below for detailed exploration.

### 3. Async/Await (JavaScript-style)

Focus on async I/O rather than general coroutines:

```lambda
async fn fetch_data(url: string) {
    let response = await fetch(url)
    await response.json()
}
```

**Pros**: Familiar to web developers
**Cons**: Limited to async use cases

---

## Lazy Lists Deep Dive

### Languages with Lazy Lists / Lazy Evaluation

#### 1. Haskell - Lazy by Default (Gold Standard)

Everything is lazy unless forced:

```haskell
-- Infinite list - no special syntax needed!
naturals = [0..]                    -- [0, 1, 2, 3, ...]
fibs = 0 : 1 : zipWith (+) fibs (tail fibs)

-- Just use it - laziness is automatic
take 10 fibs                        -- [0,1,1,2,3,5,8,13,21,34]
take 5 [x^2 | x <- [1..], even x]   -- [4, 16, 36, 64, 100]
```

#### 2. Scala - `LazyList` (formerly `Stream`)

```scala
// #:: is the lazy cons operator
def naturals(n: Int): LazyList[Int] = n #:: naturals(n + 1)

// Or use LazyList.from
val nums = LazyList.from(0)
val fibs: LazyList[Int] = 0 #:: 1 #:: fibs.zip(fibs.tail).map(_ + _)

nums.take(5).toList    // List(0, 1, 2, 3, 4)
```

#### 3. Clojure - Lazy Sequences

```clojure
;; lazy-seq macro
(defn naturals [n]
  (lazy-seq (cons n (naturals (inc n)))))

;; Built-in lazy functions
(take 5 (range))              ; (0 1 2 3 4)
(take 5 (iterate inc 0))      ; (0 1 2 3 4)
(take 10 (filter even? (range)))  ; (0 2 4 6 8 10 12 14 16 18)
```

#### 4. F# - `seq` and `lazy`

```fsharp
// Sequence expressions (lazy)
let naturals = Seq.initInfinite id
let evens = seq { for x in 0 .. System.Int32.MaxValue do if x % 2 = 0 then yield x }

// Explicit lazy values
let lazyValue = lazy (expensiveComputation())
lazyValue.Force()  // evaluate when needed
```

#### 5. Kotlin - `Sequence`

```kotlin
// sequence builder with yield
val naturals = sequence {
    var n = 0
    while (true) {
        yield(n++)
    }
}

val fibs = sequence {
    var a = 0
    var b = 1
    while (true) {
        yield(a)
        a = b.also { b += a }
    }
}

naturals.take(5).toList()  // [0, 1, 2, 3, 4]
```

#### 6. Rust - `Iterator` trait

```rust
// Iterator adapters are lazy
let squares = (0..).map(|x| x * x);
let first_5: Vec<_> = squares.take(5).collect();  // [0, 1, 4, 9, 16]

// Custom iterator
fn fibs() -> impl Iterator<Item = u64> {
    std::iter::successors(Some((0, 1)), |&(a, b)| Some((b, a + b)))
        .map(|(a, _)| a)
}
```

#### 7. OCaml - `Seq` module (4.07+)

```ocaml
(* Seq.t is a lazy sequence type *)
let rec naturals n () = Seq.Cons (n, naturals (n + 1))

let take n seq = Seq.take n seq |> List.of_seq
let _ = take 5 (naturals 0)  (* [0; 1; 2; 3; 4] *)
```

---

### Lazy List Core Concept

A lazy list uses **thunks** (suspended computations) to defer evaluation until needed:

```lambda
// A lazy list is either:
// - null (empty)
// - A pair of (head_value, thunk_that_produces_tail)

type LazyList<T> = null | {
    head: T,
    tail: () => LazyList<T>   // thunk - not evaluated until called
}
```

#### Building Lazy Lists

```lambda
// Infinite sequence of natural numbers
fn naturals(n: int) -> LazyList<int> {
    {
        head: n,
        tail: () => naturals(n + 1)   // NOT called yet - just stored
    }
}

let nums = naturals(0)
// nums = { head: 0, tail: <thunk> }
// The thunk hasn't run - we haven't computed 1, 2, 3... yet!
```

#### Consuming Lazy Lists

```lambda
// Take first n elements (forces evaluation)
fn take(n: int, lst: LazyList<T>) -> [T] {
    if (n <= 0 or lst == null) []
    else [lst.head] + take(n - 1, lst.tail())  // tail() forces the thunk
}

take(5, naturals(0))   // [0, 1, 2, 3, 4]
// Only computed 5 values, not infinite!
```

#### Lazy Combinators

```lambda
// Lazy map - doesn't compute anything until consumed
fn lazy_map(f, lst: LazyList<T>) -> LazyList<U> {
    if (lst == null) null
    else {
        head: f(lst.head),
        tail: () => lazy_map(f, lst.tail())   // deferred!
    }
}

// Lazy filter
fn lazy_filter(pred, lst: LazyList<T>) -> LazyList<T> {
    if (lst == null) null
    else if (pred(lst.head)) {
        head: lst.head,
        tail: () => lazy_filter(pred, lst.tail())
    }
    else lazy_filter(pred, lst.tail())  // skip, try next
}

// Lazy zip
fn lazy_zip(a: LazyList<T>, b: LazyList<U>) -> LazyList<(T, U)> {
    if (a == null or b == null) null
    else {
        head: (a.head, b.head),
        tail: () => lazy_zip(a.tail(), b.tail())
    }
}
```

#### Complete Example: Fibonacci

```lambda
// Infinite Fibonacci sequence - O(1) to define!
fn fibs() -> LazyList<int> {
    fn go(a: int, b: int) -> LazyList<int> {
        {
            head: a,
            tail: () => go(b, a + b)
        }
    }
    go(0, 1)
}

take(10, fibs())   // [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]
```

#### Pipeline Example

```lambda
// Find first 5 even squares greater than 100
let result = take(5,
    lazy_filter((x) => x > 100,
        lazy_filter((x) => x % 2 == 0,
            lazy_map((x) => x * x,
                naturals(1)))))

// result: [144, 196, 256, 324, 400]
// Only computed as many squares as needed to find 5 matches!
```

#### Sieve of Eratosthenes (Classic Example)

```lambda
// Infinite prime numbers via lazy sieve
fn sieve(lst: LazyList<int>) -> LazyList<int> {
    if (lst == null) null
    else {
        let p = lst.head
        {
            head: p,
            tail: () => sieve(
                lazy_filter((x) => x % p != 0, lst.tail())
            )
        }
    }
}

fn primes() => sieve(naturals(2))

take(10, primes())   // [2, 3, 5, 7, 11, 13, 17, 19, 23, 29]
```

---

### Comparison: Lazy Lists vs Coroutines

| Aspect | Lazy Lists | Coroutines |
|--------|-----------|------------|
| **Syntax** | Verbose (manual thunks) | Clean (`yield`) |
| **Mental model** | Recursive data structure | Imperative control flow |
| **State** | Encoded in closures | Explicit variables |
| **Memory** | Closure per element | Single stack frame |
| **Composability** | Excellent | Excellent |
| **Familiarity** | FP programmers | Imperative programmers |
| **Runtime support** | None (just closures) | Requires coroutine runtime |

#### Same Fibonacci, Both Styles

**Lazy List:**
```lambda
fn fibs() {
    fn go(a, b) => { head: a, tail: () => go(b, a + b) }
    go(0, 1)
}
```

**Coroutine:**
```lambda
gen fn fibs() {
    var a = 0, b = 1
    while (true) {
        yield a
        (a, b) = (b, a + b)
    }
}
```

The coroutine version is more readable for most programmers, but lazy lists are more "purely functional" and don't require runtime coroutine support.

---

### Proposed Lazy Syntax Options for Lambda

#### Option A: Haskell-style List Comprehension with `..`

```lambda
// Infinite range
let naturals = [0..]           // [0, 1, 2, 3, ...]
let evens = [0, 2..]           // [0, 2, 4, 6, ...] (step inferred)
let countdown = [10, 9..0]     // [10, 9, 8, ..., 0]

// Lazy comprehension with ..
let squares = [x^2 for x in 0..]
let primes = [x for x in 2.. if is_prime(x)]

// Usage
take(5, squares)               // [0, 1, 4, 9, 16]
```

**Pros**: Very concise, familiar to Haskell/Python users
**Cons**: `..` might conflict with spread operator

#### Option B: Kotlin/Python-style `yield` in Comprehensions

```lambda
// Generator expression (inherently lazy)
let naturals = (yield n for n in 0..)
let fibs = (yield a for (a, b) in iterate((0, 1), (a, b) => (b, a+b)))

// Or simpler: any for-expression over infinite range is lazy
let squares = (for x in 0.. yield x^2)
```

#### Option C: `lazy` Keyword Modifier

```lambda
// Explicit lazy modifier
let naturals = lazy [0, 1, 2, ...]     // or lazy range(0)
let expensive = lazy compute_big_data()

// Lazy comprehension
let squares = lazy (for x in range() x^2)

// Force evaluation
force(take(5, squares))                // [0, 1, 4, 9, 16]
```

**Pros**: Explicit, clear when laziness applies
**Cons**: More verbose

#### Option D: Stream Type with `~>` Operator (Novel)

```lambda
// Stream literal with ~> (lazy cons)
let naturals = 0 ~> (n => n + 1)       // seed ~> step function
let fibs = (0, 1) ~> ((a, b) => (b, a + b)) |> map(fst)

// Stream comprehension
let squares = [| x^2 for x in 0.. |]   // [| |] = lazy stream

// Stream combinators
naturals 
    |> filter(x => x % 2 == 0)
    |> map(x => x^2)
    |> take(5)                         // [0, 4, 16, 36, 64]
```

#### Option E: F#/Scala-style `seq` Block

```lambda
// seq block creates lazy sequence
let naturals = seq {
    var n = 0
    while (true) {
        yield n
        n = n + 1
    }
}

// Concise seq expression
let squares = seq { for x in 0.. => x^2 }
let evens = seq { for x in 0.. if x % 2 == 0 => x }

// Nested yields
let flatten = seq {
    for list in nested_lists {
        for item in list {
            yield item
        }
    }
}
```

**Pros**: Clear block syntax, supports complex logic
**Cons**: New keyword, block-based

#### Option F: `*` Suffix for Lazy Iteration (Minimal)

```lambda
// * suffix makes iteration lazy
let squares = (for x in range()* x^2)     // lazy
let eager = (for x in range(10) x^2)      // eager (finite)

// Or on the result
let lazy_squares = (for x in range() x^2)*
```

---

### Recommended Lazy Syntax for Lambda

Given Lambda's functional nature and existing syntax, combining **Options A + E** is recommended:

```lambda
// 1. Infinite ranges with ..
let naturals = 0..                    // infinite from 0
let evens = 0, 2..                    // 0, 2, 4, 6, ...

// 2. Lazy for-expressions over infinite ranges (automatic)
let squares = (for x in 0.. x^2)      // lazy because 0.. is infinite
let primes = (for x in 2.. if is_prime(x) x)

// 3. seq block for complex generators
let fibs = seq {
    var a = 0, b = 1
    while (true) {
        yield a
        (a, b) = (b, a + b)
    }
}

// 4. Lazy combinators (standard library)
0.. |> filter(even) |> map(square) |> take(5)  // [0, 4, 16, 36, 64]
```

#### Why This Combination?

| Feature | Benefit |
|---------|---------|
| `0..` infinite range | Minimal syntax change, intuitive |
| Auto-lazy over infinite | No explicit `lazy` keyword needed |
| `seq { yield }` block | Handles complex state machines |
| Combinator pipeline | Composable, readable |

This gives Lambda Haskell's elegance with Kotlin's practicality, without requiring heavy runtime changes (lazy lists can be implemented as closures).

---

### Hybrid Approach: Syntax Sugar over Lazy Lists

Lambda could support **generator syntax** that compiles to lazy lists internally:

```lambda
// User writes generator syntax
gen fn naturals(start: int) {
    var n = start
    while (true) {
        yield n
        n = n + 1
    }
}

// Compiler transforms to lazy list internally (no special runtime needed!)
fn naturals(start: int) -> LazyList<int> {
    {
        head: start,
        tail: () => naturals(start + 1)
    }
}
```

This provides clean syntax while keeping a simple functional implementation—the best of both worlds.

---

## Recommendation

**Coroutines with `gen fn` and `yield`** are recommended because they:

1. **Maximize expressiveness**: Handle generators, pipelines, state machines, and lazy evaluation
2. **Minimize memory**: O(1) memory for streaming large datasets
3. **Compose naturally**: Work with existing `for` expressions
4. **Align with Lambda's goals**: Data processing often involves large datasets

### Priority Implementation Order

1. **Phase 1**: Basic generators (`gen fn`, `yield`, iteration support)
2. **Phase 2**: Built-in lazy combinators (`filter`, `map`, `take`, `drop`, `zip`)
3. **Phase 3**: Lazy file I/O (`lines()`, `chunks()`)
4. **Phase 4**: Bidirectional coroutines (send values back to generator)

---

## Coroutines vs Async/Await

### Core Difference

| Aspect | Coroutines | Async/Await |
|--------|-----------|-------------|
| **Primary Purpose** | General control flow suspension | I/O-bound concurrency |
| **Scheduling** | Cooperative, manual (`resume`) | Event loop / runtime managed |
| **Use Cases** | Generators, state machines, iterators | Network requests, file I/O, timers |
| **Blocking** | Suspends to caller | Suspends to event loop |
| **Concurrency** | Sequential by default | Concurrent by design |

---

### Mental Model

**Coroutines** = "Pausable functions" (you control when to resume)
```
caller <--yield/resume--> coroutine
```

**Async/Await** = "Functions that wait for external events" (runtime controls resume)
```
async fn <--await--> event loop <--events--> I/O (network, disk, timer)
```

---

### Code Comparison

#### Coroutines (Lua-style)

```lua
-- Producer: yields values on demand
function numbers()
    for i = 1, 3 do
        coroutine.yield(i)  -- pause, return to caller
    end
end

-- Consumer: manually resumes
co = coroutine.wrap(numbers)
print(co())  -- 1 (you decide when to call)
print(co())  -- 2
print(co())  -- 3
```

**Key**: Caller explicitly controls execution with `resume()`.

#### Async/Await (JavaScript-style)

```javascript
// Async function: awaits external events
async function fetchData() {
    const resp = await fetch(url);  // pause until network responds
    const data = await resp.json(); // pause until parsing done
    return data;
}

// Caller: doesn't manually resume - event loop does
fetchData().then(data => console.log(data));
// Event loop resumes fetchData when I/O completes
```

**Key**: Runtime/event loop controls execution based on I/O events.

---

### When to Use Which

| Scenario | Best Choice | Why |
|----------|-------------|-----|
| Lazy iteration / generators | Coroutines | Control when to produce next value |
| State machines | Coroutines | Explicit state transitions |
| Network requests | Async/Await | I/O-driven, runtime manages timing |
| Parallel HTTP calls | Async/Await | `Promise.all()` / concurrent execution |
| Streaming data processing | Coroutines | Pull-based, memory efficient |
| Database queries | Async/Await | I/O-bound, non-blocking |
| Game loop / simulation | Coroutines | Frame-by-frame control |
| UI responsiveness | Async/Await | Don't block main thread |

---

### Relationship: Async/Await IS Built on Coroutines

Many languages implement async/await **on top of** coroutines:

```
┌─────────────────────────────────────────┐
│           Async/Await Syntax            │  ← Sugar
├─────────────────────────────────────────┤
│         Promise / Future / Task         │  ← Abstraction
├─────────────────────────────────────────┤
│       Event Loop / Scheduler            │  ← Runtime
├─────────────────────────────────────────┤
│            Coroutines                   │  ← Foundation
└─────────────────────────────────────────┘
```

**Examples:**
- **Python**: `async/await` compiles to generator-based coroutines
- **Kotlin**: `suspend` functions are coroutines + dispatcher
- **C#**: `async/await` uses state machine (compiler-generated coroutine)
- **Rust**: `async fn` returns `Future` (lazy coroutine + executor)

---

### Detailed Comparison

#### 1. Suspension Point Semantics

```python
# Coroutine: yield returns value TO caller
def gen():
    yield 1      # returns 1 to whoever called next()
    yield 2

# Async: await waits FOR external result
async def fetch():
    data = await http_get(url)  # waits for network
    return data
```

#### 2. Bidirectional Communication

```python
# Coroutines: can send values IN
def accumulator():
    total = 0
    while True:
        x = yield total    # receive value, yield result
        total += x

acc = accumulator()
next(acc)       # start
acc.send(10)    # 10
acc.send(20)    # 30

# Async: typically one-way (await returns result)
result = await compute()  # can't send mid-execution
```

#### 3. Concurrency Model

```javascript
// Async: natural concurrency
async function parallel() {
    // Both requests start immediately, run concurrently
    const [a, b] = await Promise.all([
        fetch('/api/a'),
        fetch('/api/b')
    ]);
}

// Coroutines: sequential by default
function* sequential() {
    const a = yield fetch('/api/a');  // waits
    const b = yield fetch('/api/b');  // then waits
}
```

#### 4. Error Handling

```javascript
// Async: try/catch works naturally
async function safe() {
    try {
        await riskyOperation();
    } catch (e) {
        console.error(e);
    }
}

// Coroutines: errors can be thrown into them
function* gen() {
    try {
        yield 1;
    } catch (e) {
        console.error(e);
    }
}
const g = gen();
g.next();
g.throw(new Error("injected"));  // throw error into coroutine
```

---

### Summary Table

| Feature | Coroutines | Async/Await |
|---------|-----------|-------------|
| **Control** | Manual (caller resumes) | Automatic (event loop) |
| **Purpose** | General suspension | I/O concurrency |
| **Values** | Yield multiple values | Return single result |
| **Direction** | Bidirectional (send/yield) | Unidirectional (await result) |
| **Concurrency** | Opt-in | Built-in (`Promise.all`) |
| **Memory** | O(1) for streaming | O(n) pending promises |
| **Best for** | Generators, iterators, state machines | Network, files, timers |
| **Runtime** | Minimal | Requires event loop |

---

### For Lambda: Why Both Could Be Valuable

**Coroutines (`gen fn` + `yield`)** for:
- Lazy data processing pipelines
- Infinite sequences
- Memory-efficient streaming
- State machines in parsers

**Async/Await** for:
- HTTP requests (`fetch`)
- File I/O
- Database queries
- Parallel operations

```lambda
// Coroutine: lazy data pipeline
gen fn process_logs(filename: string) {
    for (line in lines(filename)) {
        if (contains(line, "ERROR")) {
            yield parse_error(line)
        }
    }
}

// Async: concurrent HTTP
async fn fetch_all(urls: [string]) {
    await parallel(for (url in urls) fetch(url))
}
```

---

## References

- [Lua 5.4 Reference Manual - Coroutines](https://www.lua.org/manual/5.4/manual.html#2.6)
- [Python PEP 255 - Simple Generators](https://peps.python.org/pep-0255/)
- [JavaScript Generators](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Generator)
- [Kotlin Sequences](https://kotlinlang.org/docs/sequences.html)
