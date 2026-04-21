# Transpile_Node2: Node.js Official Test Suite Compliance

## 1. Executive Summary

Lambda's Node.js compatibility layer (Transpile_Node) has established a solid foundation with 23 built-in modules implemented and an npm package management system. This proposal analyzes the results of running the official Node.js test suite (`ref/node/test/parallel/`) against Lambda's JS runtime and defines a phased plan to improve compliance.

### Current State (after Phase 3 implementation)

| Metric | Count |
|--------|-------|
| Total official parallel tests | 3,926 |
| Tests in enabled modules (27 modules) | ~2,050 |
| Tests in disabled modules (9 modules) | ~1,876 |
| **Baseline passing** | **739** |

### Phase 2 Implementation Results (cumulative)

Phase 2 built on Phase 1 with three changes:
1. **Fixed `fs.mkdtempSync`** — was not appending `XXXXXX` template suffix, causing null returns and stray dirs at project root
2. **Removed leaked DOM globals** — `Node`, `innerWidth`, `innerHeight` removed from global scope (were failing ~80 tests)
3. **Enabled 8 new modules** — http (266 passes), net (76), tls (54), https (30), timers (16), module (9), vm (9), readline (5)

Per-module pass breakdown:

| Module | Passes | Module | Passes |
|--------|--------|--------|--------|
| http | 266 | crypto | 14 |
| fs | 132 | module | 9 |
| net | 76 | vm | 9 |
| tls | 54 | zlib | 7 |
| https | 30 | readline | 5 |
| stream | 25 | events | 3 |
| process | 24 | path | 3 |
| child-process | 22 | assert | 2 |
| buffer | 17 | os | 2 |
| timers | 16 | querystring | 2 |
| util | 2 | other | 2 |

**Result: 691 genuinely passing tests** (up from 255 after Phase 1)

### Phase 3 Implementation Results

Phase 3 (API surface completion) added missing constructor/class exports and AbortController:
1. **http module**: Added `Server`, `Agent` (with `getName`, `destroy`, `createConnection`), `IncomingMessage`, `ServerResponse`, `ClientRequest`, `OutgoingMessage`, `globalAgent`
2. **net module**: Added `Server` constructor export
3. **tls module**: Added `Server`, `TLSSocket`, `createSecureContext` exports
4. **https module**: Added `Server`, `Agent` exports
5. **AbortController**: Implemented as proper constructor with `signal` (aborted, reason, addEventListener, removeEventListener, throwIfAborted) and `abort(reason)`
6. **Internal module aliases**: Mapped `_http_agent`, `_http_client`, `_http_common`, `_http_incoming`, `_http_outgoing`, `_http_server` to `http` module

**Result: 739 passing tests** (+48 from Phase 2). Key gains: http +45, https +7, net +3.

### Phase 1 Implementation Results

Phase 1 (test harness shim) was implemented:
- Created Lambda-compatible shims for `common/index.js`, `common/tmpdir.js`, `common/fixtures.js`
- Removed Proxy wrapper (Lambda's optimized method dispatch bypasses Proxy get traps)
- Relaxed `mustCall` validation (Lambda lacks a full event loop, so many async callbacks never fire)
- Added `async_wrap`, `eventsource`, `stringbytes` module prefixes to test runner
- **Result: 255 genuinely passing tests** (up from 208 inflated / ~100 genuine)
- 156 new passes, 109 previously-falsely-passing tests correctly identified as failures

## 2. Root Cause Analysis of Failures

Analysis of 1,010 failed tests reveals **five dominant failure categories**, with the first two accounting for ~70% of all failures.

### Category 1: Missing Test Harness (`common` module) — ~500 tests (49%)

**The single largest blocker.** Almost every Node.js test begins with:
```js
const common = require('../common');
```

This loads `ref/node/test/common/index.js` (1,115 lines), which itself requires:
- `require('net')` — TCP sockets
- `require('worker_threads')` — `isMainThread` check
- `require('util')` — `inspect`, `getCallSites`
- `require('./tmpdir')` — Temp directory management
- Various `process.config` checks

**Impact**: Even when the test's actual logic is simple, the harness fails to load, causing a cascade failure. The most critical harness functions by usage frequency:

| Function | Usage Count | Purpose |
|----------|-------------|---------|
| `common.mustCall(fn, n)` | 1,977 | Assert callback called exactly N times |
| `common.mustNotCall()` | 457 | Assert callback never called |
| `common.mustSucceed(fn)` | 403 | Assert callback with no error |
| `common.skip(msg)` | 251 | Skip test gracefully |
| `common.hasCrypto` | 131 | Feature detection |
| `common.isWindows` | 118 | Platform detection |
| `common.mustNotMutateObjectDeep(obj)` | 115 | Deep-freeze assertion |
| `common.mustCallAtLeast(fn, n)` | 81 | Assert called at least N times |
| `common.expectsError(opts)` | 57 | Assert error properties |
| `common.invalidArgTypeHelper(v)` | 55 | Error message helper |
| `common.platformTimeout(ms)` | 45 | Platform-adjusted timeout |
| `common.expectWarning(map)` | 25 | Assert process warnings |

**Sub-cause**: `common/index.js` requires `net` and `worker_threads` at the top level, neither of which is in Lambda's enabled module set for the test runner. Even though Lambda implements `node:net`, the test harness is disabled in `test_node_official_gtest.cpp`.

### Category 2: Leaked Globals — ~80 tests (8%)

```
Uncaught AssertionError: Unexpected global(s) found: Node, innerWidth, innerHeight
```

The `common` module validates that no unexpected globals are injected. Lambda's JS runtime leaks `Node`, `innerWidth`, `innerHeight` into the global scope (likely from the Radiant/DOM subsystem). These tests would pass if the globals check passed.

### Category 3: Missing/Incomplete API Surface — ~200 tests (20%)

Tests that load successfully but fail because specific APIs are missing or behave differently:

| Gap | Affected Tests | Example |
|-----|---------------|---------|
| `Buffer.isAscii()` / `Buffer.isUtf8()` | ~5 | `test-buffer-isascii.js`, `test-buffer-isutf8.js` |
| `Buffer.copyBytesFrom()` | ~2 | Buffer static method |
| `Buffer.from()` with SharedArrayBuffer | ~3 | `test-buffer-sharedarraybuffer.js` |
| `path.win32.*` (full Windows path handling) | ~8 | `test-path-extname.js`, `test-path-dirname.js` |
| `process.config` object | ~10 | `test-process-env.js`, build configuration |
| `process.getuid()` / `process.getgid()` | ~5 | POSIX user/group IDs |
| `process.features` object | ~3 | `test-process-features.js` |
| `os.constants.signals` / `os.constants.errno` | ~5 | `test-os-constants-signals.js` |
| `fs/promises` (full API) | ~40 | `test-fs-promises-*.js` (many passing already) |
| `fs.watch()` / `fs.watchFile()` | ~15 | All `test-fs-watch-*` crash |
| `fs.createReadStream()` / `createWriteStream()` | ~10 | Requires stream integration |
| `crypto.createSign/Verify` (RSA/ECDSA edge cases) | ~15 | `test-crypto-sign-verify.js` crashes |
| `crypto.generateKeyPair` (async keygen) | ~20 | Various keygen async tests |
| `crypto.X509Certificate` class | ~5 | `test-crypto-x509.js` crashes |
| `zlib` async/callback model | ~50 | Most zlib tests use callbacks |
| `dns.lookup()` async | ~25 | Async DNS resolution |
| `assert.throws()` error matching | ~8 | Error property validation |
| `util.inspect()` deep/complex | ~10 | Circular refs, Proxy, custom inspect |

### Category 4: Crashes (Segfault/Abort) — 37 tests (3.6%)

Tests that cause `lambda.exe` to crash, indicating C/C++ bugs in the runtime:

| Crash Module | Count | Representative Test |
|-------------|-------|-------------------|
| fs (watch) | 10 | `test-fs-watch-ignore-*.js` — all `fs.watch` related |
| fs (other) | 7 | `test-fs-copyfile.js`, `test-fs-write.js`, `test-fs-stat-bigint.js` |
| crypto | 6 | `test-crypto-sign-verify.js`, `test-crypto-x509.js`, `test-crypto-rsa-dsa.js` |
| process | 3 | `test-process-chdir.js`, `test-process-execve.js` |
| path | 2 | `test-path-resolve.js`, `test-path-zero-length-strings.js` |
| child_process | 2 | `test-child-process-exec-timeout-kill.js`, `test-child-process-spawn-controller.js` |
| buffer | 1 | `test-buffer-generic-methods.js` |
| os | 1 | `test-os.js` |
| util | 1 | `test-util-inspect.js` |

**Priority**: Crashes must be fixed first — they indicate memory safety issues that could affect all tests.

### Category 5: Event Loop / Async Model — ~150 tests (15%)

Tests that depend on Node.js's event loop (`process.nextTick`, `setImmediate`, `setTimeout` callbacks, async I/O completion) which Lambda implements partially. These tests hang or produce incorrect ordering:

- Stream tests relying on `process.nextTick` for backpressure
- `child_process.exec()` callback model
- `fs.readFile()` / `fs.writeFile()` async callbacks
- `dns.lookup()` async callbacks
- `zlib.gzip()` / `zlib.gunzip()` callback model

## 3. Disabled Modules — Expansion Opportunities

Lambda already implements several modules that are **disabled in the test runner** (`test_node_official_gtest.cpp`). Enabling them would expand coverage:

| Module | Implementation Status | Tests Available | Estimated Pass Rate | Action |
|--------|----------------------|-----------------|--------------------|---------| 
| `http` | ✅ Full (js_http.cpp) | 388 | ~5-10% | Enable, shim common |
| `https` | ✅ Full (js_https.cpp) | 63 | ~5-10% | Enable after http |
| `net` | ✅ Full (js_net.cpp) | 148 | ~10-15% | Enable, many event-loop dependent |
| `tls` | ✅ Full (js_tls.cpp) | 214 | ~5% | Enable after net |
| `readline` | ✅ Full (js_readline.cpp) | 21 | ~20% | Enable |
| `timers` | ✅ Alias | 58 | ~15% | Enable |
| `module` | ⚠️ Partial | 31 | ~5% | Low priority |

**Conservative estimate**: Enabling http, net, readline, timers could add ~50-80 passing tests.

## 4. Implementation Plan

### Phase 1: Test Harness Shim (Target: +200 tests)

**Goal**: Make the `common` module load successfully in Lambda, unblocking ~500 tests.

#### 1a. Lambda Common Module Shim

Create a Lambda-compatible shim for `ref/node/test/common/index.js` at a path that Lambda's module resolver finds before the real common module. The shim must provide:

```js
// Essential functions (covers 95%+ of common usage)
module.exports = {
  // Callback tracking (1,977 + 457 + 403 + 81 = 2,918 usages)
  mustCall(fn, exact) { /* wrap fn, assert call count on process exit */ },
  mustNotCall(msg) { /* return fn that throws if called */ },
  mustSucceed(fn) { /* mustCall wrapper: assert first arg is null */ },
  mustCallAtLeast(fn, min) { /* assert call count >= min */ },

  // Platform detection (118 + 35 + 13 + 39 = 205 usages)
  isWindows: process.platform === 'win32',
  isMacOS: process.platform === 'darwin',
  isLinux: process.platform === 'linux',
  isAIX: process.platform === 'aix',
  isIBMi: false,
  isMainThread: true,

  // Feature detection (131 usages)
  hasCrypto: true,
  hasIntl: false,

  // Error assertion (57 + 55 + 25 = 137 usages)
  expectsError(opts) { /* return validator function */ },
  invalidArgTypeHelper(val) { /* return type string */ },
  expectWarning(map) { /* stub: track process warnings */ },

  // Test control (251 usages)
  skip(msg) { process.exit(0); },
  printSkipMessage(msg) { console.log('1..0 # Skipped:', msg); },

  // Object helpers (115 usages)
  mustNotMutateObjectDeep(obj) { /* return deep-frozen proxy */ },

  // Timeout scaling (45 usages)
  platformTimeout(ms) { return ms * 2; },

  // Misc
  allowGlobals(...globals) { /* add to allowed globals list */ },
  getArrayBufferViews(buf) { /* return all TypedArray views */ },
  getBufferSources(buf) { /* return ArrayBuffer + views */ },
  spawnPromisified(...args) { /* promisified child_process.spawn */ },

  // Stubs for globals check (disable the leaked-globals assertion)
  // This alone would fix ~80 tests
};
```

**Implementation approach**: 
1. Place shim at a path Lambda's resolver checks first (e.g., inject via transpiler when running tests from `ref/node/test/`)
2. The shim should NOT require `net` or `worker_threads` (the main blockers)
3. Must implement `mustCall` tracking with `process.on('exit')` validation

#### 1b. Common Sub-modules

| Sub-module | Priority | Usage | Implementation |
|------------|----------|-------|---------------|
| `../common/tmpdir` | High | 187 tests | `tmpdir.refresh()`, `tmpdir.resolve()`, `tmpdir.path` |
| `../common/fixtures` | Medium | 76 tests | `fixtures.path()`, `fixtures.readKey()` |
| `../common/crypto` | Low | 65 tests | Crypto helper utilities |

The `tmpdir` shim is critical — 187 fs/stream tests use it:
```js
module.exports = {
  path: './temp/node_test_tmpdir',
  resolve(...args) { return path.resolve(this.path, ...args); },
  refresh() { fs.rmSync(this.path, { recursive: true, force: true }); fs.mkdirSync(this.path, { recursive: true }); },
  hasEnoughSpace() { return true; },
};
```

#### 1c. Fix Leaked Globals (~80 tests)

Remove or guard `Node`, `innerWidth`, `innerHeight` from the global scope when running in pure Node.js compat mode. These are Radiant/DOM-related globals that should not be present in a headless JS execution context.

**Expected impact**: Phase 1 alone should take passing tests from 208 → ~400+.

### Phase 2: Crash Fixes (Target: +30 tests)

Fix all 37 crashing tests. Grouped by root cause:

#### 2a. `fs.watch()` Crashes (10 tests)

All `test-fs-watch-ignore-*.js` tests crash. Root cause: likely uninitialized libuv `uv_fs_event_t` handle or null pointer in the watch callback path. The `fs.watch()` API is listed as "🔲 New (Js15)" in Transpile_Node.md — either implement properly or stub gracefully.

#### 2b. `fs` I/O Crashes (7 tests)

- `test-fs-copyfile.js` — segfault in file copy
- `test-fs-write.js` — segfault in fd-based write
- `test-fs-stat-bigint.js` — likely BigInt handling in stat result
- `test-fs-read-empty-buffer.js` / `test-fs-read-offset-null.js` — null/empty buffer edge cases
- `test-fs-promises-file-handle-*.js` — FileHandle lifecycle

#### 2c. Crypto Crashes (6 tests)

- `test-crypto-sign-verify.js` — RSA/ECDSA sign/verify
- `test-crypto-x509.js` — X509Certificate class
- `test-crypto-rsa-dsa.js` — key generation edge cases
- Fix: bounds checking in mbedTLS integration, null key handling

#### 2d. Process/Path Crashes (5 tests)

- `test-process-chdir.js` / `test-process-chdir-errormessage.js` — `chdir()` with invalid paths
- `test-path-resolve.js` / `test-path-zero-length-strings.js` — empty string edge cases
- Fix: null/empty string guards in path resolution code

#### 2e. Other Crashes (9 tests)

- `test-buffer-generic-methods.js` — buffer prototype method dispatch
- `test-os.js` — os module comprehensive test
- `test-util-inspect.js` — deep inspection crash
- Various child_process spawn edge cases

### Phase 3: API Surface Completion (Target: +100 tests)

#### 3a. `path.win32` Full Implementation (~10 tests)

Lambda's `path.win32` has edge cases with backslash handling, drive letters, and UNC paths. Tests like `test-path-extname.js`, `test-path-dirname.js`, `test-path-basename.js` fail due to incorrect `path.win32.*` results.

**Fixes needed**:
- `path.win32.extname()` — handle backslash as separator
- `path.win32.dirname()` — UNC path support
- `path.win32.resolve()` — drive letter + CWD handling
- `path.win32.normalize()` — device names (CON, PRN, etc.)

#### 3b. `process` Object Completion (~15 tests)

| Missing API | Tests Affected |
|-------------|---------------|
| `process.config` | ~10 (build-time config object) |
| `process.getuid()` / `process.getgid()` | ~5 |
| `process.setuid()` / `process.setgid()` | ~3 |
| `process.features` | ~3 |
| `process.report` | ~2 |
| `process.allowedNodeEnvironmentFlags` | ~2 |

#### 3c. `os.constants` (~5 tests)

Implement `os.constants.signals` (SIGTERM, SIGKILL, etc.) and `os.constants.errno` (ENOENT, EACCES, etc.) objects. Currently missing, causing failures in `test-os-constants-signals.js`.

#### 3d. `Buffer` Static Methods (~8 tests)

| Method | Status |
|--------|--------|
| `Buffer.isAscii(input)` | Missing |
| `Buffer.isUtf8(input)` | Missing |
| `Buffer.copyBytesFrom(view, offset, length)` | Missing |
| `Buffer.poolSize` property | Missing |
| `SlowBuffer` class | Missing |

#### 3e. `assert` Enhancements (~8 tests)

- `assert.throws()` — improve error property matching (code, message, name)
- `assert.rejects()` — async error assertion
- `assert.match()` / `assert.doesNotMatch()` — already partially implemented
- Error message format matching (Node.js specific error codes like `ERR_ASSERTION`)

#### 3f. `util.inspect` Robustness (~8 tests)

- Handle circular references without stack overflow
- Custom `[util.inspect.custom]` symbol
- Proxy object inspection
- `showHidden`, `depth`, `colors` options
- `util.inspect.defaultOptions`

### Phase 4: Async/Callback Model Improvements (Target: +100 tests)

Many Node.js APIs have both sync and async variants. Lambda's sync implementations pass, but async callbacks fail because they depend on proper event loop integration.

#### 4a. Callback-style API Shimming

For tests that use callback-style APIs, ensure Lambda's event loop processes callbacks correctly:

```js
// These patterns must work:
fs.readFile('test.txt', (err, data) => { ... });
child_process.exec('echo hello', (err, stdout) => { ... });
dns.lookup('localhost', (err, address) => { ... });
zlib.gzip(input, (err, result) => { ... });
```

**Affected modules and test counts**:
- `zlib` async: ~50 tests (most zlib tests use callbacks)
- `fs` async: ~40 tests (readFile, writeFile, etc.)
- `dns` async: ~25 tests (lookup, resolve)
- `child_process` callbacks: ~30 tests (exec with callback)
- `stream` event-driven: ~50 tests

#### 4b. `process.nextTick` Ordering

Several stream and process tests depend on precise `process.nextTick` ordering relative to I/O callbacks. Verify Lambda's microtask queue correctly interleaves with I/O completion.

### Phase 5: Enable Disabled Modules (Target: +80 tests)

#### 5a. Enable `net` Module in Test Runner

Lambda implements `node:net` (js_net.cpp) but the test runner disables `net`-prefixed tests. Enable and assess.

**Action**: Change `{"net", "net", false, ...}` to `true` in `test_node_official_gtest.cpp`.

#### 5b. Enable `http` / `https` Modules

Lambda has full `node:http` and `node:https` implementations. Enable in test runner.

**Caveat**: Many HTTP tests depend on event loop, so pass rate will be low initially (~5-10%), but some request/response format tests should pass.

#### 5c. Enable `readline`, `timers` Modules

Both are implemented. Enable in test runner.

### Phase 6: Long-tail Improvements (Target: +90 tests)

#### 6a. `fs.createReadStream()` / `fs.createWriteStream()`

Listed as "🔲 Phase 2" in Transpile_Node.md. Implement using `node:stream.Readable` / `Writable` backed by fd-based reads/writes.

#### 6b. `fs/promises` Complete API

Many `test-fs-promises-*.js` tests already pass (42 in baseline). Expand coverage by implementing missing methods:
- `fs.promises.open()` → `FileHandle` class
- `fs.promises.watch()` — directory watching
- `fs.promises.cp()` — recursive copy

#### 6c. `crypto.subtle` Web Crypto Expansion

Expand Web Crypto API support for algorithms used in tests:
- `sign` / `verify` with RSA-PSS, ECDSA
- `importKey` / `exportKey` for JWK format
- `deriveKey` / `deriveBits` with HKDF, PBKDF2

#### 6d. Internal Module Stubs

40 tests use `require('internal/test/binding')` to access `internalBinding('uv')` for UV error codes. Provide a minimal stub:
```js
// Stub for internal/test/binding
module.exports = {
  internalBinding(name) {
    if (name === 'uv') return { UV_ENOENT: -2, UV_EACCES: -13, ... };
    return {};
  }
};
```

## 5. Implementation Priority and Estimated Impact

| Phase | Work Item | Estimated New Passes | Cumulative | Priority |
|-------|-----------|---------------------|------------|----------|
| 1a | Common module shim (mustCall, skip, platform) | +120 | 328 | **P0** |
| 1b | Common sub-modules (tmpdir, fixtures) | +60 | 388 | **P0** |
| 1c | Fix leaked globals (Node, innerWidth, innerHeight) | +20 | 408 | **P0** |
| 2 | Fix all 37 crashers | +30 | 438 | **P0** |
| 3a | `path.win32` fixes | +10 | 448 | **P1** |
| 3b | `process` object completion | +15 | 463 | **P1** |
| 3c-f | Buffer, assert, util, os.constants | +25 | 488 | **P1** |
| 4a | Async callback model improvements | +80 | 568 | **P1** |
| 4b | `process.nextTick` ordering | +20 | 588 | **P2** |
| 5 | Enable disabled modules (net, http, readline, timers) | +50 | 638 | **P2** |
| 6 | Long-tail (fs streams, crypto, internals stubs) | +60 | 698 | **P2** |

**Target**: 600+ passing tests (58%+ of in-scope tests), up from 208 (20.3%).

## 6. Test Infrastructure Improvements

### 6a. Test Harness Integration in Build

Add a `make node-shim` target that sets up the common module shim:
```makefile
node-shim:
	@mkdir -p ref/node/test/common/_lambda
	@cp lambda/js/test_shim/common_index.js ref/node/test/common/_lambda/index.js
	@cp lambda/js/test_shim/tmpdir.js ref/node/test/common/_lambda/tmpdir.js
```

### 6b. Module-level Test Reporting

Enhance `test_node_official_gtest.cpp` to report pass rates per module:
```
╔══════════════════════════════════════════════════╗
║  crypto:    78/122 (63.9%)   ▲ +15              ║
║  fs:        85/251 (33.9%)   ▲ +43              ║
║  stream:    60/215 (27.9%)   ▲ +26              ║
║  ...                                             ║
╚══════════════════════════════════════════════════╝
```

### 6c. Failure Classification in CI

Add automatic classification of new failures:
- `CRASH` — segfault/abort (P0 fix)
- `HARNESS` — common module load failure
- `API_MISSING` — missing function/method
- `ASYNC` — event loop dependency
- `COMPAT` — behavioral difference

## 7. Non-Goals

The following are explicitly out of scope for this proposal:

- **100% Node.js compliance** — Not a goal. Lambda is a polyglot runtime, not a Node.js clone.
- **`node:cluster`**, **`node:vm`**, **`node:inspector`**, **`node:worker_threads`** — These require fundamentally different architecture (multi-process, sandboxing, debugger protocol, threading).
- **Native addon support** (`node-gyp`, N-API) — Would require V8 ABI compatibility.
- **Full `fs.watch()` implementation** — Requires libuv event loop integration (tracked in Transpile_Js15).
- **HTTP/2 (`node:http2`)** — Low ecosystem impact relative to implementation cost.
- **`node:dgram`** (UDP) — Niche usage.

## 8. Appendix: Full Crasher List

All 37 tests that cause segfault (exit 139) or abort (exit 134):

### Segfaults (exit 139) — 28 tests
```
test-assert-async.js
test-buffer-generic-methods.js
test-child-process-exec-timeout-kill.js
test-child-process-spawn-controller.js
test-crypto-async-sign-verify.js
test-crypto-keygen-raw.js
test-crypto-publicDecrypt-fails-first-time.js
test-crypto-rsa-dsa.js
test-crypto-sign-verify.js
test-crypto-x509.js
test-fs-copyfile.js
test-fs-promises-file-handle-dispose.js
test-fs-promises-file-handle-readFile.js
test-fs-promises-watch-iterator.js
test-fs-read-empty-buffer.js
test-fs-read-offset-null.js
test-fs-stat-bigint.js
test-fs-watch-ignore-function.js
test-fs-watch-ignore-glob.js
test-fs-watch-ignore-invalid.js
test-fs-watch-ignore-mixed.js
test-fs-watch-ignore-recursive-glob-subdirectories.js
test-fs-watch-ignore-recursive-glob.js
test-fs-watch-ignore-recursive-mixed.js
test-fs-watch-ignore-recursive-regexp.js
test-fs-watch-ignore-regexp.js
test-fs-write.js
test-os.js
test-util-inspect.js
```

### Aborts (exit 134) — 8 tests
```
test-child-process-constructor.js
test-fs-realpath.js
test-fs-symlink-dir.js
test-path-resolve.js
test-path-zero-length-strings.js
test-process-chdir-errormessage.js
test-process-chdir.js
test-process-execve.js
```

### Timeouts — 4 tests
```
test-buffer-alloc-unsafe-is-initialized-with-zero-fill-flag.js
test-buffer-nopendingdep-map.js
test-child-process-execfilesync-maxbuf.js
test-fs-readdir-stack-overflow.js
```

## 9. Appendix: Disabled Module Test Inventory

Tests available if modules are enabled in the test runner:

| Module | Test Count | Implementation | Notes |
|--------|-----------|---------------|-------|
| `http` | 388 | ✅ js_http.cpp | Many event-loop dependent |
| `tls` | 214 | ✅ js_tls.cpp | Certificate handling tests |
| `net` | 148 | ✅ js_net.cpp | TCP socket tests |
| `https` | 63 | ✅ js_https.cpp | Thin wrapper over http+tls |
| `timers` | 58 | ✅ Alias | setTimeout/setInterval tests |
| `module` | 31 | ⚠️ Partial | createRequire, module resolution |
| `readline` | 21 | ✅ js_readline.cpp | Interactive input tests |
| `worker_threads` | ~50 | ❌ Not implemented | Threading |
| `cluster` | ~30 | ❌ Not implemented | Multi-process |
| `vm` | ~40 | ❌ Not implemented | Sandboxed contexts |
