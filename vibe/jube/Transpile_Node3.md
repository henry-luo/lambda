# Transpile_Node3: Structural Enhancement for Node.js Official Test Compliance

## 1. Executive Summary

Lambda's Node.js layer passes **1,399 of 3,527** official parallel tests (39.7%). This proposal identifies the structural gaps blocking the remaining ~2,128 tests and defines a phased plan targeting **1,900+ passes (54%+)** through six work areas ordered by impact-per-effort.

### Current Baseline Snapshot

| Metric | Count |
|--------|-------|
| Total official parallel tests | 3,527 |
| Baseline passing | **1,399** (39.7%) |
| Remaining failures | 2,128 |

### Per-Module Gap Analysis (top 20 by gap)

| Module | Total | Pass | Fail | Rate | Primary Blocker |
|--------|------:|-----:|-----:|-----:|-----------------|
| http2 | 268 | 0 | 268 | 0% | Unimplemented protocol |
| stream | 215 | 46 | 169 | 21% | Incomplete Readable/Writable internals |
| tls | 214 | 54 | 160 | 25% | Async + certificate API gaps |
| worker | 138 | 27 | 111 | 20% | No threading model |
| http | 387 | 277 | 110 | 72% | Async event patterns |
| crypto | 121 | 14 | 107 | 12% | Missing cipher/sign/DH/keygen |
| fs | 251 | 157 | 94 | 63% | watch/stream/promises gaps |
| child | 108 | 24 | 84 | 22% | fork/IPC/signal handling |
| repl | 104 | 28 | 76 | 27% | No REPL implementation |
| dgram | 75 | 0 | 75 | 0% | Unimplemented UDP |
| vm | 95 | 28 | 67 | 29% | Sandbox isolation, Script class |
| cluster | 83 | 11 | 72 | 13% | No multi-process |
| diagnostics | 67 | 0 | 67 | 0% | No diagnostics_channel |
| net | 148 | 82 | 66 | 55% | Async server/socket patterns |
| process | 93 | 34 | 59 | 37% | Missing APIs + error codes |
| zlib | 61 | 7 | 54 | 11% | Async streaming + constructor classes |
| whatwg | 62 | 10 | 52 | 16% | URL/encoding edge cases |
| async | 55 | 11 | 44 | 20% | AsyncLocalStorage/hooks |
| buffer | 68 | 25 | 43 | 37% | Missing methods + error codes |
| timers | 57 | 17 | 40 | 30% | Async ordering |

---

## 2. Root Cause Analysis

Sampling failures across modules reveals **five structural patterns** that account for the vast majority:

### Pattern A: Missing Node.js Error Codes (~30% of failures)

Lambda's runtime throws generic `TypeError` / `RangeError` without Node.js error codes. Tests validate:

```js
assert.throws(() => Buffer.alloc(-1), {
  code: 'ERR_INVALID_ARG_VALUE',     // Lambda: missing
  name: 'RangeError',
  message: /must be a non-negative number/
});
```

**Impact**: Cuts across buffer, fs, path, stream, process, crypto, zlib, util, assert — nearly every module.

**Current state**: `js_throw_type_error_code()` / `js_throw_range_error_code()` exist in `js_runtime.h` but are used in only ~36 call sites (30 in buffer, 4 in globals, 2 in fs). Hundreds of `js_throw_type_error(msg)` calls throughout the runtime omit the code.

### Pattern B: Async Streaming API Gaps (~25% of failures)

Node.js stream tests require:
- Constructor classes (`new Readable()`, `new Writable()`, `new Transform()`) with options (`objectMode`, `highWaterMark`, `read()`, `write()`, `transform()`)
- Event protocol (`data`, `end`, `error`, `close`, `drain`, `finish`, `pipe`, `unpipe`)
- Internal state management (`_readableState`, `_writableState`, `destroyed`, `readable`, `writable`)
- Static utilities (`Readable.from()`, `stream.addAbortSignal()`, `stream.compose()`, `stream.pipeline()`, `stream.finished()`)
- Async iteration (`for await (const chunk of readable)`)
- Backpressure mechanics (`write()` returning false → `drain` event)

Lambda's `js_stream.cpp` (848 LOC) provides basic class stubs but lacks the event-driven internal state machine that Node.js streams require. This is the deepest structural gap.

### Pattern C: Missing crypto Primitives (~12% of failures)

Lambda's crypto (1,753 LOC) covers hashing (SHA/HMAC) and random. Node.js tests require:

| Missing API | Tests Affected |
|-------------|---------------|
| `createCipheriv` / `createDecipheriv` | ~20 (AES-CBC, AES-GCM, etc.) |
| `createSign` / `createVerify` | ~15 (RSA, ECDSA) |
| `createDiffieHellman` / `ECDH` | ~20 (key exchange) |
| `generateKeyPair` / `generateKeyPairSync` | ~10 (async keygen) |
| `createSecretKey` / `createPublicKey` / `createPrivateKey` | ~10 (KeyObject) |
| `pbkdf2` / `pbkdf2Sync` / `scrypt` / `scryptSync` | ~8 (KDF) |
| `getCiphers()`, `getFips()` | ~5 (feature queries) |
| `X509Certificate` class | ~5 (certificate parsing) |
| `crypto.subtle` (WebCrypto) | ~32 tests in webcrypto prefix |

Additionally, several crypto tests **crash** Lambda (exit 139) — indicating null pointer dereferences in the mbedTLS integration when encountering unsupported operations.

### Pattern D: `common` Test Harness Limitations (~15% of failures)

The existing Lambda test shim (`lambda/js/test_shim/common_index.js`) provides `mustCall`, `mustNotCall`, platform detection, etc. But several `common` sub-modules are missing or incomplete:

| Sub-module | Impact | Status |
|------------|--------|--------|
| `common/index.js` | Base | ✅ Shimmed |
| `common/tmpdir` | 187 tests | ✅ Shimmed |
| `common/fixtures` | 76 tests | ✅ Shimmed |
| `common/internet` | ~20 tests | ❌ Missing (skip tests requiring internet) |
| `common/wpt` | ~40 tests | ❌ Missing (WPT harness) |
| `internal/test/binding` | ~40 tests | ❌ Missing (`internalBinding` stub) |

The `mustCall` implementation is also overly lenient — it doesn't fail when async callbacks genuinely never fire, which masks real issues.

### Pattern E: Module Subpath Resolution Gaps (~8% of failures)

Tests import sub-paths that Lambda's module dispatcher doesn't handle:

```js
const { pipeline } = require('stream/promises');  // → null in some codepaths
const dns = require('dns/promises');                // → not registered
const { Readable } = require('stream');             // works, but missing .from()
```

The `js_module_get()` dispatcher handles most sub-paths, but `dns/promises`, `fs/promises` (as a proper namespace with all methods), and some edge cases still return null.

---

## 3. Structural Enhancement Plan

### Phase 6: Error Code Infrastructure (Target: +80 tests)

**Goal**: Systematically attach Node.js `ERR_*` codes to all thrown errors.

#### 6.1 Error Code Registry

Create a centralized error code header:

```c
// js_error_codes.h
#define JS_ERR_INVALID_ARG_TYPE     "ERR_INVALID_ARG_TYPE"
#define JS_ERR_INVALID_ARG_VALUE    "ERR_INVALID_ARG_VALUE"
#define JS_ERR_OUT_OF_RANGE         "ERR_OUT_OF_RANGE"
#define JS_ERR_MISSING_ARGS         "ERR_MISSING_ARGS"
#define JS_ERR_BUFFER_TOO_LARGE     "ERR_BUFFER_TOO_LARGE"
#define JS_ERR_BUFFER_OUT_OF_BOUNDS "ERR_BUFFER_OUT_OF_BOUNDS"
#define JS_ERR_UNKNOWN_ENCODING     "ERR_UNKNOWN_ENCODING"
#define JS_ERR_STREAM_DESTROYED     "ERR_STREAM_DESTROYED"
#define JS_ERR_STREAM_PUSH_AFTER_EOF "ERR_STREAM_PUSH_AFTER_EOF"
#define JS_ERR_STREAM_NULL_VALUES   "ERR_STREAM_NULL_VALUES"
#define JS_ERR_INVALID_RETURN_VALUE "ERR_INVALID_RETURN_VALUE"
#define JS_ERR_INVALID_THIS         "ERR_INVALID_THIS"
#define JS_ERR_ASSERTION            "ERR_ASSERTION"
// ... 40+ codes total
```

#### 6.2 Migration Strategy

Replace `js_throw_type_error(msg)` with `js_throw_type_error_code(code, msg)` across all modules. Priority order:

1. **buffer** (already 30 code uses — complete the remaining ~20 sites)
2. **fs** (argument validation, encoding validation, ENOENT → ERR_INVALID_ARG_TYPE)
3. **stream** (push/write/read validation)
4. **process** (env, argv, exit validation)
5. **path** (argument type validation)
6. **crypto** (algorithm/key validation)
7. **util** / **assert** / **zlib** / **dns** (remaining modules)

#### 6.3 Error Message Format

Node.js tests validate exact error message patterns:

```js
// Node.js standard format:
'The "path" argument must be of type string. Received type number'
'The "size" argument must be a non-negative number. Received -1'
```

Add a helper:

```c
// js_error_codes.c
Item js_throw_invalid_arg_type(const char* name, const char* expected, Item actual) {
    char msg[512];
    const char* actual_type = js_typeof_string(actual);
    snprintf(msg, sizeof(msg),
        "The \"%s\" argument must be of type %s. Received type %s", name, expected, actual_type);
    return js_throw_type_error_code(JS_ERR_INVALID_ARG_TYPE, msg);
}
```

**Estimated impact**: +80 tests (crosses all modules — buffer +10, fs +15, stream +10, process +10, path +5, crypto +5, util +5, assert +5, zlib +5, misc +10).

---

### Phase 7: Stream Internals Rebuild (Target: +100 tests)

**Goal**: Rewrite `js_stream.cpp` to implement the Node.js internal state machine for Readable/Writable/Transform streams.

This is the highest-effort, highest-reward work item. 169 failing stream tests + downstream impact on fs, zlib, http, net, child_process.

#### 7.1 Readable Stream State Machine

```c
struct ReadableState {
    int highWaterMark;          // backpressure threshold (default 16384)
    int length;                 // bytes currently buffered
    bool flowing;               // null | true | false (three states)
    bool reading;               // currently in _read() call
    bool ended;                 // source exhausted (push(null) called)
    bool endEmitted;            // 'end' event has been emitted
    bool destroyed;             // destroy() called
    bool errorEmitted;
    bool objectMode;            // chunks are objects, not buffers
    bool needReadable;          // should emit 'readable' on next push
    bool emittedReadable;
    bool readableListening;     // has 'readable' listener
    int readingMore;            // _read() re-entry guard
    // ... buffer as linked list or ArrayList
};
```

Key behaviors to implement:
- **Flow modes**: `null` (initial) → `true` (flowing, emits `data`) → `false` (paused, buffers)
- **Transition triggers**: Adding `data` listener → flowing; `pause()` → paused; `resume()` → flowing
- **Backpressure**: When `length >= highWaterMark`, `push()` returns false
- **End sequence**: `push(null)` → drain buffer → emit `end` → emit `close`

#### 7.2 Writable Stream State Machine

```c
struct WritableState {
    int highWaterMark;
    bool writing;               // currently in _write() call
    bool ended;                 // end() called
    bool finished;              // 'finish' emitted
    bool destroyed;
    bool needDrain;             // emit 'drain' when current write completes
    bool objectMode;
    int bufferedRequest;        // pending writes in queue
    // ... write queue
};
```

Key behaviors:
- **Write buffering**: When `writing`, queue subsequent writes
- **Drain signal**: `write()` returns false when buffer exceeds highWaterMark; emit `drain` when buffer drops
- **Final flush**: `end()` → wait for writes → `_final()` → emit `finish` → emit `close`

#### 7.3 Transform / Duplex / PassThrough

- **Transform**: Extends Duplex with `_transform(chunk, encoding, callback)` + `_flush(callback)`
- **Duplex**: Merges Readable + Writable state (shared or independent highWaterMark)
- **PassThrough**: Transform that passes data through unmodified

#### 7.4 Static Utilities

| API | Description |
|-----|-------------|
| `Readable.from(iterable)` | Create Readable from sync/async iterable |
| `stream.pipeline(src, ...transforms, dst, cb)` | Connect streams with error propagation + cleanup |
| `stream.finished(stream, cb)` | Callback when stream is done (end/finish/error/close) |
| `stream.addAbortSignal(signal, stream)` | Destroy stream on AbortSignal |
| `stream.compose(...streams)` | Compose streams into a single Duplex |
| `stream.Readable.toWeb(readable)` | Convert to WHATWG ReadableStream |
| `stream.promises.pipeline()` | Promise-returning pipeline |
| `stream.promises.finished()` | Promise-returning finished |

#### 7.5 Async Iteration Protocol

Readable streams must support `Symbol.asyncIterator`:

```js
for await (const chunk of readable) { ... }
```

Implement as an async generator that reads until end/error.

#### 7.6 Implementation Approach

Estimate: ~2,500 LOC rewrite of `js_stream.cpp` (currently 848 LOC).

The new implementation should model Node.js's actual `lib/internal/streams/readable.js` and `writable.js` behavior rather than approximating with stubs. The state machine transitions and event sequencing must match Node.js exactly for tests to pass.

**Estimated impact**: +100 stream tests directly, +20–30 tests in fs/zlib/http/net that depend on proper stream behavior.

---

### Phase 8: Crypto API Expansion (Target: +60 tests)

**Goal**: Implement the most-tested crypto operations using mbedTLS.

#### 8.1 Priority API Surface

Ordered by test coverage impact:

| Priority | API | Tests | mbedTLS Backend |
|----------|-----|------:|-----------------|
| P1 | `createCipheriv` / `createDecipheriv` | ~20 | `mbedtls_cipher_*` |
| P1 | `pbkdf2` / `pbkdf2Sync` | ~5 | `mbedtls_pkcs5_pbkdf2_hmac` |
| P1 | `scrypt` / `scryptSync` | ~3 | Custom impl or libscrypt |
| P2 | `createSign` / `createVerify` | ~15 | `mbedtls_pk_sign` / `mbedtls_pk_verify` |
| P2 | `createDiffieHellman` / `ECDH` | ~10 | `mbedtls_dhm_*` / `mbedtls_ecdh_*` |
| P2 | `generateKeyPair` / `generateKeyPairSync` | ~10 | `mbedtls_pk_*` + `mbedtls_rsa_gen_key` |
| P3 | `createSecretKey` / `createPublicKey` / `createPrivateKey` | ~8 | `mbedtls_pk_parse_*` |
| P3 | `getCiphers()`, `getHashes()`, `getFips()` | ~5 | Static lists |
| P3 | `X509Certificate` class | ~5 | `mbedtls_x509_crt_*` |

#### 8.2 Cipher Streaming Model

`createCipheriv` returns a Transform stream. This depends on Phase 7 (stream rebuild):

```
createCipheriv(algorithm, key, iv)
  → CipherStream extends Transform
    → _transform(chunk) calls mbedtls_cipher_update()
    → _flush() calls mbedtls_cipher_finish()
    → .final() returns last block
    → .update(data) returns intermediate blocks
```

#### 8.3 Crash Fixes (P0)

Before expanding APIs, fix the ~6 crypto crashers:
- Guard null key pointers in `mbedtls_pk_*` calls
- Bounds-check key sizes before passing to mbedTLS
- Return proper error codes instead of dereferencing null results
- Add `try/catch` around mbedTLS calls that may fail with bad input

**Estimated impact**: +60 tests (cipher +20, sign/verify +15, DH/keygen +10, KDF +8, feature queries +5, crash fixes +2).

---

### Phase 9: Module Resolution & Subpath Completion (Target: +50 tests)

#### 9.1 `dns/promises` Namespace

Register `dns/promises` in `js_module_get()`:

```cpp
// node:dns/promises
if (match("dns/promises") || match("node:dns/promises")) {
    return js_get_dns_promises_namespace();  // lookup, resolve, Resolver
}
```

Implement as promise-wrapping `dns.lookup()` and `dns.resolve()`.

#### 9.2 `fs/promises` Full Namespace

Lambda already passes some `fs/promises` tests. Ensure all async methods are available:

- `open()` → `FileHandle` class with `read()`, `write()`, `close()`, `stat()`, `readFile()`, `writeFile()`
- `readFile()`, `writeFile()`, `appendFile()`, `copyFile()`, `rename()`, `unlink()`, `mkdir()`, `rmdir()`, `readdir()`, `stat()`, `access()`, `chmod()`, `chown()`
- `cp()` (recursive copy)

#### 9.3 `internal/test/binding` Stub

~40 tests import `require('internal/test/binding')` to access `internalBinding('uv')`. Provide a minimal stub:

```js
module.exports = {
  internalBinding(name) {
    if (name === 'uv') return {
      UV_ENOENT: -2, UV_EACCES: -13, UV_EBADF: -9,
      UV_EINVAL: -22, UV_EEXIST: -17, UV_ENOTEMPTY: -39,
      UV_ENOSYS: -38, UV_EPERM: -1, UV_EISDIR: -21,
      UV_UNKNOWN: -4094,
      errname: (code) => Object.entries(this).find(([k,v]) => v === code)?.[0] || 'UNKNOWN'
    };
    if (name === 'config') return { hasOpenSSL: true, hasCrypto: true, fipsMode: false };
    return {};
  }
};
```

Register in `js_module_get()` for the `internal/test/binding` specifier.

#### 9.4 `common/internet` Shim

Add a shim that provides `addresses` object (DNS test targets) and `skip()` for tests requiring real internet access. Most internet-dependent tests should skip gracefully.

#### 9.5 `common/wpt` Shim

The WPT (Web Platform Tests) harness adapter. ~40 `test-whatwg-*` tests depend on it. Provide a minimal shim that routes to Lambda's existing `assert` module.

**Estimated impact**: +50 tests (dns/promises +8, fs/promises +12, internalBinding +15, common/internet +5, common/wpt +10).

---

### Phase 10: Process & OS Completion (Target: +40 tests)

#### 10.1 Process API Gaps

| Missing API | Tests | Implementation |
|-------------|------:|----------------|
| `process.binding(name)` | ~8 | Return empty objects (deprecated API, tests check it exists) |
| `process.emitWarning(msg, type)` | ~5 | Emit `warning` event on process object |
| `process.allowedNodeEnvironmentFlags` | ~3 | Return Set of known flags |
| `process.setuid()` / `process.setgid()` | ~3 | `setuid()`/`setgid()` syscall (POSIX) |
| `process.report` | ~3 | Stub: `{ getReport() }` returning basic diagnostics |
| `process.execve()` | ~5 | Fix crash (null guard), implement via `execve()` syscall |
| `process.exit()` codes | ~3 | Validate exit code range (ERR_OUT_OF_RANGE) |

#### 10.2 `os.constants` Completion

```c
// os.constants.signals
os_constants_signals = {
    SIGHUP: 1, SIGINT: 2, SIGQUIT: 3, SIGILL: 4, SIGTRAP: 5,
    SIGABRT: 6, SIGBUS: 7, SIGFPE: 8, SIGKILL: 9, SIGUSR1: 10,
    SIGSEGV: 11, SIGUSR2: 12, SIGPIPE: 13, SIGALRM: 14, SIGTERM: 15,
    // ... platform-specific values from <signal.h>
};

// os.constants.errno
os_constants_errno = {
    E2BIG: 7, EACCES: 13, EADDRINUSE: 48, EADDRNOTAVAIL: 49,
    EBADF: 9, EBUSY: 16, EEXIST: 17, EINTR: 4, EINVAL: 22,
    EISDIR: 21, ENOENT: 2, ENOTEMPTY: 66, ENOSYS: 78, EPERM: 1,
    // ... from <errno.h>
};

// os.constants.priority
os_constants_priority = {
    PRIORITY_LOW: 19, PRIORITY_BELOW_NORMAL: 10, PRIORITY_NORMAL: 0,
    PRIORITY_ABOVE_NORMAL: -7, PRIORITY_HIGH: -14, PRIORITY_HIGHEST: -20
};
```

#### 10.3 Crash Fixes in process/os

- `test-os.js` crash — likely in `os.networkInterfaces()` or `os.cpus()` when encountering unexpected interface types
- `test-process-chdir.js` crash — null/invalid path guard
- `test-path-resolve.js` / `test-path-zero-length-strings.js` — empty string edge cases in path resolution

**Estimated impact**: +40 tests (process +25, os +10, path crash fixes +5).

---

### Phase 11: child_process & REPL (Target: +40 tests)

#### 11.1 child_process Enhancements

84 child_process tests fail. Major gaps:

| Gap | Tests | Description |
|-----|------:|-------------|
| `fork()` | ~15 | Spawn Node.js child process with IPC channel |
| Signal handling | ~10 | `child.kill(signal)`, `process.on('SIGTERM')` |
| `stdio` options | ~10 | `{ stdio: ['pipe', 'pipe', 'pipe'] }`, `'inherit'`, `'ignore'` |
| IPC channel | ~8 | `child.send(msg)`, `child.on('message')` |
| Shell option | ~5 | `exec({ shell: '/bin/bash' })` |
| Error codes | ~10 | `ERR_CHILD_PROCESS_*` codes on spawn failures |

**`fork()` implementation**: Since Lambda is `lambda.exe`, not `node`, `fork()` should spawn `lambda.exe js <script>` with IPC handled via pipes (not V8 serialization).

#### 11.2 REPL Module Stub

104 REPL tests fail but most require a full interactive REPL which is out of scope. However, ~10 tests only check basic module exports:

```js
const repl = require('repl');
assert.strictEqual(typeof repl.start, 'function');
assert.strictEqual(typeof repl.REPLServer, 'function');
```

A minimal stub providing `start()`, `REPLServer` constructor, and `REPL_MODE_*` constants would pass these.

**Estimated impact**: +40 tests (child_process +30 with fork/signal/stdio, repl stub +10).

---

### Phase 12: zlib Streaming & Constructors (Target: +35 tests)

#### 12.1 Streaming Constructors

54 zlib tests fail. The primary gap is that zlib tests instantiate streaming classes:

```js
const gzip = zlib.createGzip();        // → Transform stream
const deflate = zlib.createDeflate();  // → Transform stream
stream.pipe(gzip).pipe(output);        // pipe chain
```

These depend on Phase 7 (stream rebuild). Each zlib class is a Transform stream wrapping zlib C functions:

| Class | Transform | Underlying |
|-------|-----------|------------|
| `Gzip` / `createGzip` | Compress | `deflateInit2()` + `deflate()` |
| `Gunzip` / `createGunzip` | Decompress | `inflateInit2()` + `inflate()` |
| `Deflate` / `createDeflate` | Compress | `deflateInit()` + `deflate()` |
| `Inflate` / `createInflate` | Decompress | `inflateInit()` + `inflate()` |
| `DeflateRaw` / `createDeflateRaw` | Compress (raw) | No zlib header |
| `InflateRaw` / `createInflateRaw` | Decompress (raw) | No zlib header |
| `BrotliCompress` / `createBrotliCompress` | Compress | Brotli encoder |
| `BrotliDecompress` / `createBrotliDecompress` | Decompress | Brotli decoder |
| `ZstdCompress` | Compress | Zstd encoder |
| `ZstdDecompress` | Decompress | Zstd decoder |

#### 12.2 `zlib.codes` Immutability

`test-zlib-const.js` checks that `zlib.codes` is frozen. Currently fails with "zlib.codes should be immutable". Add `Object.freeze()` on the codes/constants objects.

#### 12.3 Callback-style Convenience Methods

Many zlib tests use the callback form:

```js
zlib.gzip(input, (err, result) => { ... });
```

Lambda implements `gzipSync` but needs the async wrapper that calls the sync version and invokes the callback via `process.nextTick`.

**Estimated impact**: +35 tests (streaming classes +20, callback wrappers +10, constants/codes +5). Requires Phase 7 completion.

---

### Phase 13: Buffer & util Hardening (Target: +30 tests)

#### 13.1 Buffer Missing Methods

| Method | Tests | Description |
|--------|------:|-------------|
| `Buffer.isAscii(input)` | ~3 | Check if all bytes are ASCII (<128) |
| `Buffer.isUtf8(input)` | ~3 | Validate UTF-8 encoding |
| `Buffer.copyBytesFrom(view, offset, length)` | ~2 | Copy from TypedArray |
| `Buffer.of(...items)` | ~1 | Create from variadic args |
| `SlowBuffer(size)` | ~2 | Deprecated, alias to `Buffer.allocUnsafeSlow` |
| `buf[Symbol.iterator]` | ~2 | Iteration protocol |
| `buf.readBigInt64BE/LE` / `writeBigInt64BE/LE` | ~2 | 64-bit integer R/W |
| `Buffer.poolSize` | ~1 | Static property (default 8192) |

#### 13.2 Buffer Error Code Completion

30 call sites already use `js_throw_type_error_code`; complete the remaining ~20 sites. Match Node.js error message format exactly:
- `"The value of \"offset\" is out of range. It must be >= 0 and <= N. Received M"`
- `"The \"value\" argument must be of type number. Received type string"`

#### 13.3 util.inspect Robustness

| Gap | Tests | Fix |
|-----|------:|-----|
| Circular reference detection | ~3 | Track seen objects set, emit `[Circular *N]` |
| `[util.inspect.custom]` symbol | ~2 | Check for custom inspect method on objects |
| Proxy object inspection | ~2 | Detect Proxy, show handler traps |
| `showHidden` option | ~2 | Enumerate non-enumerable properties |
| `depth` option (null = unlimited) | ~2 | Recursive depth control |
| `colors` / `stylize` option | ~1 | ANSI color codes |
| `util.inspect.defaultOptions` | ~1 | Global default configuration |

#### 13.4 assert Module Enhancements

| Gap | Tests | Fix |
|-----|------:|-----|
| `assert.throws()` error code matching | ~3 | Match `{ code, name, message }` properties |
| `assert.rejects()` | ~2 | Async promise rejection assertion |
| Node.js ERR_ASSERTION format | ~2 | `code: 'ERR_ASSERTION'` on AssertionError |
| `assert.CallTracker` | ~1 | Deprecated but tested |

**Estimated impact**: +30 tests (buffer +12, util +10, assert +8).

---

### Phase 14: Test Infrastructure Improvements (Target: +25 tests)

#### 14.1 `mustCall` Strictness

The current shim's `mustCall` only fails if at least one tracked function was partially invoked. This is too lenient — tests that never invoke any callbacks exit 0 falsely. Fix:

```js
// Track all mustCall registrations
// On process exit: fail if ANY mustCall with exact=N was never called at all
// Exception: mustCall(fn, 0) means "may be called 0 times" (valid)
```

This will cause some currently-"passing" tests to correctly fail, but will improve signal quality and reveal tests that are actually closer to passing.

#### 14.2 Additional Common Sub-module Shims

| Shim | Tests Unblocked |
|------|----------------|
| `common/internet` | ~20 (DNS/HTTP tests with internet targets) |
| `common/wpt` | ~40 (WHATWG URL/encoding/stream tests) |
| `common/dns` | ~10 (DNS test helpers) |
| `common/crypto` | ~15 (crypto test utilities: keys, certs) |
| `common/report` | ~5 (diagnostic report helpers) |

#### 14.3 Crash Protection in Test Runner

Add `SIGABRT`/`SIGSEGV` recovery in the test runner to classify crashes vs failures:
- Tag crashes in baseline as `# CRASH` comments
- Auto-retry crashes once (flaky signal handling)
- Report crash rate per module

#### 14.4 Per-Module Reporting

Add test runner output showing per-module pass/fail/crash:

```
Module          Total  Pass  Fail  Crash  Rate
──────────────────────────────────────────────
http             387   277   108     2   71.6%
fs               251   157    89     5   62.5%
stream           215    46   165     4   21.4%  ← priority
crypto           121    14   101     6   11.6%  ← priority
...
```

**Estimated impact**: +25 net new passes from shim improvements (offsetting some regressions from stricter mustCall).

---

## 4. Non-Goals

Explicitly out of scope:

| Module/Feature | Tests | Reason |
|----------------|------:|--------|
| `http2` | 268 | Full HTTP/2 protocol implementation (HPACK, multiplexing, server push) is disproportionate effort |
| `worker_threads` | 138 | Requires threading model; Lambda is single-threaded |
| `cluster` | 83 | Requires multi-process with shared sockets |
| `dgram` (UDP) | 75 | Niche usage, low ecosystem impact |
| `diagnostics_channel` (full) | 67 | Internal V8 diagnostics |
| `repl` (full) | ~94 | Interactive REPL with syntax highlighting, tab completion |
| Native addons / N-API | N/A | Requires V8 ABI compatibility |
| `fs.watch()` full | ~15 | Requires libuv/kqueue/inotify event loop integration |
| `inspector` protocol | ~2 | Chrome DevTools protocol (already 70/72 pass on stubs) |

**Total excluded**: ~825 tests (23% of total). These represent fundamental architecture mismatches rather than incremental API gaps.

---

## 5. Implementation Roadmap

### Phase Priority Matrix

| Phase | Work Area | Est. Tests | Effort | Dependencies | Priority |
|-------|-----------|-----------|--------|-------------|----------|
| **6** | Error code infrastructure | +80 | Medium | None | **P0** |
| **7** | Stream internals rebuild | +100 | High | None | **P0** |
| **8** | Crypto API expansion | +60 | High | Phase 7 (partial) | **P1** |
| **9** | Module resolution + stubs | +50 | Low | None | **P1** |
| **10** | Process & OS completion | +40 | Medium | Phase 6 | **P1** |
| **11** | child_process + REPL | +40 | Medium | None | **P2** |
| **12** | zlib streaming | +35 | Medium | Phase 7 | **P2** |
| **13** | Buffer & util hardening | +30 | Low | Phase 6 | **P2** |
| **14** | Test infrastructure | +25 | Low | None | **P2** |

### Projected Cumulative Results

| Milestone | Passes | Rate | Delta |
|-----------|-------:|-----:|------:|
| Current baseline | 1,399 | 39.7% | — |
| After Phase 6 (error codes) | 1,479 | 41.9% | +80 |
| After Phase 7 (streams) | 1,579 | 44.8% | +100 |
| After Phase 8 (crypto) | 1,639 | 46.5% | +60 |
| After Phase 9 (module resolution) | 1,689 | 47.9% | +50 |
| After Phase 10 (process/os) | 1,729 | 49.0% | +40 |
| After Phase 11 (child_process/repl) | 1,769 | 50.1% | +40 |
| After Phase 12 (zlib streaming) | 1,804 | 51.1% | +35 |
| After Phase 13 (buffer/util) | 1,834 | 52.0% | +30 |
| After Phase 14 (test infra) | 1,859 | 52.7% | +25 |
| **Stretch target** | **1,900+** | **54%+** | **+500** |

### Critical Path

```
Phase 6 (error codes) ──→ Phase 10 (process/os)
                      └──→ Phase 13 (buffer/util)

Phase 7 (streams)    ──→ Phase 8 (crypto ciphers as Transform)
                     └──→ Phase 12 (zlib streaming)

Phase 9 (module resolution) ── independent
Phase 11 (child_process)    ── independent
Phase 14 (test infra)       ── independent
```

Phases 6, 7, 9, 11, 14 can start in parallel. Phases 8, 10, 12, 13 have upstream dependencies.

---

## 6. Success Criteria

| Metric | Target |
|--------|--------|
| Baseline passes | ≥1,900 (54%+) |
| Zero regressions | Each phase must not decrease existing pass count |
| Zero new crashes | Each phase must fix, not add, crashers |
| Module rate ≥50% | fs, buffer, process, path, util, assert all ≥50% |
| Stream rate ≥35% | stream prefix: ≥75 passes (from 46) |
| Crypto rate ≥40% | crypto prefix: ≥50 passes (from 14) |

---

## 7. Appendix: Failure Samples

### Sample: Missing error code (buffer)
```
$ lambda.exe js test-buffer-alloc.js
# Expected: { code: 'ERR_INVALID_ARG_VALUE', name: 'RangeError' }
# Actual:   RangeError without .code property
```

### Sample: Stream internal state (stream)
```
$ lambda.exe js test-stream-destroy.js
# Uncaught TypeError: is not a function
# → stream.destroy() not implemented
```

### Sample: Missing crypto API (crypto)
```
$ lambda.exe js test-crypto-cipheriv-decipheriv.js
# js_map_method fallback: method 'getFips' not found
# → crypto.getFips() not implemented
```

### Sample: DNS module gap (dns)
```
$ lambda.exe js test-dns-lookup.js
# Uncaught TypeError: is not a function
# → dns.lookup() async callback form not working
```

### Sample: Process env (process)
```
$ lambda.exe js test-process-env.js
# AssertionError: values are not strictly equal
# → process.env property deletion/enumeration behavior differs
```
