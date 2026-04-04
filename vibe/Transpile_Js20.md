# JavaScript Transpiler v20: Test262 Compliance Improvement Proposal

## 1. Executive Summary

LambdaJS currently passes **12,036 of 27,030 executed tests** (44.5%) in the test262 suite (38,649 total, 11,619 skipped due to unsupported features). This proposal identifies the **root causes** of the remaining failures, groups them into prioritized phases, and estimates the recovery potential for each.

The key finding is that a small number of systemic issues cascade across thousands of tests. Fixing **5 root causes** could recover an estimated **~5,500 tests**, raising the pass rate to ~61%.

### Original Baseline (v18)

| Metric | Value |
|--------|-------|
| Total test262 tests | 38,649 |
| Skipped (unsupported features) | 11,619 |
| Executed | 27,030 |
| Passed | 10,982 (40.6%) |
| Failed | 16,048 |

### Current Progress (v20 in-progress)

| Metric | Value |
|--------|-------|
| Executed | 27,030 |
| Passed | **12,036 (44.5%)** |
| Failed | 14,994 |
| Net gain from v18 | **+1,054 tests** |

### Target (v20)

| Metric | Value |
|--------|-------|
| Executed | 27,030 |
| Passed (projected) | ~16,500 (61%) |
| Net gain from v18 | ~5,500 tests |

## 1.1 Implementation Progress

### Milestone Progression

| Milestone | Passing | Pass Rate | Net Gain | Phases Completed |
|-----------|--------:|----------:|---------:|------------------|
| v18 baseline | 10,982 | 40.6% | — | — |
| After Phase 1a, 1d, 2, 4, 5, 7d | 11,231 | 41.6% | +249 | 1a, 1d, 2, 4, 5, 7d |
| After Phase 1b, 7c | 11,439 | 42.3% | +457 | +1b, 7c |
| After Phase 3 (destructuring) | 11,969 | 44.3% | +987 | +3 |
| After callback validation fix | **12,036** | **44.5%** | **+1,054** | +callback validation |

### Phase Completion Status

| Phase | Description | Status | Tests Recovered |
|:-----:|-------------|:------:|:---------------:|
| 1a | Built-in method descriptors (non-enumerable markers) | ✅ Done | ~100 |
| 1b | Class prototype chain (methods on prototype, not instance) | ✅ Done | ~80 |
| 1c | getOwnPropertyDescriptor accuracy | ⬜ Not started | — |
| 1d | Property enumeration order (integer indices first) | ✅ Done | ~60 |
| 1e | for-in enumerable flag enforcement | ⬜ Not started | — |
| 2 | Date object completion (setters, statics, utilities) | ✅ Done | ~250 |
| 3 | Destructuring completion (recursive helpers, nested, computed, for-of, rest) | ✅ Done | ~530 |
| 4 | JSON full parameters (replacer, space, reviver) | ✅ Done | ~80 |
| 5 | encodeURI / decodeURI | ✅ Done | ~60 |
| 6 | Generator/iterator protocol | ⬜ Not started | — |
| 7a | arguments aliasing | ⬜ Not started | — |
| 7b | TDZ enforcement for let/const | ⬜ Not started | — |
| 7c | Error object properties (toString, message defaults) | ✅ Done | ~40 |
| 7d | Number/Math gaps (static properties, missing functions) | ✅ Done | ~60 |
| 7e | try/catch improvements | ⬜ Not started | — |
| 7f | Tagged template literals | ⬜ Not started | — |
| 7g | new expression semantics | ⬜ Not started | — |
| — | Array callback validation (TypeError for non-callable) | ✅ Done | ~69 |

### Key Implementation Details

**Phase 3 — Destructuring Refactoring**: Created unified recursive destructuring helpers (`jm_emit_array_destructure`, `jm_emit_object_destructure`, `jm_emit_destructure_target`, `jm_emit_destructure_default`, `jm_bind_destructure_var`) that handle all destructuring contexts: variable declarations, assignments, function parameters, and for-of bindings. Fixed computed property key support in `build_js_ast.cpp` (`pair_pattern` handler).

**Phase 1b — Class Prototype Chain**: Fixed `jm_transpile_new_expr()` to assign methods to the prototype instead of copying them to each instance. Instances now properly inherit from the prototype chain.

**Callback Validation**: Added `js_throw_not_callable()` helper. Updated all array higher-order methods (`filter`, `reduce`, `forEach`, `find`, `findIndex`, `some`, `every`, `map`, `reduceRight`, `sort`) to throw `TypeError` for non-callable callbacks. Also fixed `reduce`/`reduceRight` to throw `TypeError` on empty array with no initial value.

### Remaining High-Failure Categories (Non-Class, Non-Temporal)

| Category | Failed Tests | Notes |
|----------|:-----------:|-------|
| RegExp property-escapes | ~350 | Unicode property escape support needed |
| dynamic-import syntax | ~503 | `import()` expression not supported |
| Array named properties | ~500+ | `LMD_TYPE_ARRAY` lacks named property storage |
| Object.prototype methods | ~69 | Various missing/incorrect behaviors |
| Function.prototype.call | ~44 | Edge cases in call/apply |
| Array.prototype.splice | ~46 | Edge case handling |
| Number.prototype.toString | ~46 | Radix conversion issues |

## 2. Failure Analysis

### 2.1 Top Failure Categories (by failed test count)

| # | Category | Total | Passed | Failed | Skipped | Pass Rate | Executable |
|---|----------|------:|-------:|-------:|--------:|----------:|-----------:|
| 1 | statements/class | 4,367 | 976 | 1,936 | 1,455 | 33.5% | 2,912 |
| 2 | Object (built-in) | 3,411 | 1,359 | 1,863 | 189 | 42.2% | 3,222 |
| 3 | expressions/class | 4,059 | 875 | 1,741 | 1,443 | 33.4% | 2,616 |
| 4 | Array (built-in) | 3,081 | 1,260 | 1,346 | 475 | 48.3% | 2,606 |
| 5 | TypedArray | 1,438 | 0 | 886 | 552 | 0% | 886 |
| 6 | statements/for | 2,485 | 479 | 612 | 1,394 | 43.9% | 1,091 |
| 7 | String (built-in) | 1,223 | 435 | 595 | 193 | 42.2% | 1,030 |
| 8 | expressions/object | 1,170 | 242 | 548 | 380 | 30.6% | 790 |
| 9 | RegExp (built-in) | 1,879 | 126 | 498 | 1,255 | 20.2% | 624 |
| 10 | TypedArrayCtors | 736 | 9 | 447 | 280 | 2.0% | 456 |
| 11 | Date (built-in) | 594 | 49 | 427 | 118 | 10.3% | 476 |
| 12 | Function (built-in) | 509 | 139 | 293 | 77 | 32.2% | 432 |
| 13 | statements/function | 451 | 173 | 258 | 20 | 40.1% | 431 |
| 14 | expressions/assignment | 485 | 147 | 274 | 64 | 34.9% | 421 |
| 15 | Set (built-in) | 383 | 123 | 220 | 40 | 35.9% | 343 |

These 15 categories account for **12,011 of 16,048 failures** (74.8%).

### 2.2 Root Cause Analysis

After sampling hundreds of failing tests and examining the transpiler implementation, failures cluster into **7 systemic root causes** that cascade across multiple categories.

#### Root Cause 1: Property Descriptor Protocol Gaps (~3,200 tests affected)

**Problem**: The test262 harness (`propertyHelper.js`) uses `verifyProperty()` which calls `Object.getOwnPropertyDescriptor()` on every property being verified. While LambdaJS implements `getOwnPropertyDescriptor()` with internal `__nw_`/`__ne_`/`__nc_` markers, the issue is that **most properties are created without proper descriptor attributes**.

When a class method is defined:
```javascript
class C { m() {} }
```
The method `m` on `C.prototype` should have `{writable: true, enumerable: false, configurable: true}`. But LambdaJS creates it as a plain map entry without the `__ne_m` (non-enumerable) marker, so `getOwnPropertyDescriptor()` may return incorrect attribute values or the `verifyProperty()` check fails.

Similarly, built-in prototype methods (Array.prototype.map, String.prototype.charAt, etc.) should be `{writable: true, enumerable: false, configurable: true}` but may not have correct attributes.

**Cascading impact**:
- ~1,400 Object built-in tests (property descriptor verification)
- ~800 class statement/expression tests (method descriptor checks)
- ~500 Array/String/Function tests (built-in method descriptor verification)
- ~500 other tests using `verifyProperty()` or `propertyIsEnumerable()`

**Fix**: Ensure all built-in methods and class-defined methods are registered with correct `__ne_` (non-enumerable) markers. Add `writable` and `configurable` defaults matching the ES spec for each property creation path.

#### Root Cause 2: Missing Date Setter Methods (~400 tests)

**Problem**: Date only implements **getter methods** (getFullYear, getMonth, getDate, getHours, etc.) and their UTC variants. All **setter methods** are missing:

| Missing Method | Spec Behavior |
|---------------|---------------|
| `setTime(time)` | Sets `[[DateValue]]` to `TimeClip(time)` |
| `setFullYear(year [, month, date])` | Updates year (and optionally month/date) |
| `setMonth(month [, date])` | Updates month (and optionally date) |
| `setDate(date)` | Updates day of month |
| `setHours(hour [, min, sec, ms])` | Updates hours (and optionally min/sec/ms) |
| `setMinutes(min [, sec, ms])` | Updates minutes (and optionally sec/ms) |
| `setSeconds(sec [, ms])` | Updates seconds (and optionally ms) |
| `setMilliseconds(ms)` | Updates milliseconds |
| `setUTCFullYear(year [, month, date])` | UTC variant |
| `setUTCMonth(month [, date])` | UTC variant |
| `setUTCDate(date)` | UTC variant |
| `setUTCHours(hour [, min, sec, ms])` | UTC variant |
| `setUTCMinutes(min [, sec, ms])` | UTC variant |
| `setUTCSeconds(sec [, ms])` | UTC variant |
| `setUTCMilliseconds(ms)` | UTC variant |

Also missing:
- `Date.now()` (static, returns current timestamp)
- `Date.UTC(year, month, ...)` (static, returns UTC timestamp)
- `Date.parse(string)` (static, parses date string)
- `toJSON()`, `toUTCString()`, `toDateString()`, `toTimeString()`
- `getTimezoneOffset()`
- `valueOf()` (should return timestamp number)

**Impact**: 400+ of the 427 Date failures are due to missing setters and utility methods. The getter infrastructure already works; setters just need to mutate the internal timestamp and return it.

#### Root Cause 3: Incomplete Destructuring in For-Of and Assignments (~700 tests)

**Problem**: While basic destructuring works (`[a, b] = arr`, `{x, y} = obj`), several patterns fail:

1. **Object rest destructuring**: `let {a, ...rest} = obj` — logged as unimplemented
2. **Computed property keys in destructuring**: `let {[expr]: val} = obj`
3. **Nested destructuring in for-of**: `for (const [{a}, [b]] of nested) {}`
4. **Destructuring with default + side effects**: evaluation order must match spec
5. **Assignment target destructuring**: `[a.b, c[d]] = arr` (member expression targets)

**Impact**:
- ~250 for-of tests depend on destructuring patterns
- ~200 assignment expression tests use complex destructuring
- ~150 class/function parameter destructuring tests
- ~100 variable declaration destructuring tests

**Fix**: Implement object rest pattern (`...rest`), computed property keys in patterns, and ensure spec-compliant evaluation order for destructuring with defaults.

#### Root Cause 4: Incorrect Property Enumeration Order (~200 tests)

**Problem**: ES2015+ specifies that `Object.keys()`, `for-in`, and `Object.getOwnPropertyNames()` must return keys in this order:
1. Integer indices in ascending numeric order
2. String keys in insertion/creation order
3. Symbol keys in insertion order

LambdaJS uses Lambda's map iteration which follows shape-entry order (insertion order), but **does not sort integer indices first**. Example:

```javascript
var o = {b: 1, 2: 'x', a: 2, 1: 'y'};
Object.keys(o);
// Expected: ["1", "2", "b", "a"]
// Actual: ["b", "2", "a", "1"] (insertion order)
```

**Impact**:
- ~80 for-in enumeration order tests
- ~60 Object.keys/values/entries order tests
- ~40 JSON.stringify key order tests
- ~20 other enumeration-dependent tests

**Fix**: In `js_for_in_keys()` and `Object.keys()`, partition keys into integer-index and string-key groups, sort integers numerically, then append strings in insertion order.

#### Root Cause 5: JSON.stringify/parse Missing Parameters (~100 tests)

**Problem**: `JSON.stringify()` only accepts a single argument. The `replacer` (2nd arg) and `space` (3rd arg) parameters are not implemented. `JSON.parse()` is missing `reviver` support.

```javascript
// These all fail:
JSON.stringify({a: 1, b: 2}, ['b']);           // replacer array
JSON.stringify({a: 1}, null, 2);               // space indentation
JSON.stringify({a: 1}, (key, val) => val + 1); // replacer function
JSON.parse('{"a":1}', (key, val) => val * 2);  // reviver function
```

**Impact**: 106 JSON failures, ~100 recoverable with full parameter support.

#### Root Cause 6: Missing encodeURI/decodeURI (~84 tests)

**Problem**: `encodeURIComponent()` and `decodeURIComponent()` are implemented, but `encodeURI()` and `decodeURI()` are not. The non-Component variants encode/decode differently — they preserve URI-structural characters (`:`, `/`, `?`, `#`, `[`, `]`, `@`, `!`, `$`, `&`, `'`, `(`, `)`, `*`, `+`, `,`, `;`, `=`).

**Impact**: 54 decodeURI + 30 encodeURI = 84 tests at 0% pass rate. These are trivial to implement given the Component variants already exist — just adjust the reserved character set.

#### Root Cause 7: Generator/Iterator Protocol Gaps (~300 tests)

**Problem**: Generator functions and yield are implemented with a state-machine transpilation, but several protocol behaviors are incomplete:

1. **Iterator.return()**: When a for-of loop exits early (break/return/throw), the iterator's `.return()` method must be called. LambdaJS doesn't call it.
2. **Generator.throw()**: `gen.throw(err)` should resume the generator and throw `err` at the current yield point. Incomplete.
3. **Generator.return()**: `gen.return(val)` should force the generator to complete with `{value: val, done: true}`.
4. **`yield*` delegation**: `yield*` should delegate to another iterable's iterator, forwarding `.next()`, `.throw()`, and `.return()`.
5. **First `.next()` argument**: The argument to the first `.next()` call should be ignored per spec.

**Impact**:
- ~185 generator statement tests
- ~195 generator expression tests
- ~80 for-of iterator-close tests

### 2.3 Secondary Failure Patterns

These contribute fewer test failures individually but are still worth addressing:

| Pattern | Failed Tests | Description |
|---------|:-----------:|-------------|
| `with` statement | 143 | Non-strict `with` creates dynamic scope — requires runtime property lookup injection |
| arguments object aliasing | 184 | Mapped arguments should alias formal params in non-strict mode |
| `eval()` runtime | 40+8 | Direct/indirect eval not implemented (dynamic code compilation) |
| `new` expression edge cases | 45 | Constructor return value semantics, `new.target` |
| `try/catch` edge cases | 128 | Catch parameter destructuring, optional catch binding |
| `const`/`let` TDZ | 72+79 | Temporal Dead Zone enforcement before declaration |
| `delete` operator semantics | 49 | Configurable check, strict mode restrictions |
| Error types (built-in) | 62+39 | NativeError subclass properties, `.stack`, Error.captureStackTrace |
| `super` in methods/ctors | 56 | Super property access, super() in derived constructors |
| tagged template literals | 19 | Tag function receives template object with `.raw` |
| `in` operator | 21 | Private field `#x in obj` syntax (already skipped vs normal `in` |
| `instanceof` edge cases | 26 | `Symbol.hasInstance`, cross-realm instanceof |

### 2.4 Strength Areas (Already Working Well)

These areas have high pass rates and need minimal attention:

| Category | Passed/Executed | Pass Rate |
|----------|:--------------:|:---------:|
| BigInt literals | 59/59 | **100%** |
| Future reserved words | 55/55 | **100%** |
| ASI (S7) | 99/101 | **98%** |
| Statement lists (block/class/fn) | 40/40 | **100%** |
| Numeric literals | 147/157 | **93.6%** |
| Throw statement | 14/14 | **100%** |
| Boolean literals | 4/4 | **100%** |
| Null type | 4/4 | **100%** |
| Undefined type | 8/8 | **100%** |
| String literals | 63/73 | **86.3%** |
| AssignmentTargetType | 316/318 | **99.4%** |
| Exponentiation | 36/40 | **90%** |
| Block scope | 115/126 | **91.3%** |
| Identifier start chars | 65/66 | **98.5%** |
| parseFloat | 42/53 | **79.2%** |
| Nullish coalescing | 21/22 | **95.5%** |

## 3. Proposed Phases

### Phase 1: Property Descriptor Correctness (Est. ~3,200 tests recovered)

**Goal**: Ensure all property creation paths produce correct ES-compliant descriptors.

#### 1a. Built-in Method Descriptors

All built-in prototype methods (Array.prototype.*, String.prototype.*, Object.prototype.*, etc.) must be non-enumerable. In `js_runtime.cpp` where methods are registered on prototypes, ensure each method entry has the `__ne_<name>` marker set.

**Specific changes**:
- In the method registration loop for each built-in type, call a helper that sets the non-enumerable marker
- Built-in methods should have: `{writable: true, enumerable: false, configurable: true}`
- Constructor functions should have: `{writable: true, enumerable: false, configurable: false}`
- Static methods (Math.*, JSON.*) should match their spec descriptors
- `Function.prototype.length` should be `{writable: false, enumerable: false, configurable: true}`
- `Function.prototype.name` should be `{writable: false, enumerable: false, configurable: true}`

#### 1b. Class Method Descriptors

When transpiling class declarations/expressions:
```javascript
class C {
    method() {}          // {writable: true, enumerable: false, configurable: true}
    get prop() {}        // {get: fn, set: undefined, enumerable: false, configurable: true}
    static staticM() {}  // same as method, on C rather than C.prototype
}
```

In `transpile_js_mir.cpp`, after installing methods on prototypes, emit code to set `__ne_` markers for each method.

#### 1c. Object.getOwnPropertyDescriptor Accuracy

Audit `js_globals.cpp` L1262-1470 to ensure:
- Missing attributes default correctly (absent `__nw_` marker → writable=true by default for data properties)
- Accessor descriptors return `{get, set, enumerable, configurable}` without `value`/`writable`
- Data descriptors return `{value, writable, enumerable, configurable}` without `get`/`set`
- Returns `undefined` for properties not directly on the object (not inherited)

#### 1d. Property Enumeration Order

Implement spec-compliant key ordering in `js_for_in_keys()` and all `Object.keys()`/`Object.values()`/`Object.entries()` paths:

```
1. Integer-index strings (0, 1, 2, ...) in ascending numeric order
2. Non-index strings in creation order
3. Symbols in creation order (if applicable)
```

**Implementation**: When building the key array, separate keys into two buckets: those that are valid array indices (`/^(0|[1-9][0-9]*)$/` and < 2^32 - 1) vs others. Sort the index bucket numerically, then append the string bucket in insertion order.

#### 1e. for-in Must Respect Enumerable Flag

Only enumerate properties where `enumerable` is `true`. Skip properties flagged with `__ne_<name>`.

```cpp
// In js_for_in_keys():
// When iterating shape entries, skip entries where __ne_<name> exists
```

### Phase 2: Date Object Completion (Est. ~400 tests recovered)

#### 2a. Date Setter Methods

Implement all 15 setter methods. These all follow the same pattern:

```
1. Let t = LocalTime(this.[[DateValue]])     // or UTC: t = this.[[DateValue]]
2. Let newComponent = ToNumber(argument)
3. Compute newDate = MakeDate(MakeDay(...), MakeTime(...)) using updated component
4. Set this.[[DateValue]] = TimeClip(UTC(newDate))  // or: TimeClip(newDate) for UTC setters
5. Return this.[[DateValue]]
```

The internal date representation likely stores a milliseconds-since-epoch timestamp. Each setter:
- Breaks down the timestamp into components (year, month, day, hour, min, sec, ms)
- Replaces the targeted component(s) with the new value(s)
- Recomposes the timestamp using `mktime()` or equivalent
- Returns the new timestamp

The local ↔ UTC conversions use the C library's timezone support (`localtime_r`, `mktime`).

**Implementation plan** (in `js_globals.cpp`):
1. Add a `js_date_set_component()` helper that decomposes timestamp → `struct tm`, applies mutation, recomposes
2. Register setter method IDs (18-32) in the Date method dispatcher
3. Handle optional trailing arguments (e.g. `setFullYear(year, month, date)` — month and date are optional)

#### 2b. Date Static Methods

| Method | Implementation |
|--------|---------------|
| `Date.now()` | Return `gettimeofday()` or `clock_gettime()` in ms |
| `Date.UTC(y,m,d,h,min,s,ms)` | Compose date from components in UTC, return ms timestamp |
| `Date.parse(string)` | Parse ISO 8601 and common date formats, return ms timestamp |

#### 2c. Date Instance Methods (Missing)

| Method | Implementation |
|--------|---------------|
| `valueOf()` | Return `this.[[DateValue]]` (same as `getTime()`) |
| `toJSON()` | Call `toISOString()` |
| `toUTCString()` | Format as "Day, DD Mon YYYY HH:MM:SS GMT" |
| `toDateString()` | Format as "Day Mon DD YYYY" |
| `toTimeString()` | Format as "HH:MM:SS GMT±HHMM (Timezone Name)" |
| `getTimezoneOffset()` | Return `(UTC - local) / 60000` in minutes |
| `getDay()` / `getUTCDay()` | Return day of week (0=Sunday) |

### Phase 3: Destructuring Completion (Est. ~700 tests recovered)

#### 3a. Object Rest Destructuring

```javascript
let {a, b, ...rest} = {a: 1, b: 2, c: 3, d: 4};
// rest = {c: 3, d: 4}
```

Currently logged as unimplemented in `transpile_js_mir.cpp`. Implementation:
1. After extracting named properties, iterate remaining keys
2. Create a new object containing only the non-extracted keys
3. Assign to the rest target variable

#### 3b. Computed Property Keys in Destructuring

```javascript
let key = 'x';
let {[key]: val} = {x: 42}; // val = 42
```

Requires evaluating the key expression at runtime and using it as a dynamic property lookup.

#### 3c. Member Expression Targets in Destructuring

```javascript
let obj = {};
[obj.a, obj.b] = [1, 2];
```

Destructuring targets can be any valid assignment target (member expressions, bracket access), not just identifiers.

#### 3d. Spec-Compliant Evaluation Order

The ES spec mandates a specific evaluation order for destructuring with default values:

```javascript
let order = [];
let {
    [order.push('key1'), 'a']: x = (order.push('default1'), 1),
    [order.push('key2'), 'b']: y = (order.push('default2'), 2)
} = {a: 10};
// order should be: ['key1', 'key2', 'default2']
// x = 10 (default not evaluated because 'a' exists)
// y = 2 (default evaluated because 'b' is undefined)
```

This means: all computed keys are evaluated left-to-right first, then values are extracted and defaults applied.

### Phase 4: JSON Full Parameter Support (Est. ~100 tests recovered)

#### 4a. JSON.stringify(value, replacer, space)

**Replacer function**: `(key, value) => newValue`
- Called for each key/value pair during serialization
- Return value replaces the original value
- Returning `undefined` omits the property

**Replacer array**: `['key1', 'key2']`
- Only include listed properties in output
- Properties appear in the array's order

**Space parameter**: number or string
- Number 1-10: indent with that many spaces
- String: use string as indent prefix (max 10 chars)

Implementation: Extend `format_json()` to accept two optional Item parameters. Thread the replacer through the recursive serialization, calling it at each node. Apply space to indentation.

#### 4b. JSON.parse(text, reviver)

**Reviver function**: `(key, value) => newValue`
- Called bottom-up for each parsed value
- Return value replaces original
- Returning `undefined` deletes the property

Implementation: After parsing JSON into Lambda data, walk the result tree bottom-up, calling the reviver at each node. Modifying values in-place or rebuilding the structure.

### Phase 5: URI Function Completion (Est. ~84 tests recovered)

#### 5a. encodeURI()

Copy `encodeURIComponent()` implementation but add these characters to the "do not encode" set:
```
; , / ? : @ & = + $ - _ . ! ~ * ' ( ) #
A-Z a-z 0-9
```

The component variant only preserves `A-Z a-z 0-9 - _ . ! ~ * ' ( )`. The non-component variant additionally preserves URI-structural characters.

#### 5b. decodeURI()

Copy `decodeURIComponent()` implementation but do not decode escape sequences for reserved URI characters:
```
%23 (#) %24 ($) %26 (&) %2B (+) %2C (,) %2F (/)
%3A (:) %3B (;) %3D (=) %3F (?) %40 (@)
```

### Phase 6: Generator/Iterator Protocol (Est. ~300 tests recovered)

#### 6a. Iterator Close on Early Exit

When a for-of loop exits via `break`, `return`, or `throw`, the engine must call the iterator's `.return()` method (if it exists). This ensures resources are cleaned up.

```javascript
function* gen() {
    try { yield 1; yield 2; }
    finally { console.log('cleanup'); }
}
for (const x of gen()) { break; }
// Must print 'cleanup'
```

**Implementation** in `transpile_js_mir.cpp`:
- Wrap for-of loop bodies in a try-finally block (at the MIR level)
- In the finally block, check if the iterator has a `.return` method and call it
- Handle the case where `.return()` itself throws

#### 6b. Generator.prototype.throw()

```javascript
function* gen() {
    try { yield 1; }
    catch (e) { yield e + ' caught'; }
}
let g = gen();
g.next();          // {value: 1, done: false}
g.throw('error');  // {value: 'error caught', done: false}
```

The generator's state machine must support a "throw mode" where resumption throws at the current yield point instead of returning a value.

#### 6c. Generator.prototype.return()

```javascript
function* gen() {
    try { yield 1; yield 2; }
    finally { yield 'finally'; }
}
let g = gen();
g.next();           // {value: 1, done: false}
g.return('done');   // {value: 'finally', done: false} — finally runs first
g.next();           // {value: 'done', done: true}
```

#### 6d. yield* Delegation

```javascript
function* inner() { yield 'a'; yield 'b'; }
function* outer() { yield* inner(); yield 'c'; }
[...outer()] // ['a', 'b', 'c']
```

`yield*` must:
1. Get the iterator from the operand
2. Forward each `.next()` call to the inner iterator
3. When inner is done, use its return value as the result of the `yield*` expression
4. Forward `.throw()` and `.return()` to inner

### Phase 7: Secondary Improvements (Est. ~700 tests recovered)

#### 7a. arguments Object Mapped Aliasing (~100 tests)

In non-strict functions, `arguments[i]` should alias the corresponding formal parameter:
```javascript
function f(a) { arguments[0] = 10; return a; } // should return 10
function g(a) { a = 10; return arguments[0]; }  // should return 10
```

Implementation: In non-strict mode, `arguments` entries should be backed by references to the same storage as formal parameters, not copies.

#### 7b. TDZ Enforcement for let/const (~100 tests)

In `transpile_js_mir.cpp`, `let` and `const` declarations should mark their variables as "uninitialized" until the declaration is reached at runtime. Accessing them before declaration should throw `ReferenceError`:

```javascript
console.log(x); // ReferenceError: Cannot access 'x' before initialization
let x = 5;
```

Implementation: Initialize `let`/`const` variables to a sentinel "TDZ" value. On every read, check for the sentinel and throw if found. On declaration, replace with the actual value.

#### 7c. Error Object Properties (~50 tests)

- `Error.prototype.message` should be `""` (empty string)
- `Error.prototype.name` should be `"Error"`
- `TypeError.prototype.name` should be `"TypeError"`, etc.
- `.stack` property (non-standard but commonly tested)
- `new Error().toString()` → `"Error"` or `"Error: message"`

#### 7d. Number/Math Gaps (~100 tests)

**Number static properties**:
```javascript
Number.MAX_SAFE_INTEGER  // 2^53 - 1
Number.MIN_SAFE_INTEGER  // -(2^53 - 1)
Number.EPSILON           // 2^-52
Number.POSITIVE_INFINITY // Infinity
Number.NEGATIVE_INFINITY // -Infinity
Number.MAX_VALUE         // ~1.7976931348623157e+308
Number.MIN_VALUE         // ~5e-324
Number.NaN               // NaN
```

**Missing Math functions**: `Math.hypot()`, `Math.imul()`, `Math.clz32()`, `Math.cbrt()`, `Math.log2()`, `Math.log10()`, `Math.expm1()`, `Math.log1p()`, `Math.fround()`, `Math.sign()`, `Math.trunc()`

#### 7e. try/catch Improvements (~50 tests)

- Optional catch binding: `try {} catch {}`  (no parameter)
- Catch parameter destructuring: `try {} catch ({message}) {}`

#### 7f. Tagged Template Literals (~19 tests)

```javascript
function tag(strings, ...values) {
    strings.raw; // array of raw template strings
    return strings[0] + values[0];
}
tag`hello ${name} world`;
```

The tag function receives a frozen template array with a `.raw` property. Currently tagged templates may not pass the template object correctly.

#### 7g. new Expression Semantics (~30 tests)

- If constructor returns an object, that object becomes the result of `new` (not `this`)
- `new.target` should reference the constructor being called
- `new` with no arguments: `new C` equivalent to `new C()`

## 4. Implementation Plan

| Phase | Description | Est. Tests Recovered | Priority | Effort | Status |
|:-----:|-------------|:--------------------:|:--------:|:------:|:------:|
| 1 | Property descriptor correctness | ~3,200 | 🔴 Critical | Medium-High | Partial (1a, 1b, 1d ✅) |
| 2 | Date object completion | ~400 | 🔴 Critical | Medium | ✅ Done |
| 3 | Destructuring completion | ~700 | 🔴 Critical | Medium | ✅ Done |
| 4 | JSON full parameters | ~100 | 🟡 High | Low | ✅ Done |
| 5 | URI function completion | ~84 | 🟡 High | Low | ✅ Done |
| 6 | Generator/iterator protocol | ~300 | 🟡 High | High | ⬜ Not started |
| 7 | Secondary improvements | ~700 | 🟠 Medium | Medium | Partial (7c, 7d ✅) |
| — | Array callback validation | ~69 | 🟡 High | Low | ✅ Done |
| **Total** | | **~5,553** | | | |

### Suggested Implementation Order

**Sprint 1 (Highest ROI)**:
- Phase 1a-1c: Built-in and class method descriptors (fix the most cascading issue first)
- Phase 5: encodeURI/decodeURI (trivial, 84 tests)

**Sprint 2 (Date + JSON)**:
- Phase 2a-2c: Date setters, static methods, and utility methods
- Phase 4a-4b: JSON.stringify replacer/space, JSON.parse reviver

**Sprint 3 (Destructuring + Enumeration)**:
- Phase 1d-1e: Property enumeration order, enumerable flag enforcement
- Phase 3a-3d: Object rest, computed keys, evaluation order

**Sprint 4 (Generators + Secondary)**:
- Phase 6a-6d: Iterator close, throw, return, yield*
- Phase 7a-7g: Arguments aliasing, TDZ, errors, etc.

## 5. Verification Plan

### Test Methodology

After each phase, run the test262 suite and compare:
```bash
make test-js-test262    # or equivalent
```

Record in `test/js/test262_results.json`:
- Pass/fail/skip counts per category
- Delta from v18 baseline
- Any regressions (tests that previously passed but now fail)

### Regression Guards

Each phase should include unit tests in `test/js/` that verify the newly implemented behavior:
- `test/js/v20_property_descriptors.js` + `.txt`
- `test/js/v20_date_setters.js` + `.txt`
- `test/js/v20_destructuring.js` + `.txt`
- `test/js/v20_json_params.js` + `.txt`
- `test/js/v20_uri_encoding.js` + `.txt`
- `test/js/v20_generator_protocol.js` + `.txt`

### Acceptance Criteria

| Phase | Target Pass Rate (Executed) | Absolute Passes | Actual |
|:-----:|:---------------------------:|:---------------:|:------:|
| v18 baseline | 40.6% | 10,982 | 10,982 ✅ |
| After Phase 1 (partial) | ~52% | ~14,100 | 11,439 (1a, 1b, 1d done) |
| After Phase 2 | ~54% | ~14,500 | — (included in cumulative) |
| After Phase 3 | ~56% | ~15,200 | — (included in cumulative) |
| After Phase 4 | ~57% | ~15,300 | — (included in cumulative) |
| After Phase 5 | ~57% | ~15,400 | — (included in cumulative) |
| **Current (cumulative)** | — | — | **12,036 (44.5%)** |
| After Phase 6 | ~58% | ~15,700 | ⬜ |
| After Phase 7 | ~61% | ~16,500 | ⬜ |

## 6. Out of Scope (Not Proposed)

These features are intentionally excluded from this proposal due to high complexity or low test coverage ROI:

| Feature | Reason |
|---------|--------|
| **Proxy/Reflect** (skipped, ~80 features) | Fundamental runtime architecture change; meta-programming not needed for most JS code |
| **TypedArrays** (0% pass, 1,438 tests) | Requires new typed buffer infrastructure; could be a separate v21 proposal |
| **async/await + Promises** (skipped/4%) | Requires event loop, microtask queue, and scheduler infrastructure — separate project |
| **eval()** (0% pass) | Dynamic compilation requires runtime access to the transpiler/parser — architectural decision |
| **with statement** (10% pass) | Deprecated, non-strict only, requires dynamic scope chain modification |
| **Modules** (skipped) | import/export already handled separately; test262 module tests need different runner |
| **WeakRef/FinalizationRegistry** (skipped) | GC integration, niche use case |
| **Symbol well-known methods** (skipped) | Symbol.iterator etc. require broader protocol changes |
| **Private fields runtime** (partial) | Already parsed; runtime enforcement needs audit but may "just work" |

### Future Proposal (v21): TypedArrays

TypedArrays are the single largest 0% category (1,438 tests, 886 executed). A dedicated proposal would cover:
- `ArrayBuffer` (byte buffer allocation)
- `DataView` (heterogeneous access)
- 9 TypedArray constructors (Int8Array, Uint8Array, Int16Array, Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array, BigInt64Array/BigUint64Array)
- Shared `.prototype` methods (subarray, set, slice, copyWithin, etc.)
- Interop with `Array.from()`, spread, for-of

### Future Proposal (v22): Async/Await + Promises

Promise infrastructure (652 tests, most skipped) requires:
- Microtask queue with proper scheduling
- `Promise.resolve()`, `.reject()`, `.all()`, `.allSettled()`, `.any()`, `.race()`
- `async function` / `await` transpilation to promise chains
- `for await...of` for async iterables

## 7. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|:----------:|:------:|------------|
| Property descriptor changes break existing passes | Medium | High | Run baseline tests before and after; compare per-test results |
| Date timezone handling differs from V8 | Medium | Low | Use platform-agnostic UTC tests for validation |
| Enumeration order change causes regressions | Low | Medium | The current order is non-compliant; fixing it should only help |
| Destructuring evaluation order hard to get right | High | Medium | Copy SpiderMonkey/V8 behavior; validate with spec tests |
| Generator protocol changes break existing generators | Medium | High | Run existing generator tests as regression suite |
| Performance regression from descriptor checks | Low | Low | Descriptor checks are O(1) hash lookups |

## 8. Summary

The 16,048 test262 failures are dominated by a few systemic issues, not thousands of independent bugs. Property descriptor protocol failures alone account for an estimated ~3,200 cascading failures across Object, Array, Class, String, and Function test categories.

The proposed 7 phases address the root causes in priority order, with an estimated recovery of ~5,500 tests. This would raise the test262 pass rate from **40.6% to ~61%** on executed tests, establishing LambdaJS as a substantially more ES-compliant engine.

The most important single fix — ensuring all property creation paths produce correct descriptors with proper enumerable/writable/configurable attributes — has the highest leverage because test262's harness functions depend on descriptor correctness for verification.

## 9. Test Runner Performance Tuning

### Problem

The test262 GTest runner (`test/test_js_test262_gtest.cpp`) executes ~27K tests by spawning child processes via `posix_spawn()`. Each child runs `./lambda.exe js-test-batch` with a manifest of JS test sources piped through stdin. The original configuration:

- **Batch size**: 5 tests per process spawn
- **Workers**: 6 parallel threads
- **Timeout**: 10s per test
- **No crash recovery**: if a test crashed, all remaining tests in that batch were silently lost

This resulted in **~5,400 process spawns**, producing ~372s of system time (page-table setup, pipe I/O, process teardown) and a **2:38 wall clock** for a full run.

### Analysis

Profiling the 3-phase pipeline revealed Phase 2 (execute) consumed 99% of wall time:

| Phase | Time | % of Total |
|-------|-----:|:----------:|
| Phase 1 (prepare metadata) | 0.9s | 0.6% |
| Phase 2 (execute batches) | 146.1s | **99.3%** |
| Phase 3 (evaluate results) | <0.1s | <0.1% |

Key observations:
- **System time was 372s** on a 419s user-time run — spawn overhead dominated
- Each `posix_spawn()` cost ~69ms in syscall overhead
- Only **8 tests** ever hit the 10s timeout — not a significant factor
- **839 tests** were "batch-lost" (crashed process killed remaining tests in batch)
- Machine has 10 cores but only 6 workers were used (500% CPU)

### Changes Made

#### 1. Batch size 5 → 50 (10× fewer spawns)

Reduces spawn count from ~5,400 to ~540. Phase 2 execution dropped from 146s to ~79s. The tradeoff: larger blast radius when a test crashes (more tests lost per crash).

#### 2. Workers 6 → 8 (better CPU utilization)

On a 10-core machine, 6 workers left 4 cores idle. Increasing to 8 improved CPU utilization from 500% to ~590%.

#### 3. Crash recovery pass (Phase 2b)

With batch size 50, crashes lose up to 49 co-batched tests. A new Phase 2b re-runs all batch-lost tests in small batches of 5:

```
Phase 2:  79s — 21,250 results collected (batch size 50)
Phase 2b: 38s — recovered 4,700 of 5,669 lost tests (batch size 5)
```

The ~969 remaining unrecovered tests are genuine crashers (they crash even when run solo).

#### 4. `--baseline-only` mode

New flag that filters execution to only tests listed in `test262_baseline.txt` (tests known to pass). Skips all ~15K expected-failing tests, enabling fast regression checks during development.

### Results

| Mode | Before | After | Speedup |
|------|-------:|------:|:-------:|
| **Full run (wall clock)** | 2:38 | 2:01 | **1.3×** |
| **Full run (system time)** | 372s | 307s | **17% less** |
| **`--baseline-only` (wall clock)** | 2:38 | 0:35 | **4.5×** |

Detailed phase breakdown (full run, after tuning):

| Phase | Time | Notes |
|-------|-----:|-------|
| Phase 1 (prepare) | 0.9s | Unchanged — metadata parsing is fast |
| Phase 2 (execute) | 79s | 540 spawns vs 5,400 previously |
| Phase 2b (retry) | 38s | Crash recovery for batch-lost tests |
| Phase 3 (evaluate) | <0.1s | Unchanged |
| **Total** | **~118s** | Down from 147s |

#### Batch size tradeoff analysis

| Batch Size | Phase 2 | Batch-Lost | Retry Time | Total Exec | Wall Clock |
|:----------:|--------:|-----------:|-----------:|-----------:|-----------:|
| 5 (original) | 146s | 839 | — | 146s | 2:38 |
| 25 | 89s | 3,228 | 31s | 120s | 2:05 |
| **50 (chosen)** | **79s** | **5,669** | **38s** | **117s** | **2:01** |

Batch size 50 is optimal: the time saved from fewer spawns outweighs the retry overhead.

### Usage

```bash
# Full compliance run (2 min)
./test/test_js_test262_gtest.exe

# Fast regression check — baseline tests only (35s)
./test/test_js_test262_gtest.exe --baseline-only

# Update baseline after improvements
./test/test_js_test262_gtest.exe --update-baseline
```

## 10. Batch Crash Root-Cause Fixes

### Problem

With the Phase 2b crash recovery in place (Section 9), ~968 tests remained permanently lost — they crashed `lambda.exe` (SIGSEGV/SIGABRT/SIGBUS) even when retried individually. These crashes also caused **collateral damage**: each crash killed the entire batch, losing up to 49 co-batched tests per incident.

A full individual scan of all 968 batch-lost tests confirmed **208 genuine crashers** (the rest were collateral from sharing a batch with a crasher).

### Analysis

Crash reports from macOS `.ips` diagnostic files were parsed to extract stack traces. The crashes clustered into several categories:

| Category | Crash Count | Root Cause |
|----------|:-----------:|------------|
| Class expr in destructuring defaults | ~89 | SIGSEGV in `jm_transpile_expression` — NULL `ce` pointer |
| String-literal class fields | ~16 | SIGSEGV in instance field emission — NULL `inf->name` |
| `Array.prototype` runtime | ~39 | `Item::get_double()` / `Item::type_id()` type errors |
| `Object.*` builtins | ~30 | defineProperty / prototype crashes |
| Yield in destructuring | ~12 | `[x = yield] = vals` pattern not handled |
| MIR list corruption | ~8 | `DLIST_MIR_insn_t_append` at `mir.h:290` |
| Other (Function, new_expr, etc.) | ~14 | Various |

The top two categories (class expr in defaults + string-literal fields) were the most impactful and fixable without deep architectural changes.

### Fix 1: Class Expressions in Destructuring Parameter Defaults

**File**: `lambda/js/transpile_js_mir.cpp` — `jm_collect_functions()`

**Root cause**: The function pre-pass (`jm_collect_functions`) that hoists class expressions only traversed `fn->body`, never `fn->params`. When a class expression appeared as a destructuring default in a parameter:

```javascript
var f = ([cls = class {}, xCls = class X {}]) => { /* ... */ };
```

The class was never collected during the pre-pass, so `ce` (class expression struct) was NULL when `jm_transpile_expression` later tried to emit it, causing a SIGSEGV at an offset ~0x1420 from NULL.

**Fix**: Added parameter traversal before the body traversal in `jm_collect_functions` for `FUNCTION_DECLARATION`, `FUNCTION_EXPRESSION`, and `ARROW_FUNCTION` cases:

```cpp
// collect functions in params (for class expressions in destructuring defaults)
JsAstNode* param = fn->params;
while (param) {
    jm_collect_functions(mt, param);
    param = param->next;
}
```

Also added NULL guards on `ce` in three downstream emission sites (`.length` property, static fields, static blocks).

### Fix 2: String-Literal Class Field Names

**File**: `lambda/js/transpile_js_mir.cpp` — instance field emission

**Root cause**: Instance field emission assumed `inf->name` (the identifier node) was always set. But string-literal field names like `"a"` or `'b'` have no identifier — only a `key_expr`:

```javascript
class C {
    "a" = 42;   // inf->name is NULL, inf->key_expr is the string literal
    'b' = 99;
}
```

Accessing `inf->name->chars` when `inf->name` was NULL caused SIGSEGV.

**Fix**: Added a fallback cascade in both parent-chain and own-class instance field emission:

```cpp
if (inf->name) {
    key = jm_box_string_literal(mt, inf->name->chars, inf->name->length);
} else if (inf->key_expr) {
    key = jm_transpile_box_item(mt, inf->key_expr);
} else {
    continue;  // skip malformed field
}
```

### Results

| Metric | Before Fixes | After Fixes | Change |
|--------|:------------:|:-----------:|:------:|
| Phase 2 results collected | 21,131 | 23,507 | +2,376 |
| Batch-lost tests | 5,788 | 3,412 | −41% |
| Genuine crashers (individual) | 208 | 186 | −11% |
| Batch-lost collateral | 968 | 611 | −37% |
| Baseline regressions | — | 0 | ✓ |

### Remaining Crash Categories

The 186 remaining genuine crashers require deeper fixes:

- **`Array.prototype` runtime crashes** (~39): Runtime type checking issues (`Item::get_double()`, `Item::type_id()`) in array method callbacks — needs runtime guard improvements
- **`Object.*` builtin crashes** (~30): `defineProperty`, `defineProperties`, `seal`, `assign` — needs descriptor infrastructure hardening
- **Yield-in-destructuring** (~12): `[x = yield] = vals` pattern inside generators — needs transpiler support for yield as destructuring default
- **MIR list corruption** (~8): `DLIST_MIR_insn_t_append` crashes — likely emitting MIR instructions in wrong context
- **`Function.prototype` crashes** (~12): `bind`/`call`/`apply` edge cases
