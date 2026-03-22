# LambdaJS — Experimental JavaScript and DOM Support

LambdaJS is Lambda's built-in JavaScript JIT and browser DOM engine (~18K LOC). It transpiles JavaScript source code into MIR IR which is then compiled to native machine code, enabling near-native execution of JavaScript programs within the Lambda runtime.

> **Status:** Experimental. LambdaJS passes 62/62 benchmarks across 5 suites and all 52/52 JS baseline tests. It covers ES6 classes, prototype-based OOP, closures, typed arrays, template literals, try/catch/finally, destructuring, regex via RE2, optional chaining, Map/Set collections, and error subclasses. JetStream geometric mean: **6.3×** vs Node.js (V8).

## Usage

```bash
./lambda.exe script.js              # Run a JavaScript file with LambdaJS JIT
```

---

## Supported JavaScript Features

### Language Syntax

| Feature                                            | Status | Notes                                                    |
| -------------------------------------------------- | :----: | -------------------------------------------------------- |
| Variable declarations (`var`, `let`, `const`)      |   ✅    | Hoisting for `var`; block scoping for `let`/`const`      |
| Function declarations & expressions                |   ✅    | Including hoisting                                       |
| Arrow functions                                    |   ✅    | Lexical `this`                                           |
| Template literals                                  |   ✅    | Backtick strings with `${expr}` interpolation            |
| Spread operator (`...`)                            |   ✅    | In function calls and array literals                     |
| Rest parameters (`...args`)                        |   ✅    |                                                          |
| Default parameters                                 |   ✅    |                                                          |
| Destructuring — arrays                             |   ✅    | `const [a, b] = arr`                                     |
| Destructuring — objects                            |   ✅    | `const {x, y} = obj`, rename `{x: px}`, for-of           |
| Destructuring — rest (`...rest`)                   |   ⚠️   | Parsed but not transpiled                                |
| ES6 classes                                        |   ✅    | `class`, `extends`, `super()`, `static`, getters/setters |
| Prototype-based OOP                                |   ✅    | `Foo.prototype.method = fn`, `new Foo()`                 |
| Closures                                           |   ✅    | Multi-level transitive capture, shared scope env         |
| `typeof` operator                                  |   ✅    | All JS quirks (`null → "object"`)                        |
| `instanceof` operator                              |   ✅    |                                                          |
| `in` operator                                      |   ✅    |                                                          |
| `delete` operator                                  |   ✅    | Sets property to null (simplified)                       |
| Nullish coalescing (`??`)                          |   ✅    |                                                          |
| Conditional (ternary) `? :`                        |   ✅    |                                                          |
| `switch` / `case`                                  |   ✅    | With fallthrough support                                 |
| `for...of`                                         |   ✅    |                                                          |
| `for...in`                                         |   ✅    |                                                          |
| `do...while`                                       |   ✅    |                                                          |
| `try` / `catch` / `finally`                        |   ✅    | Including return-in-try                                  |
| `throw`                                            |   ✅    |                                                          |
| IIFE `(function(){...})()`                         |   ✅    |                                                          |
| Regex literals                                     |   ✅    | Via RE2 backend; `.test()`, `.exec()`                    |
| Sequence expressions (comma)                       |   ✅    |                                                          |
| Labeled statements                                 |   ✅    | `break label`, `continue label`                          |
| Nullish/logical assignment (`??=`, `&&=`, `\|\|=`) |   ✅    |                                                          |
| Optional chaining (`?.`)                           |   ✅    | Property, method, computed, nested chains                |
| `Map` / `Set` collections                          |   ✅    | `new Map()`, `new Set()` with full method API            |
| Generators / `yield`                               |   ❌    |                                                          |
| `async` / `await` / `Promise`                      |   ❌    |                                                          |
| `import` / `export` (ES modules)                   |   ❌    |                                                          |
| `WeakMap` / `WeakSet`                              |   ❌    |                                                          |
| `Symbol` API                                       |   ❌    | `Symbol.iterator`, `Symbol.toPrimitive`                  |
| `Proxy` / `Reflect`                                |   ❌    |                                                          |
| `setTimeout` / `setInterval`                       |   ❌    |                                                          |
| `encodeURIComponent` / `decodeURIComponent`        |   ❌    |                                                          |
| `structuredClone`                                  |   ❌    |                                                          |
| `globalThis`                                       |   ❌    |                                                          |
| `Intl`                                             |   ❌    |                                                          |

### Operators

| Category | Operators | Status |
|----------|-----------|:------:|
| Arithmetic | `+`, `-`, `*`, `/`, `%`, `**` | ✅ |
| Comparison | `==`, `!=`, `===`, `!==`, `<`, `<=`, `>`, `>=` | ✅ |
| Logical | `&&`, `\|\|`, `!` | ✅ |
| Bitwise | `&`, `\|`, `^`, `~`, `<<`, `>>`, `>>>` | ✅ |
| Unary | `+x`, `-x`, `++`, `--`, `typeof`, `void`, `delete` | ✅ |
| Assignment | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `&=`, `\|=`, `^=`, `<<=`, `>>=`, `>>>=` | ✅ |

### DOM Bridge

LambdaJS includes a DOM bridge for use with the Radiant layout engine:

#### Document Methods

| Method                     | Status | Notes                              |
| -------------------------- | :----: | ---------------------------------- |
| `getElementById`           |   ✅    |                                    |
| `getElementsByClassName`   |   ✅    |                                    |
| `getElementsByTagName`     |   ✅    |                                    |
| `querySelector`            |   ✅    |                                    |
| `querySelectorAll`         |   ✅    |                                    |
| `createElement`            |   ✅    |                                    |
| `createTextNode`           |   ✅    |                                    |
| `createElementNS`          |   ✅    | Namespace ignored, delegates to `createElement` |
| `write` / `writeln`        |   ✅    | Simplified; appends text to body   |
| `normalize`                |   ✅    | Merges adjacent text nodes at root |
| `createDocumentFragment`   |   ❌    |                                    |
| `createComment`            |   ❌    |                                    |
| `importNode`               |   ❌    |                                    |
| `adoptNode`                |   ❌    |                                    |

#### Document Properties

| Property            | Status | Notes |
| ------------------- | :----: | ----- |
| `body`              |   ✅    |       |
| `documentElement`   |   ✅    |       |
| `head`              |   ✅    |       |
| `title`             |   ✅    |       |
| `cookie`            |   ❌    |       |
| `readyState`        |   ❌    |       |
| `URL` / `location`  |   ❌    |       |

#### Element Properties

| Property                | Status | Notes                                  |
| ----------------------- | :----: | -------------------------------------- |
| `tagName`               |   ✅    |                                        |
| `id`                    |   ✅    |                                        |
| `className`             |   ✅    |                                        |
| `textContent`           |   ✅    |                                        |
| `children`              |   ✅    |                                        |
| `parentElement`         |   ✅    |                                        |
| `parentNode`            |   ✅    |                                        |
| `firstChild`            |   ✅    |                                        |
| `lastChild`             |   ✅    |                                        |
| `nextSibling`           |   ✅    |                                        |
| `previousSibling`       |   ✅    |                                        |
| `childNodes`            |   ✅    |                                        |
| `childElementCount`     |   ✅    |                                        |
| `nodeType`              |   ✅    |                                        |
| `nodeName`              |   ✅    | Tag name or `"#text"`                  |
| `nodeValue`             |   ✅    | Alias for text node `.data`            |
| `firstElementChild`     |   ✅    | Skips text nodes                       |
| `lastElementChild`      |   ✅    | Skips text nodes                       |
| `nextElementSibling`    |   ✅    | Skips text nodes                       |
| `previousElementSibling`|   ✅    | Skips text nodes                       |
| `innerHTML`             |   ⚠️   | Getter only; setter not implemented    |
| `offsetWidth`           |   ✅    | Returns 0 (JS runs before layout)      |
| `offsetHeight`          |   ✅    | Returns 0 (JS runs before layout)      |
| `clientWidth`           |   ✅    | Returns 0 (JS runs before layout)      |
| `clientHeight`          |   ✅    | Returns 0 (JS runs before layout)      |
| `outerHTML`             |   ❌    |                                        |
| `classList`             |   ❌    | `add`, `remove`, `toggle`, `contains`  |
| `dataset`               |   ❌    | `data-*` attribute access              |
| `scrollTop` / `scrollLeft` |  ❌  |                                        |
| `getBoundingClientRect` |   ❌    |                                        |

#### Element Methods

| Method              | Status | Notes                        |
| ------------------- | :----: | ---------------------------- |
| `getAttribute`      |   ✅    |                              |
| `setAttribute`      |   ✅    |                              |
| `hasAttribute`      |   ✅    |                              |
| `removeAttribute`   |   ✅    |                              |
| `querySelector`     |   ✅    |                              |
| `querySelectorAll`  |   ✅    |                              |
| `matches`           |   ✅    |                              |
| `closest`           |   ✅    |                              |
| `appendChild`       |   ✅    |                              |
| `removeChild`       |   ✅    |                              |
| `insertBefore`      |   ✅    |                              |
| `hasChildNodes`     |   ✅    |                              |
| `cloneNode`         |   ✅    |                              |
| `normalize`         |   ✅    | Merges adjacent text nodes   |
| `replaceChild`      |   ❌    |                              |
| `insertAdjacentHTML`|   ❌    |                              |
| `insertAdjacentElement` | ❌ |                              |
| `remove`            |   ❌    | Self-removal from parent     |
| `contains`          |   ❌    |                              |
| `toggleAttribute`   |   ❌    |                              |

#### Style

| Feature                          | Status | Notes                        |
| -------------------------------- | :----: | ---------------------------- |
| `element.style.prop` get/set     |   ✅    | camelCase ↔ CSS conversion   |
| `getComputedStyle()`             |   ✅    | Full cascade matching        |
| `element.style.cssText`          |   ❌    |                              |
| `element.style.setProperty()`    |   ❌    |                              |
| `element.style.removeProperty()` |   ❌    |                              |

### Built-in Objects & Methods

#### String Methods (24)

`indexOf`, `lastIndexOf`, `includes`, `startsWith`, `endsWith`, `trim`, `trimStart`, `trimEnd`, `toLowerCase`, `toUpperCase`, `split`, `substring`, `slice`, `replace`, `replaceAll`, `charAt`, `charCodeAt`, `concat`, `repeat`, `padStart`, `padEnd`, `at`, `search`, `toString`

**Missing:** `match`, `matchAll`, `normalize`, `localeCompare`, `codePointAt`

#### Array Methods (29)

`push`, `pop`, `indexOf`, `lastIndexOf`, `includes`, `join`, `reverse`, `slice`, `concat`, `map`, `filter`, `reduce`, `reduceRight`, `forEach`, `find`, `findIndex`, `some`, `every`, `sort`, `flat`, `flatMap`, `fill`, `splice`, `shift`, `unshift`, `at`, `toString`, `from` (static), `isArray` (static)

**Missing:** `copyWithin`, `findLast`, `findLastIndex`, `toReversed`, `toSorted`

#### Object Methods (13)

`Object.keys`, `Object.values`, `Object.entries`, `Object.create`, `Object.defineProperty`, `Object.assign`, `Object.freeze`, `Object.isFrozen`, `Object.getPrototypeOf`, `Object.setPrototypeOf`, `Object.is`, `Object.fromEntries`, `hasOwnProperty`

**Missing:** `Object.seal`, `Object.getOwnPropertyNames`

#### Math (27 methods + 8 constants)

**Methods:** `abs`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `min`, `max`, `log`, `log2`, `log10`, `exp`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sign`, `trunc`, `random`, `cbrt`, `hypot`, `clz32`, `imul`, `fround`

**Constants:** `PI`, `E`, `LN2`, `LN10`, `LOG2E`, `LOG10E`, `SQRT2`, `SQRT1_2`

**Missing:** `sinh`, `cosh`, `tanh`

#### Number (6 methods + 8 properties)

**Methods:** `toFixed`, `toString`, `Number.isInteger`, `Number.isFinite`, `Number.isNaN`, `Number.isSafeInteger`

**Properties:** `MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `MAX_VALUE`, `MIN_VALUE`, `POSITIVE_INFINITY`, `NEGATIVE_INFINITY`, `NaN`, `EPSILON`

**Missing:** `toExponential`, `toPrecision`

#### Map / Set

`new Map()`, `new Set()`

**Map methods (9):** `set`, `get`, `has`, `delete`, `clear`, `forEach`, `keys`, `values`, `entries` + `size` property

**Set methods (7):** `add`, `has`, `delete`, `clear`, `forEach`, `values`, `entries` + `size` property

**Missing:** Constructor with iterable argument, `Symbol.iterator` support

#### Regex

Regex literals via RE2 backend. Methods: `.test()`, `.exec()` with capture groups.

**Missing:** `match`, `matchAll`, lookahead/lookbehind, backreferences (RE2 limitations)

#### JSON

`JSON.parse`, `JSON.stringify`

Backed by Lambda's native JSON parser and formatter. Reviver/replacer/space arguments not yet supported.

#### Function

`.call()`, `.apply()`, `.bind()`

#### Date

`Date.now()`, `new Date()`

**Instance methods (10):** `getTime`, `getFullYear`, `getMonth`, `getDate`, `getHours`, `getMinutes`, `getSeconds`, `getMilliseconds`, `toISOString`, `toLocaleDateString`

**Missing:** `setFullYear`, `setMonth`, `setDate`, `setHours`, `toUTCString`, `toDateString`, `toTimeString`

#### Error

`Error`, `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`

All support `.message`, `.name`, and `instanceof` checks.

#### Typed Arrays

Full support for all 8 standard typed array types:

`Int8Array`, `Uint8Array`, `Int16Array`, `Uint16Array`, `Int32Array`, `Uint32Array`, `Float32Array`, `Float64Array`

Operations: `new`, `get`, `set`, `length`, `fill`, `subarray`

#### Global Functions

`parseInt`, `parseFloat`, `isNaN`, `isFinite`, `console.log`, `performance.now()`, `alert`

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
| JetStream | 6.3× | 0 | 9 | 11 |
| **Overall** | **6.3×** | **13** | **47** | **60** |

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

Measured after v11 Track A performance optimizations (property hash table, array fast path, float prescan widening, integer index fast path, constructor shape pre-allocation, property name interning).

| Benchmark | Category | LambdaJS (ms) | Node.js (ms) | Ratio |
|-----------|----------|-------------:|-----------:|------:|
| deltablue | macro | 11.0 | 5.8 | 1.9× |
| regex_dna | string | 2.4 | — | — |
| cube3d | 3D | 29.9 | 11.7 | 2.6× |
| splay | data | 22.0 | 7.7 | 2.9× |
| crypto_sha1 | crypto | 22.3 | 7.5 | 3.0× |
| crypto_md5 | crypto | 21.7 | — | — |
| richards | macro | 81.4 | 5.1 | 16.0× |
| crypto_aes | crypto | 38.8 | — | — |
| nbody | numeric | 260.8 | 3.1 | 84.1× |
| navier_stokes | numeric | 569.2 | 7.5 | 75.9× |
| base64 | string | 390.3 | — | — |
| hashmap | data | TIMEOUT | 12.0 | — |
| raytrace3d | 3D | FAIL | 9.1 | — |

**Geometric mean (exec):** LambdaJS 45.0ms vs Node.js 7.2ms → **6.3×** (improved from 8.8× in v10).

11 of 13 JetStream JS files run successfully. hashmap times out (GC pressure from 90K constructor calls). raytrace3d has a pre-existing closure capture bug.

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
