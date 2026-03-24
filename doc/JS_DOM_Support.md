# LambdaJS — Experimental JavaScript and DOM Support

LambdaJS is Lambda's built-in JavaScript JIT and browser DOM engine (~18K LOC). It transpiles JavaScript source code into MIR IR which is then compiled to native machine code, enabling near-native execution of JavaScript programs within the Lambda runtime.

> **Status:** Experimental. LambdaJS passes 60/60 JS baseline tests (677/677 total) and runs all 53/53 benchmarks across 5 suites. It covers ES6 classes, prototype-based OOP, closures, typed arrays, template literals, try/catch/finally, destructuring, regex via RE2, optional chaining, Map/Set collections, and error subclasses. 5-suite geometric mean: **2.2×** vs Node.js (V8) (JetStream standalone: **6.3×**).

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
| Destructuring — rest (`...rest`)                   |   ✅    | Object rest `{a, ...rest}` and array rest `[a, ...rest]` |
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
| `Symbol` API                                       |   ✅    | `Symbol()`, `Symbol.for()`, `Symbol.keyFor()`, well-known symbols |
| `Proxy` / `Reflect`                                |   ❌    |                                                          |
| `setTimeout` / `setInterval`                       |   ❌    |                                                          |
| `encodeURIComponent` / `decodeURIComponent`        |   ✅    | RFC 3986 percent-encoding via `lib/url.c`                |
| `structuredClone`                                  |   ❌    |                                                          |
| `globalThis`                                       |   ✅    | Singleton global object with `undefined`, `NaN`, `Infinity` |
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

| Method                   | Status | Notes                                           |
| ------------------------ | :----: | ----------------------------------------------- |
| `getElementById`         |   ✅    |                                                 |
| `getElementsByClassName` |   ✅    |                                                 |
| `getElementsByTagName`   |   ✅    |                                                 |
| `querySelector`          |   ✅    |                                                 |
| `querySelectorAll`       |   ✅    |                                                 |
| `createElement`          |   ✅    |                                                 |
| `createTextNode`         |   ✅    |                                                 |
| `createElementNS`        |   ✅    | Namespace ignored, delegates to `createElement` |
| `write` / `writeln`      |   ✅    | Simplified; appends text to body                |
| `normalize`              |   ✅    | Merges adjacent text nodes at root              |
| `createDocumentFragment` |   ✅    | Lightweight container; children transfer on append |
| `createComment`          |   ✅    | Creates comment DOM node                          |
| `importNode`             |   ✅    | Deep clone from external tree                     |
| `adoptNode`              |   ✅    | Detaches node from current parent                 |

#### Document Properties

| Property           | Status | Notes |
| ------------------ | :----: | ----- |
| `body`             |   ✅    |       |
| `documentElement`  |   ✅    |       |
| `head`             |   ✅    |       |
| `title`            |   ✅    |       |
| `cookie`           |   ❌    |       |
| `readyState`       |   ❌    |       |
| `URL` / `location` |   ✅    | `URL` returns href string; `location` object with `.href`, `.hostname`, `.pathname`, etc. |

#### Element Properties

| Property                   | Status | Notes                                                                       |
| -------------------------- | :----: | --------------------------------------------------------------------------- |
| `tagName`                  |   ✅    |                                                                             |
| `id`                       |   ✅    |                                                                             |
| `className`                |   ✅    |                                                                             |
| `textContent`              |   ✅    |                                                                             |
| `children`                 |   ✅    |                                                                             |
| `parentElement`            |   ✅    |                                                                             |
| `parentNode`               |   ✅    |                                                                             |
| `firstChild`               |   ✅    |                                                                             |
| `lastChild`                |   ✅    |                                                                             |
| `nextSibling`              |   ✅    |                                                                             |
| `previousSibling`          |   ✅    |                                                                             |
| `childNodes`               |   ✅    |                                                                             |
| `childElementCount`        |   ✅    |                                                                             |
| `nodeType`                 |   ✅    |                                                                             |
| `nodeName`                 |   ✅    | Tag name or `"#text"`                                                       |
| `nodeValue`                |   ✅    | Alias for text node `.data`                                                 |
| `firstElementChild`        |   ✅    | Skips text nodes                                                            |
| `lastElementChild`         |   ✅    | Skips text nodes                                                            |
| `nextElementSibling`       |   ✅    | Skips text nodes                                                            |
| `previousElementSibling`   |   ✅    | Skips text nodes                                                            |
| `innerHTML`                |   ✅    | Getter and setter; setter parses HTML fragment                              |
| `offsetWidth`              |   ✅    | Returns 0 (JS runs before layout)                                           |
| `offsetHeight`             |   ✅    | Returns 0 (JS runs before layout)                                           |
| `clientWidth`              |   ✅    | Returns 0 (JS runs before layout)                                           |
| `clientHeight`             |   ✅    | Returns 0 (JS runs before layout)                                           |
| `outerHTML`                |   ✅    | Getter only                                                                 |
| `classList`                |   ✅    | `add`, `remove`, `toggle`, `contains`, `item`, `replace`, `length`, `value` |
| `dataset`                  |   ✅    | `data-*` attribute proxy with camelCase ↔ kebab-case conversion             |
| `scrollTop` / `scrollLeft` |   ❌    |                                                                             |
| `getBoundingClientRect`    |   ❌    |                                                                             |

#### Element Methods

| Method                  | Status | Notes                      |
| ----------------------- | :----: | -------------------------- |
| `getAttribute`          |   ✅    |                            |
| `setAttribute`          |   ✅    |                            |
| `hasAttribute`          |   ✅    |                            |
| `removeAttribute`       |   ✅    |                            |
| `querySelector`         |   ✅    |                            |
| `querySelectorAll`      |   ✅    |                            |
| `matches`               |   ✅    |                            |
| `closest`               |   ✅    |                            |
| `appendChild`           |   ✅    |                            |
| `removeChild`           |   ✅    |                            |
| `insertBefore`          |   ✅    |                            |
| `hasChildNodes`         |   ✅    |                            |
| `cloneNode`             |   ✅    |                            |
| `normalize`             |   ✅    | Merges adjacent text nodes |
| `replaceChild`          |   ✅    |                            |
| `insertAdjacentHTML`    |   ✅    | Parses HTML fragment at position |
| `insertAdjacentElement` |   ✅    | Inserts element at position |
| `remove`                |   ✅    | Self-removal from parent   |
| `contains`              |   ✅    | Subtree containment check  |
| `toggleAttribute`       |   ✅    | Optional force parameter   |

#### Style

| Feature                          | Status | Notes                      |
| -------------------------------- | :----: | -------------------------- |
| `element.style.prop` get/set     |   ✅    | camelCase ↔ CSS conversion |
| `getComputedStyle()`             |   ✅    | Full cascade matching      |
| `element.style.cssText`          |   ✅    | Getter only                |
| `element.style.setProperty()`    |   ✅    | Sets inline style property  |
| `element.style.removeProperty()` |   ✅    | Removes inline style property |

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

`parseInt`, `parseFloat`, `isNaN`, `isFinite`, `console.log`, `performance.now()`, `alert`, `encodeURIComponent`, `decodeURIComponent`, `globalThis`

---

## Benchmark Results — LambdaJS vs Node.js (V8)

**Platform:** Apple Silicon MacBook Air (M4), macOS  
**Node.js:** v22.13.0 (V8 JIT with TurboFan optimizing compiler)  
**Methodology:** 3 runs per benchmark, median of self-reported execution time  
**Optimizations applied:** P1 (return type propagation), P2 (bump-pointer alloc), P3 (direct ctor stores), P4 (typed instance property reads), P5 (module var arithmetic), P6 (function inlining), P7 (native method dispatch)

> Ratio = LambdaJS / Node.js. Values < 1.0× mean LambdaJS is faster.

### Summary by Suite

| Suite | Geo. Mean | LJS Wins | Node Wins | Total |
|-------|----------:|:--------:|:---------:|:-----:|
| R7RS | 1.3× | 5 | 5 | 10 |
| AWFY | 4.7× | 2 | 12 | 14 |
| BENG | 0.3× | 6 | 4 | 10 |
| KOSTYA | 6.3× | 0 | 7 | 7 |
| LARCENY | 3.8× | 2 | 10 | 12 |
| JetStream | 6.3× | 0 | 9 | 11 |
| **Overall (5 suites)** | **2.2×** | **15** | **38** | **53** |

### Where LambdaJS Beats Node.js (15 benchmarks)

| Benchmark | Suite | LambdaJS | Node.js | Ratio |
|-----------|-------|----------:|--------:|------:|
| revcomp | BENG | 0.001ms | 3.4ms | **0.0003×** |
| knucleotide | BENG | 0.016ms | 5.0ms | **0.003×** |
| pidigits | BENG | 0.015ms | 2.0ms | **0.008×** |
| json | AWFY | 0.167ms | 2.8ms | **0.06×** |
| divrec | LARCENY | 0.823ms | 7.9ms | **0.10×** |
| regexredux | BENG | 0.305ms | 2.5ms | **0.12×** |
| fannkuch | BENG | 0.529ms | 4.1ms | **0.13×** |
| tak | R7RS | 0.117ms | 0.80ms | **0.15×** |
| fasta | BENG | 1.21ms | 6.2ms | **0.20×** |
| cpstak | R7RS | 0.233ms | 1.00ms | **0.23×** |
| array1 | LARCENY | 0.580ms | 1.8ms | **0.31×** |
| sieve | AWFY | 0.136ms | 0.376ms | **0.36×** |
| fib | R7RS | 1.20ms | 2.0ms | **0.60×** |
| ack | R7RS | 9.33ms | 13.7ms | **0.68×** |
| fibfp | R7RS | 1.25ms | 1.77ms | **0.70×** |

**Strengths:**
- Delegating to Lambda's native engines (revcomp, knucleotide, regexredux, pidigits) bypasses JS-level overhead entirely.
- Simple recursive functions (tak, cpstak, fib, ack): MIR JIT generates efficient code for monomorphic call sites.
- Small array/permutation code (divrec, array1, fannkuch, sieve): tight-loop compilation with P5/P6 inlining.
- AWFY json and sieve added as wins after P3/P4/P5 property-access and module-var optimizations.

### Where Node.js is Faster

| Tier | Count | Key Bottleneck |
|------|------:|----------------|
| Comparable (1–2×) | 8 | nqueens (1.0×), fft (1.6×), primes/larceny (1.7×), primes/kostya (1.8×), paraffins (1.2×), fasta/fib/fibfp |
| Node 2–5× faster | 10 | json_gen (2.5×), storage (2.8×), bounce (3.2×), ray (3.2×), list (3.3×), levenshtein (3.5×), queens (3.6×), permute (3.6×), collatz (4.4×), towers (4.9×) |
| Node 5–20× faster | 12 | sumfp (5.3×), base64 (6.2×), binarytrees (7.2×), spectralnorm (7.5×), brainfuck (11×), deltablue (12×), deriv (13×), triangl (15×), richards (16×), havlak (18×), diviter (23×), quicksort (5.8×) |
| Node >20× faster | 8 | sum (18×), mbrot (10×), puzzle (4.6×), pnpoly (12×), gcbench (29×), nbody/beng (38×), mandelbrot/beng (54×), cd (54×), nbody/awfy (67×), matmul (83×) |

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
