# Lambda browser WASM build

**Verified:** 2026-10-06, Emscripten 6.0.11, Node 24.7.0 and Chromium
154.0.8037.57 on macOS arm64. Profile authority: **D7.1.7v2**;
working design: `vibe/Lambda_Design_Static_Modules.md` §13.4 (SM17).

## Build

`make lambda-wasm` builds the optimized browser profile from the explicit
`platforms.lambda-wasm` manifest in `build_lambda_config.json`. The driver is
`utils/build_wasm.py`; it does not use desktop archives or generated Lua.
It compiles 211 project sources and utf8proc, plus WASM archives for mpdecimal
and the existing, unmodified RE2. Other in-memory document codecs and the
schema validator remain; their removal was not approved by D7.1.7v2.

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
sysinfo, image codecs, libuv/schedulers, resource caches, SQLite, PDF and LaTeX
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
decimals, wide integers, Unicode/RE2, in-memory JSON, explicit UTC dates,
failed-submission recovery, capability rejection, reset and reinitialization.
This is a focused embedding suite, not a full interpreter corpus certification.

## Measured artifacts

Optimized Emscripten 6.0.11 build, 2026-10-06; gzip sizes use deterministic
compression of each artifact separately. These are download sizes, not the
runtime's linear-memory footprint.

| Artifact | Raw bytes | Gzip bytes |
|---|---:|---:|
| `lambda-wasm.wasm` | 2,146,648 | 839,355 |
| `lambda-wasm.mjs` | 14,626 | 4,288 |
| Total | 2,161,274 | 843,643 |

Node and Chromium each pass **134 checks**. Native REPL tests pass **40/40**;
the native REPL also returns 15 and 17 for retained `make_adder(10)` calls.
The new regression exposed and fixed a shared REPL defect: fragment finalization
was reading the previous module's AST index and missed nested captures. Each
fragment now owns and publishes its own analysis graph before finalization,
then appends admitted nodes to the session index (D4.6.2v2, D6.2.3, D8.2.4).

The latest full native baseline ran 6,240 tests: 6,239 passed and the LaTeX
package-diagnostics golden failed while separate package changes were in
progress. Its mismatch was the changed bibliography diagnostics, unrelated
to the WASM profile. The expected output was subsequently updated by that
work, but a focused rerun then encountered a new import/type error in the
actively edited bibliography package. That failure remains; its log is
`temp/wasm-profile/native-latex-rerun.log`. No package files or goldens were
changed by the WASM implementation.

A Node probe measured the module's linear-memory buffer at **21,889,024 bytes**
after loading and **106,889,216 bytes** (about 102 MiB) after initialization
and the first evaluation. This is addressable linear memory, not a process-RSS
measurement. The shared side-stack and allocator reserve budgets are retained;
reducing their eager WASM backing is a separate memory-footprint task.
