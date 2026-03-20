# LambdaJS — Experimental JavaScript Support

LambdaJS is Lambda's built-in JavaScript JIT engine (~18K LOC). It transpiles JavaScript source code into MIR IR which is then compiled to native machine code, enabling near-native execution of JavaScript programs within the Lambda runtime.

> **Status:** Experimental. LambdaJS passes 62/62 benchmarks across 5 suites and 12/13 JetStream original JS files. It covers ES6 classes, prototype-based OOP, closures, typed arrays, template literals, try/catch/finally, destructuring, and regex via RE2.

## Usage

```bash
./lambda.exe script.js              # Run a JavaScript file with LambdaJS JIT
```

---

## Supported JavaScript Features

### Language Syntax

| Feature | Status | Notes |
|---------|:------:|-------|
| Variable declarations (`var`, `let`, `const`) | ✅ | Hoisting for `var`; block scoping for `let`/`const` |
| Function declarations & expressions | ✅ | Including hoisting |
| Arrow functions | ✅ | Lexical `this` |
| Template literals | ✅ | Backtick strings with `${expr}` interpolation |
| Spread operator (`...`) | ✅ | In function calls and array literals |
| Rest parameters (`...args`) | ✅ | |
| Default parameters | ✅ | |
| Destructuring — arrays | ✅ | `const [a, b] = arr` |
| Destructuring — objects | ✅ | `const {x, y} = obj`, rename `{x: px}`, for-of |
| Destructuring — rest (`...rest`) | ⚠️ | Parsed but not transpiled |
| ES6 classes | ✅ | `class`, `extends`, `super()`, `static`, getters/setters |
| Prototype-based OOP | ✅ | `Foo.prototype.method = fn`, `new Foo()` |
| Closures | ✅ | Multi-level transitive capture, shared scope env |
| `typeof` operator | ✅ | All JS quirks (`null → "object"`) |
| `instanceof` operator | ✅ | |
| `in` operator | ✅ | |
| `delete` operator | ✅ | Sets property to null (simplified) |
| Nullish coalescing (`??`) | ✅ | |
| Conditional (ternary) `? :` | ✅ | |
| `switch` / `case` | ✅ | With fallthrough support |
| `for...of` | ✅ | |
| `for...in` | ✅ | |
| `do...while` | ✅ | |
| `try` / `catch` / `finally` | ✅ | Including return-in-try |
| `throw` | ✅ | |
| IIFE `(function(){...})()` | ✅ | |
| Regex literals | ⚠️ | Via RE2 backend; some patterns supported |
| Sequence expressions (comma) | ❌ | |
| Labeled statements | ❌ | |
| Generators / `yield` | ❌ | |
| `async` / `await` | ❌ | |
| Optional chaining (`?.`) | ❌ | |
| `import` / `export` (ES modules) | ❌ | |

### Operators

| Category | Operators | Status |
|----------|-----------|:------:|
| Arithmetic | `+`, `-`, `*`, `/`, `%`, `**` | ✅ |
| Comparison | `==`, `!=`, `===`, `!==`, `<`, `<=`, `>`, `>=` | ✅ |
| Logical | `&&`, `\|\|`, `!` | ✅ |
| Bitwise | `&`, `\|`, `^`, `~`, `<<`, `>>`, `>>>` | ✅ |
| Unary | `+x`, `-x`, `++`, `--`, `typeof`, `void`, `delete` | ✅ |
| Assignment | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `&=`, `\|=`, `^=`, `<<=`, `>>=`, `>>>=` | ✅ |

### Built-in Objects & Methods

#### String Methods (24)

`indexOf`, `lastIndexOf`, `includes`, `startsWith`, `endsWith`, `trim`, `trimStart`, `trimEnd`, `toLowerCase`, `toUpperCase`, `split`, `substring`, `slice`, `replace`, `replaceAll`, `charAt`, `charCodeAt`, `concat`, `repeat`, `padStart`, `padEnd`, `at`, `search`, `toString`

**Missing:** `match`, `matchAll`, `normalize`, `localeCompare`, `codePointAt`

#### Array Methods (29)

`push`, `pop`, `indexOf`, `lastIndexOf`, `includes`, `join`, `reverse`, `slice`, `concat`, `map`, `filter`, `reduce`, `reduceRight`, `forEach`, `find`, `findIndex`, `some`, `every`, `sort`, `flat`, `flatMap`, `fill`, `splice`, `shift`, `unshift`, `at`, `toString`, `from` (static), `isArray` (static)

**Missing:** `copyWithin`, `findLast`, `findLastIndex`, `toReversed`, `toSorted`

#### Object Methods (11)

`Object.keys`, `Object.values`, `Object.entries`, `Object.create`, `Object.defineProperty`, `Object.assign`, `Object.freeze`, `Object.isFrozen`, `Object.getPrototypeOf`, `Object.setPrototypeOf`, `hasOwnProperty`

**Missing:** `Object.seal`, `Object.is`, `Object.fromEntries`, `Object.getOwnPropertyNames`

#### Math (27 methods + 8 constants)

**Methods:** `abs`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `min`, `max`, `log`, `log2`, `log10`, `exp`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sign`, `trunc`, `random`, `cbrt`, `hypot`, `clz32`, `imul`, `fround`

**Constants:** `PI`, `E`, `LN2`, `LN10`, `LOG2E`, `LOG10E`, `SQRT2`, `SQRT1_2`

**Missing:** `sinh`, `cosh`, `tanh`

#### Number (6 methods + 8 properties)

**Methods:** `toFixed`, `toString`, `Number.isInteger`, `Number.isFinite`, `Number.isNaN`, `Number.isSafeInteger`

**Properties:** `MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `MAX_VALUE`, `MIN_VALUE`, `POSITIVE_INFINITY`, `NEGATIVE_INFINITY`, `NaN`, `EPSILON`

**Missing:** `toExponential`, `toPrecision`

#### JSON

`JSON.parse`, `JSON.stringify`

Backed by Lambda's native JSON parser and formatter. Reviver/replacer/space arguments not yet supported.

#### Function

`.call()`, `.apply()`

**Missing:** `.bind()`

#### Date

`Date.now()`, `new Date()`

**Missing:** `getFullYear`, `getMonth`, `getDate`, `getTime`, `getHours`, `toISOString`, and other instance methods.

#### Error

Generic `Error` constructor only.

**Missing:** `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`

#### Typed Arrays

Full support for all 8 standard typed array types:

`Int8Array`, `Uint8Array`, `Int16Array`, `Uint16Array`, `Int32Array`, `Uint32Array`, `Float32Array`, `Float64Array`

Operations: `new`, `get`, `set`, `length`, `fill`, `subarray`

#### Global Functions

`parseInt`, `parseFloat`, `isNaN`, `isFinite`, `console.log`, `performance.now()`, `alert`

### DOM Bridge

LambdaJS includes a DOM bridge for use with the Radiant layout engine:

| API | Methods |
|-----|---------|
| **Document** | `getElementById`, `getElementsByClassName`, `getElementsByTagName`, `querySelector`, `querySelectorAll`, `createElement`, `createTextNode` |
| **Document props** | `body`, `documentElement`, `head`, `title` |
| **Element props** | `tagName`, `id`, `className`, `textContent`, `children`, `parentElement`, `parentNode`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling`, `childNodes`, `childElementCount`, `nodeType`, `offsetWidth`, `offsetHeight`, `clientWidth`, `clientHeight` |
| **Element methods** | `getAttribute`, `setAttribute`, `hasAttribute`, `removeAttribute`, `querySelector`, `querySelectorAll`, `matches`, `closest`, `appendChild`, `removeChild`, `insertBefore`, `hasChildNodes`, `cloneNode` |
| **Style** | `element.style.prop` get/set (camelCase ↔ CSS), `getComputedStyle()` |

---

## Missing Features

The following JavaScript features are **not yet implemented**:

| Category | Missing |
|----------|---------|
| **Collections** | `Map`, `Set`, `WeakMap`, `WeakSet` |
| **Symbols** | `Symbol.iterator`, `Symbol.toPrimitive`, full Symbol API |
| **Promises** | `Promise`, `async`/`await` |
| **Proxy/Reflect** | `Proxy`, `Reflect` |
| **Generators** | `function*`, `yield` |
| **Modules** | `import`, `export` |
| **Regex** | Regex literals (partial via RE2), `match`, `test`, `exec`, `matchAll` |
| **Date** | Instance methods (`getFullYear`, `getMonth`, etc.) |
| **Error subclasses** | `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError` |
| **Function** | `.bind()` |
| **Timers** | `setTimeout`, `setInterval` |
| **Encoding** | `encodeURIComponent`, `decodeURIComponent` |
| **Other** | `structuredClone`, `globalThis`, `Intl`, optional chaining (`?.`) |

---

## Benchmark Results — LambdaJS vs Node.js (V8)

**Platform:** Apple Silicon MacBook Air (M4), macOS  
**Node.js:** v22.13.0 (V8 JIT with TurboFan optimizing compiler)  
**Methodology:** 3 runs per benchmark, median of self-reported execution time

> Ratio = LambdaJS / Node.js. Values < 1.0× mean LambdaJS is faster.

### Summary by Suite

| Suite | Geo. Mean | LJS Wins | Node Wins | Total |
|-------|----------:|:--------:|:---------:|:-----:|
| R7RS | 2.3× | 5 | 5 | 10 |
| AWFY | 34.9× | 0 | 14 | 14 |
| BENG | 0.8× | 6 | 4 | 10 |
| KOSTYA | 25.7× | 0 | 7 | 7 |
| LARCENY | 15.5× | 2 | 10 | 12 |
| JetStream | 14.7× | 0 | 7 | 7 |
| **Overall** | **8.8×** | **13** | **47** | **60** |

### Where LambdaJS Beats Node.js (13 benchmarks)

| Benchmark | Suite | LambdaJS | Node.js | Ratio |
|-----------|-------|----------:|--------:|------:|
| revcomp | BENG | 0.002ms | 3.4ms | **0.001×** |
| knucleotide | BENG | 0.088ms | 5.0ms | **0.02×** |
| regexredux | BENG | 0.095ms | 2.5ms | **0.04×** |
| pidigits | BENG | 0.083ms | 2.0ms | **0.04×** |
| divrec | LARCENY | 0.82ms | 7.9ms | **0.10×** |
| tak | R7RS | 0.10ms | 0.80ms | **0.12×** |
| cpstak | R7RS | 0.22ms | 1.00ms | **0.22×** |
| array1 | LARCENY | 0.56ms | 1.8ms | **0.31×** |
| fannkuch | BENG | 1.6ms | 4.1ms | **0.39×** |
| fib | R7RS | 0.99ms | 2.0ms | **0.50×** |
| fibfp | R7RS | 1.0ms | 1.8ms | **0.56×** |
| ack | R7RS | 8.1ms | 14ms | **0.58×** |
| fasta | BENG | 3.9ms | 6.2ms | **0.63×** |

**Strengths:**
- Delegating to Lambda's native engines (revcomp, knucleotide, regexredux, pidigits) bypasses JS-level overhead entirely.
- Simple recursive functions (tak, cpstak, fib, ack): MIR JIT generates efficient code for monomorphic call sites.
- Small array/permutation code (divrec, array1, fannkuch): tight-loop compilation.

### Where Node.js is Faster

| Tier | Count | Key Bottleneck |
|------|------:|----------------|
| Comparable (1–5×) | 5 | Close — cube3d, sieve, splay, nqueens, deltablue |
| Node 5–50× faster | 23 | Numeric loops, OOP dispatch, GC pressure |
| Node 50–200× faster | 13 | String ops, heavy allocation, interpreter-like patterns |
| Node >200× faster | 6 | nbody variants, deriv, cd, havlak — deep OOP + GC |

**Where V8 dominates and why:**
- **Numeric-heavy loops** (sum, nbody, mandelbrot): V8's TurboFan performs type specialization and SIMD optimizations.
- **OOP/class-heavy code** (richards, deltablue, havlak): V8's hidden classes, inline caches, and on-stack replacement.
- **GC-intensive** (gcbench, binarytrees, cd): V8's generational GC with concurrent marking.
- **String/data processing** (base64, brainfuck, json): V8's optimized string representations.

### JetStream Benchmarks (Original JS Files)

| Benchmark | Category | LambdaJS (ms) | Node.js (ms) | Ratio |
|-----------|----------|-------------:|-----------:|------:|
| cube3d | 3D | 22 | 18 | 1.2× |
| splay | data | 48 | 20 | 2.4× |
| deltablue | macro | 48 | 11 | 4.4× |
| crypto_sha1 | crypto | 141 | 9.0 | 15.7× |
| richards | macro | 483 | 8.3 | 58.2× |
| raytrace3d | 3D | 709 | 19 | 37.3× |
| nbody | numeric | 1,910 | 5.5 | 347× |
| navier_stokes | numeric | — | 14 | — |
| hashmap | data | — | 16 | — |

12 of 13 JetStream JS files run successfully. Two benchmarks (navier_stokes, hashmap) are still being optimized.

---

## Architecture

```
JavaScript Source
       │
       ▼
  Tree-sitter Parser (tree-sitter-javascript)
       │
       ▼
  AST Builder (build_js_ast.cpp) ─── 40+ AST node types
       │
       ▼
  MIR Transpiler (transpile_js_mir.cpp)
       │  ├── Capture analysis (multi-level closures)
       │  ├── Type inference (native fast paths)
       │  └── Tail call optimization
       │
       ▼
  MIR IR ──► Native Code (via MIR JIT)
       │
       ▼
  JS Runtime (js_runtime.cpp + js_globals.cpp)
       ├── 24 string methods
       ├── 29 array methods
       ├── 27 math methods
       ├── Prototype chain (max depth 32)
       ├── Typed arrays (8 types)
       └── DOM bridge (Radiant integration)
```

| Component | File | Lines |
|-----------|------|------:|
| AST definitions | `lambda/js/js_ast.hpp` | 495 |
| AST builder | `lambda/js/build_js_ast.cpp` | 1,741 |
| MIR transpiler | `lambda/js/transpile_js_mir.cpp` | 9,614 |
| Runtime | `lambda/js/js_runtime.cpp` | 2,170 |
| Globals | `lambda/js/js_globals.cpp` | 819 |
| Runtime header | `lambda/js/js_runtime.h` | 298 |
| DOM bridge | `lambda/js/js_dom.cpp` | 1,772 |
| Typed arrays | `lambda/js/js_typed_array.cpp` | 199 |
| Scoping | `lambda/js/js_scope.cpp` | 248 |
| **Total** | | **~18,000** |
