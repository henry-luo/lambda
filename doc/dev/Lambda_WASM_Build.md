# Lambda browser WASM build

**Verified:** 2026-10-06, Emscripten 6.0.11, Node 24.7.0 and Chromium
154.0.8037.57 on macOS arm64. Profile authority: **D7.1.7v6**;
working design: `vibe/Lambda_Design_Static_Modules.md` §13.4 (SM17).

## Build

`make lambda-wasm` builds the optimized browser profile from the explicit
`platforms.lambda-wasm` manifest in `build_lambda_config.json`. The driver is
`utils/build_wasm.py`; it does not use desktop archives or generated Lua.
It compiles 189 project sources and utf8proc, plus WASM archives for mpdecimal
and the existing, unmodified RE2. Other in-memory document codecs and shared
type-validation code remain; further removals are proposals under D7.1.7v6.

`LAMBDA_NO_CSS_INPUT` removes stylesheet dispatch and inline CSS parsing.
`LAMBDA_NO_GRAPH_INPUT` removes graph dispatch and Structurizr include resolution.
The manifest excludes the CSS input adapter, engine/parser/tokenizer/value-parser,
font-face/paged-media parsing and selector matcher, plus all six graph input units.
HTML's four HTML5 units remain: document/fragment parsing, entities, raw style
text and attributes are preserved as Lambda data. CSS and graph `parse` requests
return explicit errors, including options-map graph flavors. Shared DOM/value
support remains for the retained HTML/Mark data path.

`LAMBDA_NO_CSS_FORMAT`, `LAMBDA_NO_GRAPH_FORMAT` and `LAMBDA_NO_MATH_FORMAT`
remove dispatch and six formatter units (CSS/graph, three math formatters and
the shared CSS formatter). Existing `LAMBDA_NO_LATEX` already excludes LaTeX
document parsing/formatting; `LAMBDA_NO_MATH_INPUT` makes the math-expression
input exclusion explicit. Unsupported string/options-map formats return errors;
JSON/HTML formatting and ordinary numerical computation remain.

`LAMBDA_NO_IMAGE_PROCESSING` excludes the 22 image kernels/pixel conversions
and their shared stencil/gather helpers from `lambda-vector.cpp`. The registry
retains signatures with NULL entry pointers, including `as_float`/`as_ubyte`;
these scale image samples rather than providing general numeric casts.
`LAMBDA_NO_EDIT_HISTORY` excludes public `undo`/`redo`/`edit_commit` wrappers;
`LAMBDA_NO_EMIT` excludes `emit`. Internal template transactions and `apply`
remain. The REPL now registers each admitted fragment's view declarations,
restores the registry on failed execution, and removes its module's entries and
context-owned reconciliation/state before AST teardown (D5.3.3).

No sources under `lambda/js/` enter this profile. The shared NamePool skips the
JS well-known-name catalog under `LAMBDA_NO_JS`; those spellings still work as
ordinary Lambda keys through normal interning (D4.6.1v3, D4.6.2v2).

`LAMBDA_NO_LOG` selects the disabled stub inside `lib/log.c`. The header strips
all logging levels and their arguments; the stub retains the callable ABI,
reports logging disabled, and has no formatting, configuration or IO providers.
`LAMBDA_NO_CONSOLE_DUMP` removes AST/value, CSS/DOM, version, memory, stack and
profile dump bodies and traversal callbacks. `LAMBDA_NO_CLI` removes the
validator's file-backed command and
console reports while retaining in-memory validation. Host-returned REPL values,
structured source diagnostics and explicit procedural `print` remain (D5.4.4).

`LAMBDA_NO_MEMTRACK` excludes `lib/memtrack.c`, tracker initialization, counters,
registries and fault injection. `lib/mem_alloc.c` supplies the lean allocation
provider and the string-allocation helpers shared with native builds. Zero-size,
checked multiplication/addition and failed-realloc ownership remain intact;
precise GC, Pool/Arena and MemContext ownership are retained (D4.2.5v3, D5.3.3).

`LAMBDA_NO_STR_SIMD` excludes `lib/str_simd.c` and its packed-pair preparation
and dispatch. `str.c` uses its existing scalar rare-byte search instead. WASM
[supports SIMD when enabled](https://emscripten.org/docs/porting/simd.html),
but this profile has no `-msimd128`, SSE2 or NEON flags; the excluded file was
using scalar fallback code. Unicode and ordinary string behavior remain.

Prerequisites are Git, CMake, Make and an activated Emscripten SDK. Initial SDK
installation needs Python 3.10 or newer. Downloads belong under `temp/`:

```sh
mkdir -p temp/wasm-toolchain
git clone https://github.com/emscripten-core/emsdk.git temp/wasm-toolchain/emsdk
python3 temp/wasm-toolchain/emsdk/emsdk.py install 6.0.11
python3 temp/wasm-toolchain/emsdk/emsdk.py activate 6.0.11
git clone --depth 1 --branch v2.12.0 https://github.com/JuliaStrings/utf8proc.git temp/wasm-toolchain/utf8proc
curl -L https://www.bytereef.org/software/mpdecimal/releases/mpdecimal-4.0.1.tar.gz -o temp/wasm-toolchain/mpdecimal-4.0.1.tar.gz
tar -xzf temp/wasm-toolchain/mpdecimal-4.0.1.tar.gz -C temp/wasm-toolchain
make lambda-wasm
```

The mpdecimal archive SHA-256 is
`96d33abb4bb0070c7be0fed4246cd38416188325f820468214471938545b1ac8`.
The driver discovers `emcc` from `EMCC`, PATH, or `EMSDK`, falling back to the
project SDK above. It uses the SDK's configured Python when available.
Compilation logs, objects and the retained-source manifest are in
`temp/wasm-build/`. Both dependency archives must be rebuilt when changing
compiler or ABI flags; remove `temp/wasm-build/re2` and run `make clean` inside
`temp/wasm-toolchain/mpdecimal-4.0.1/libmpdec` before such a rebuild.

Outputs are `build/wasm/lambda-wasm.mjs`, `lambda-wasm.wasm` and
`lambda-wasm.size.json`. The report records compiler identity, profile hash,
source manifest and raw/gzip bytes. Only a successful link publishes artifacts.

## ABI and lifetime

Lambda's pointer and Item layout requires 64-bit C pointers (D2.1.1,
D2.1.7). The build uses `-sMEMORY64=2`: LLVM compiles a 64-bit ABI and Binaryen
lowers the memory operations to wasm32. It therefore does not require browser
memory64 support. See [Emscripten's MEMORY64 setting](https://emscripten.org/docs/tools_reference/settings_reference.html#memory64).
The module uses `-Oz`, LTO, constructor evaluation, a 4 MiB C stack, growing
linear memory, no emulated filesystem and no dynamic JavaScript execution.
Modern WebAssembly and JavaScript BigInt are required.

Each module instance owns one synchronous REPL. Its C API is
`lambda/lambda-wasm.h`, implemented in `lambda/runtime/wasm_embed.cpp`:

| Function | Contract |
|---|---|
| `lambda_wasm_init()` | Create the session; return 1 on success, 0 on failure. Repeated initialization preserves the session. |
| `lambda_wasm_eval(source)` | Submit UTF-8 source; return printed Lambda value text, including `error` for rejected/failed evaluation. NULL means invalid argument or initialization failure. |
| `lambda_wasm_reset()` | Destroy retained bindings and start a fresh session; return 1 on success. |
| `lambda_wasm_shutdown()` | Destroy the session and runtime owners. |

Returned C text is borrowed until the next eval/reset/shutdown. `ccall` with
return type `string` copies it into a JavaScript string before that boundary.
Tagged Items and raw GC objects never cross the JavaScript API. Diagnostics
arrive through the module's `print`/`printErr` callbacks. REPL source, bindings,
module state and precise roots follow D4.6.2v2 and D5.3.3. Native VM reservations
are replaced by eagerly backed linear-memory extents with checked bounds and
logical commit accounting; WASM cannot provide per-page native guard protection.

```js
import createLambda from './lambda-wasm.mjs';
// wasmBytes is supplied by the embedding application.
const lambda = await createLambda({wasmBinary: wasmBytes,
  print: console.log, printErr: console.error});
lambda.ccall('lambda_wasm_init', 'number', [], []);
const evaluate = source => lambda.ccall('lambda_wasm_eval', 'string', ['string'], [source]);
evaluate('let x = 41');
console.log(evaluate('x + 1')); // "42"
lambda.ccall('lambda_wasm_shutdown', null, [], []);
```

The explicit incoming-module API includes `wasmBinary`; it is not in the SDK's
default list. See [INCOMING_MODULE_JS_API](https://emscripten.org/docs/tools_reference/settings_reference.html#incoming-module-js-api).
Without supplied bytes, the loader acquires its own `.wasm` artifact. This
bootstrap is host transport and cannot be invoked through Lambda evaluation.

## Capabilities and verification

MIR, Radiant, LambdaJS, Jube, CLI/terminal code, network/file providers,
sysinfo, image codecs, libuv/schedulers, resource caches, SQLite, PDF, LaTeX,
CSS/graph/math input parsers, image-processing kernels and CSS/graph/LaTeX/math
formatters
are absent from the source/dependency manifest. The shared builtin registry
retains signatures while excluded entry pointers are NULL. Unsupported
operations reject at AST admission or return an explicit error at invocation.
External/package imports are rejected; built-in `math`/`io` namespaces remain
subject to the same capability restrictions. There is no host module-map API.

RE2's legacy C++ logging retains unused locale facets. `utils/wasm_host.js`
implements their capability boundary: environment requests return the WASI
`ENOSYS` error, and timezone/timer requests throw explicit exclusion errors.
No environment variables, clock values or timezone values are invented, and
no browser ambient providers are linked. Console stdio imports remain for
diagnostics; there is no filesystem backend. Explicit datetime conversion
uses pure civil-calendar arithmetic. Profiling and wall-clock validation/GC
timing are compiled out; ordinary depth/allocation checks remain.

```sh
make test-wasm
CHROME_HEADLESS_SHELL=/path/to/chrome-headless-shell make test-wasm ARGS=--browser
```

The browser test also requires the project's Puppeteer dependency. It supplies
the loader through a data URL and WASM bytes directly, then denies fetch, XHR,
WebSocket, Date, Intl timezone formatting, performance clocks and entropy.
It covers retained functions/closures and arrays across allocation pressure,
decimals, wide integers, Unicode/RE2, ordinary keys matching JS catalog spellings,
in-memory JSON, HTML documents/fragments and inert style/resource attributes,
CSS/graph/math parser and formatter rejection, all image-processing builtin
admission exclusions, retained `apply`/stateful templates with rollback/reset,
explicit UTC dates,
failed-submission recovery, capability rejection, reset and reinitialization.
This is a focused embedding suite, not a full interpreter corpus certification.

## Measured artifacts

Optimized Emscripten 6.0.11 build, 2026-10-06; gzip sizes use deterministic
compression of each artifact separately. These are download sizes, not the
runtime's linear-memory footprint.

| Artifact | Raw bytes | Gzip bytes |
|---|---:|---:|
| `lambda-wasm.wasm` | 1,876,371 | 725,561 |
| `lambda-wasm.mjs` | 15,034 | 4,313 |
| Total | 1,891,405 | 729,874 |

Node and Chromium each pass **283 checks**. An additional link with function
names verifies that excluded parser/formatter, image-processing and public
history/event entry points are absent while HTML parsing and `apply` remain.
Native REPL tests pass **41/41**;
focused native tracker/pool/arena/string suites pass **431/431**. The lean
allocator also passes overflow, zero-size, string-copy/join, failed-realloc
and scalar/folded string-search checks without linking the tracker or SIMD file.
The embedding suite exercises literal string search and replacement as well.

The native REPL also returns 15 and 17 for retained `make_adder(10)` calls.
The new regression exposed and fixed a shared REPL defect: fragment finalization
was reading the previous module's AST index and missed nested captures. Each
fragment now owns and publishes its own analysis graph before finalization,
then appends admitted nodes to the session index (D4.6.2v2, D6.2.3, D8.2.4).

After the builtin/formatter exclusions and REPL template-registration fix, the
full native Lambda baseline passes **6,243/6,243**. Its log is
`temp/wasm-profile/no-image-format-edit-native-baseline.log`. The previously observed
LaTeX package-diagnostics failure no longer occurs in this run. No package
files or goldens were changed by the WASM implementation.

A Node probe measured the module's linear-memory buffer at **21,757,952 bytes**
after loading and **106,692,608 bytes** (about 102 MiB) after initialization
and the first evaluation. This is addressable linear memory, not a process-RSS
measurement. The shared side-stack and allocator reserve budgets are retained;
reducing their eager WASM backing is a separate memory-footprint task.
