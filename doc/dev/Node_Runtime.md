# Lambda Node.js Runtime — Design Document

## Overview

Lambda includes a Node.js compatibility layer that enables running Node.js-style JavaScript programs within the LambdaJS engine. This covers three areas:

1. **Built-in modules** — Native C++ implementations of 25+ Node.js core modules (`fs`, `path`, `crypto`, `os`, `http`, etc.)
2. **Module system** — CommonJS `require()` and ES Module `import` with Node.js resolution semantics
3. **NPM client** — Package installation, dependency resolution, and script execution (`lambda node install`, `lambda node task`)

All Node.js APIs are implemented as native C++ functions that operate on Lambda `Item` values — the same runtime representation used by JavaScript and Lambda scripts. There is no separate Node.js binary; everything runs inside `lambda.exe`.

> For the core JavaScript engine design (type system, JIT compilation, closures, classes, generators, etc.), see [dev/JS_Runtime.md](dev/JS_Runtime.md).

---

## 1. CLI Commands

### Running JavaScript

```bash
./lambda.exe js script.js                      # run a JS file
./lambda.exe js script.js --document page.html  # run with DOM context
```

The `js` command detects `.js` files, compiles them via `transpile_js_to_mir()`, and executes the resulting native code. `process.argv` is populated from CLI arguments.

### Package Manager

```bash
./lambda.exe node install                # install all from package.json
./lambda.exe node install lodash         # install a specific package
./lambda.exe node install lodash@^4.0    # install with version range
./lambda.exe node install -D jest        # install as dev dependency
./lambda.exe node uninstall lodash       # remove a package
./lambda.exe node task test              # run a package.json script
./lambda.exe node task test -- --verbose # run script with extra args
./lambda.exe node exec cowsay hello      # run a package binary (like npx)
```

Options: `--production` (skip devDependencies), `--dry-run`, `--verbose`.

---

## 2. Built-in Modules

Each module is a separate C++ file under `lambda/js/`. Modules are lazily constructed as Lambda `Map` objects on first access, then cached with epoch-based invalidation for batch test resets.

### Module Registration Pattern

Every module follows the same pattern:

```cpp
// js_path.cpp
static Item ns = {0};
static int ns_epoch = -1;

extern "C" Item js_get_path_namespace(void) {
    if (ns.item != 0 && ns_epoch == js_heap_epoch) return ns;
    ns_epoch = js_heap_epoch;
    ns = js_new_object();
    heap_register_gc_root(&ns.item);
    // populate properties and methods...
    js_property_set(ns, key("default"), ns);  // CJS default export
    return ns;
}

extern "C" void js_path_reset(void) { ns = (Item){0}; }
```

- **Singleton**: Each namespace is created once per heap epoch
- **GC-rooted**: Registered as GC root to survive garbage collection
- **Reset function**: Called by `js_batch_reset()` to invalidate the cache between test runs
- **Default export**: Each module sets a `default` property pointing to itself (for CJS `require()` interop)

### Module Dispatch

`js_module_get()` in `js_runtime.cpp` dispatches bare specifiers to native namespace constructors. Each module is matched by three forms:

```
"fs" | "fs.js" | "node:fs"  →  js_get_fs_namespace()
```

Sub-paths are also handled where applicable:

```
"path/posix" | "node:path/posix"    →  js_get_path_namespace()
"stream/promises"                    →  js_get_stream_namespace()
"assert/strict"                      →  js_get_assert_namespace()
"util/types"                         →  util namespace .types property
```

### Module Reference

| Module | File | Key APIs |
|--------|------|----------|
| `fs` | `js_fs.cpp` | `readFileSync`, `writeFileSync`, `existsSync`, `statSync`, `mkdirSync`, `readdirSync`, `unlinkSync`, `renameSync`, `appendFileSync`, `copyFileSync`, `realpathSync`, `accessSync`, `rmSync`, `mkdtempSync`, `chmodSync`, `symlinkSync`, `readlinkSync`, `lstatSync`, `readFile` (async), `writeFile` (async), `fs.constants`, `Stats` prototype |
| `path` | `js_path.cpp` | `join`, `resolve`, `dirname`, `basename`, `extname`, `isAbsolute`, `normalize`, `relative`, `parse`, `format`, `sep`, `delimiter`, `path.posix`, `path.win32` |
| `crypto` | `js_crypto.cpp` | `createHash` (md5, sha1, sha256, sha384, sha512), `createHmac`, `randomBytes`, `randomUUID`, native SHA implementation |
| `os` | `js_os.cpp` | `platform`, `arch`, `type`, `hostname`, `homedir`, `tmpdir`, `totalmem`, `freemem`, `cpus`, `uptime`, `endianness`, `release`, `version`, `networkInterfaces`, `userInfo`, `EOL` |
| `buffer` | `js_buffer.cpp` | `Buffer.alloc`, `Buffer.from` (utf8/hex/base64/array), `Buffer.concat`, `Buffer.isBuffer`, `Buffer.byteLength`, `toString` (utf8/hex/base64), `write`, `copy`, `equals`, `compare`, `indexOf`, `slice`, `fill` |
| `events` | `js_events.cpp` | `EventEmitter`: `on`, `once`, `off`, `emit`, `removeAllListeners`, `listeners`, `listenerCount`, `eventNames`, `setMaxListeners`, `getMaxListeners`, `prependListener`, `prependOnceListener` |
| `http` | `js_http.cpp` | `createServer`, `request`, `get`, `IncomingMessage`, `ServerResponse` |
| `https` | `js_https.cpp` | `request`, `get` |
| `net` | `js_net.cpp` | `createServer`, `createConnection`, `Socket`, `Server` |
| `tls` | `js_tls.cpp` | `connect`, `createServer`, TLS/SSL support |
| `stream` | `js_stream.cpp` | `Readable`, `Writable`, `Transform`, `PassThrough`, `pipeline`, `finished`, `stream/promises`, `stream/web`, `stream/consumers` |
| `child_process` | `js_child_process.cpp` | `exec`, `execSync`, `spawn`, `spawnSync` |
| `url` | `js_url_module.cpp` | `URL` constructor, `URLSearchParams`, `parse`, `format`, `resolve` |
| `util` | `js_util.cpp` | `format`, `inspect`, `promisify`, `deprecate`, `inherits`, `isDeepStrictEqual`, `types.*` (isDate, isRegExp, isArray, isMap, isSet, etc.) |
| `querystring` | `js_querystring.cpp` | `parse`, `stringify`, `escape`, `unescape`, `decode` (alias), `encode` (alias) |
| `assert` | `js_assert.cpp` | `ok`, `equal`, `notEqual`, `strictEqual`, `notStrictEqual`, `deepStrictEqual`, `notDeepStrictEqual`, `throws`, `doesNotThrow`, `rejects`, `doesNotReject`, `match`, `doesNotMatch`, `fail`, `ifError`, `assert/strict` (same as assert) |
| `zlib` | `js_zlib.cpp` | `gzipSync`, `gunzipSync`, `deflateSync`, `inflateSync`, `createGzip`, `createGunzip` |
| `dns` | `js_dns.cpp` | `lookup`, `resolve`, `resolve4` |
| `readline` | `js_readline.cpp` | `createInterface` |
| `string_decoder` | `js_string_decoder.cpp` | `StringDecoder` |

### Stub Modules

Some modules are implemented inline in `js_module_get()` as lightweight stubs providing essential properties without full implementations:

| Module | Key Properties |
|--------|---------------|
| `timers` / `timers/promises` | `setTimeout`, `setInterval`, `setImmediate`, `clearTimeout`, `clearInterval`, `scheduler.wait`, `scheduler.yield` |
| `module` | `builtinModules` (array of 26 names), `isBuiltin()`, `createRequire()`, `Module` |
| `worker_threads` | `isMainThread: true`, `threadId`, `MessageChannel`, `Worker` (stub) |
| `cluster` | `isPrimary: true`, `isMaster: true`, `isWorker: false` |
| `perf_hooks` | `performance.now()` |
| `tty` | `isatty()`, `ReadStream`, `WriteStream` (stubs) |
| `v8` | `promiseHooks`, `serialize`/`deserialize` (stubs), heap stat stubs |
| `async_hooks` | `AsyncLocalStorage`, `AsyncResource`, `createHook` |
| `diagnostics_channel` | `channel()`, `subscribe()`, `unsubscribe()` |
| `domain` | `create()`, `createDomain()` |
| `vm` | `createContext`, `runInContext`, `Script` |
| `console` | Alias to global `console` object |
| `node:test` | Basic test runner: `test`, `describe`, `it` |

---

## 3. `process` Object

The `process` global is built in `js_globals.cpp` via `js_get_process_object_value()`. It supports the Node.js `EventEmitter` interface.

### Properties

| Property | Description |
|----------|-------------|
| `process.argv` | CLI arguments array (set via `js_store_process_argv()`) |
| `process.pid` / `process.ppid` | Process and parent process IDs |
| `process.platform` | `"darwin"` / `"linux"` / `"win32"` |
| `process.arch` | `"arm64"` / `"x64"` |
| `process.version` | `"v20.0.0"` |
| `process.versions` | Object with engine version strings |
| `process.title` | `"lambda"` |
| `process.env` | Environment variables (read from `environ`) |
| `process.execPath` | Absolute path to `lambda.exe` |
| `process.execArgv` | Empty array (no V8 flags) |
| `process.exitCode` | Exit code (default 0) |
| `process.config` | Minimal `variables` object for Node.js compat |
| `process.features` | Minimal feature flags |
| `process.stdout` / `process.stderr` / `process.stdin` | Stream objects with `.write()` and `.read()` |

### Methods

| Method | Description |
|--------|-------------|
| `process.cwd()` | Current working directory |
| `process.chdir(dir)` | Change working directory |
| `process.exit(code)` | Exit with code |
| `process.uptime()` | Process uptime in seconds |
| `process.nextTick(fn)` | Schedule microtask |
| `process.hrtime()` / `process.hrtime.bigint()` | High-resolution timer |
| `process.memoryUsage()` | Memory usage object |
| `process.cpuUsage()` | CPU usage object |
| `process.umask(mask)` | Get/set file creation mask |
| `process.abort()` | Abort the process |
| `process.on(event, fn)` | Event listener (supports `uncaughtException`, `exit`, `warning`) |
| `process.emit(event, ...args)` | Emit event |
| `process.removeListener(event, fn)` | Remove listener |

---

## 4. Module System

### 4.1 CommonJS `require()`

The transpiler intercepts `require("specifier")` calls at compile time:

1. **Static resolution**: If the argument is a string literal, `jm_resolve_module_path()` resolves the path at transpile time and emits a call to `js_require(resolved_path)`
2. **Dynamic resolution**: If the argument is a runtime expression, emits `js_require(expr)` for runtime resolution
3. **Local `require` bypass**: If `require` is a local variable or parameter (e.g., webpack factory functions), the interception is skipped

#### CJS Source Wrapping

When `js_require()` loads a `.js` or `.cjs` file, it wraps the source in a CJS module envelope:

```javascript
// Injected prefix:
var __cjs_module__ = {exports: {}};
var exports = __cjs_module__.exports;
var module = __cjs_module__;
var __filename = "/absolute/path/to/file.js";
var __dirname = "/absolute/path/to";

// Original source here...

// Injected suffix:
export default __cjs_module__.exports;
```

The wrapped source is compiled as an ESM module (Lambda's native module format), and the `default` export — which is `module.exports` — is extracted as the require result.

#### Module Caching

- First-access modules are compiled via `transpile_js_module_to_mir()` and their namespace is cached in `js_module_get()`'s specifier-indexed cache
- Subsequent `require()` calls for the same path return the cached namespace
- Built-in modules take priority over `node_modules` to prevent npm polyfill packages from shadowing engine builtins

#### File Type Detection

```
.cjs  →  always CJS
.mjs  →  always ESM
.js   →  CJS when loaded via require() (Node.js behavior)
         ESM when detected via nearest package.json "type": "module"
```

### 4.2 ES Module `import`

Static `import` declarations are processed during transpiler Phase 2:

1. `jm_resolve_module_path()` resolves the specifier at transpile time
2. Built-in modules dispatch to `js_module_get()` at runtime
3. User modules are loaded via `transpile_js_module_to_mir()` with proper namespace construction
4. Named exports bind to module namespace properties
5. Default exports bind to the `default` property

### 4.3 Dynamic `import()`

`import("specifier")` compiles to a call to `js_dynamic_import()`, which:

1. Loads the module synchronously using the same mechanism as `require()`
2. Wraps the result in a resolved `Promise` for spec compliance

### 4.4 Module Resolution Algorithm

Resolution is implemented in `npm_resolve_module.cpp` following the Node.js algorithm:

```
resolve(specifier, from_dir):

1. Built-in check
   "fs", "path", "node:crypto", etc. → return (handled by js_module_get)

2. Relative specifier ("./", "../", "/")
   a. Try exact file
   b. Try with extensions: .js, .mjs, .cjs, .json
   c. Try as directory:
      - package.json "exports" field (modern, with conditions)
      - package.json "module" field (ESM entry)
      - package.json "main" field (CJS entry)
      - index.js / index.mjs / index.cjs / index.json

3. Bare specifier ("lodash", "@scope/pkg", "lodash/fp")
   a. Split into package name + subpath
      "lodash/fp"       → name="lodash",     subpath="fp"
      "@scope/pkg/util" → name="@scope/pkg", subpath="util"
   b. Walk up directories looking for node_modules/<name>
   c. For each candidate:
      - If subpath: resolve via package.json "exports" or direct file lookup
      - If no subpath: resolve package root entry point via package.json
   d. Determine ESM/CJS from .mjs/.cjs extension or nearest package.json "type"
```

#### Conditional Exports

When resolving `package.json` `"exports"`, the engine uses conditions in priority order:

```
["lambda", "node", "import", "default"]
```

The `"lambda"` condition allows packages to provide Lambda-specific entry points.

### 4.5 Module Variable System

Top-level `let`/`var`/`const`, function declarations, and class declarations in each module get **module variable** slots — a fixed-size indexed array accessible from any function without closure overhead:

```cpp
static Item js_module_vars[JS_MAX_MODULE_VARS];  // 2048 slots
static Item* js_active_module_vars;               // points to current module's vars
```

For nested `require()` calls, `js_save_module_vars()` / `js_restore_module_vars()` save and restore the active module variable table, preventing cross-module contamination.

---

## 5. NPM Client

The NPM client is implemented in `lambda/npm/` as a set of C files providing package installation, dependency resolution, and registry access.

### 5.1 Architecture

```
lambda node install lodash
    │
    ├─ npm_package_json_parse("package.json")    # parse project manifest
    ├─ npm_lockfile_read("lambda-node.lock")     # check lock file
    ├─ npm_resolve_dependencies(deps)            # BFS dependency tree resolution
    │   └─ npm_registry_fetch_package(name)      # fetch metadata from registry
    │   └─ semver_satisfies(version, range)      # find matching versions
    ├─ npm_install()                              # download, extract, link
    │   ├─ npm_registry_download_tarball(url)    # download .tgz
    │   └─ symlink creation                       # pnpm-style layout
    └─ npm_lockfile_write("lambda-node.lock")    # update lock file
```

### 5.2 Storage Layout (pnpm-style)

Packages are stored in a flat structure with symlinks:

```
node_modules/
├── .lambda/                              # real package storage
│   ├── lodash@4.17.21/
│   │   └── node_modules/
│   │       └── lodash/                   # actual package files
│   └── express@4.18.2/
│       └── node_modules/
│           └── express/
├── lodash → .lambda/lodash@4.17.21/node_modules/lodash     # symlink
└── express → .lambda/express@4.18.2/node_modules/express   # symlink
```

This avoids the deeply nested `node_modules` problem while maintaining compatibility with the Node.js resolution algorithm.

### 5.3 Package.json Parsing

`npm_package_json.cpp` parses `package.json` via Lambda's JSON parser (`parse_json_to_item`) and extracts:

```c
struct NpmPackageJson {
    const char* name, *version, *main, *module, *type;
    void*       exports_item;      // parsed "exports" field (Item)
    void*       imports_item;      // parsed "imports" field
    NpmDependency* dependencies;   // name + version range pairs
    NpmDependency* dev_dependencies;
    NpmDependency* peer_dependencies;
    NpmDependency* scripts;        // name + command pairs
    NpmDependency* bin;            // name + file path pairs
    void*       raw_item;          // raw parsed JSON for additional fields
};
```

The `"exports"` field supports the full Node.js conditional exports specification — string values, nested condition objects, and subpath patterns.

### 5.4 Semver

Full semantic versioning implementation in `semver.cpp`:

| Syntax | Example | Meaning |
|--------|---------|---------|
| Exact | `1.2.3` | Only 1.2.3 |
| Caret | `^1.2.3` | ≥1.2.3 <2.0.0 |
| Tilde | `~1.2.3` | ≥1.2.3 <1.3.0 |
| Range | `>=1.2.3 <2.0.0` | Explicit range |
| Hyphen | `1.2.3 - 2.3.4` | ≥1.2.3 ≤2.3.4 |
| X-Range | `1.2.x`, `1.*`, `*` | Wildcard |
| OR | `>=1.0.0 \|\| >=2.0.0` | Union |
| Pre-release | `1.0.0-alpha.1` | Pre-release tag |

Internals: `SemVerRange` is an OR of up to 8 `SemVerComparatorSet`s, each an AND of up to 8 `SemVerComparator`s.

### 5.5 Registry Client

`npm_registry.cpp` communicates with `registry.npmjs.org`:

- `npm_registry_fetch_package(name)` — fetches package metadata JSON
- `npm_registry_resolve_version(pkg, range)` — finds the best matching version using semver
- `npm_registry_download_tarball(url, dest)` — downloads and extracts `.tgz` tarballs

### 5.6 Dependency Resolution

`npm_resolver.cpp` resolves the full dependency tree using BFS traversal:

1. Start with top-level dependencies from `package.json`
2. For each unresolved dependency, fetch metadata from the registry
3. Find the best version satisfying the range (using lock file for pinning when available)
4. Add transitive dependencies to the queue
5. Produce a flat list of `NpmResolvedPackage` entries with name, version, tarball URL, and integrity hash

### 5.7 Lock File

`npm_lockfile.cpp` manages `lambda-node.lock` (JSON format):

```json
{
  "version": 1,
  "packages": {
    "lodash@4.17.21": {
      "version": "4.17.21",
      "resolved": "https://registry.npmjs.org/lodash/-/lodash-4.17.21.tgz",
      "integrity": "sha512-...",
      "dependencies": {}
    }
  }
}
```

Lock file entries are consulted during resolution to pin versions and skip registry fetches for already-resolved packages.

### 5.8 Script Execution (`lambda node task`)

When running a package.json script:

1. Parse `package.json` to find the script command
2. Prepend `node_modules/.bin` to `PATH`
3. Execute the command via `shell_exec_line()` with the modified environment
4. Support extra CLI arguments passed after `--`

### 5.9 Binary Execution (`lambda node exec`)

When running a package binary (like `npx`):

1. Check `node_modules/.bin/<name>` for the binary
2. If not found, auto-install the package
3. Execute the binary with remaining CLI arguments

---

## 6. File Layout

### Node.js Module Implementations (`lambda/js/`)

| File | Lines | Purpose |
|------|------:|---------|
| `js_fs.cpp` | ~1,800 | File system module |
| `js_path.cpp` | ~1,100 | Path manipulation |
| `js_crypto.cpp` | ~1,800 | Cryptographic hash, HMAC, random |
| `js_os.cpp` | ~720 | Operating system info |
| `js_buffer.cpp` | ~2,500 | Buffer implementation |
| `js_events.cpp` | ~560 | EventEmitter |
| `js_http.cpp` | ~1,300 | HTTP server and client |
| `js_https.cpp` | ~120 | HTTPS adapter |
| `js_net.cpp` | ~650 | TCP networking |
| `js_tls.cpp` | ~620 | TLS/SSL |
| `js_stream.cpp` | ~850 | Readable, Writable, Transform streams |
| `js_child_process.cpp` | ~650 | exec, spawn |
| `js_url_module.cpp` | ~760 | URL and URLSearchParams |
| `js_util.cpp` | ~1,200 | format, inspect, types |
| `js_querystring.cpp` | ~460 | Query string parsing |
| `js_assert.cpp` | ~1,200 | Assertion library |
| `js_zlib.cpp` | ~460 | Compression |
| `js_dns.cpp` | ~220 | DNS resolution |
| `js_readline.cpp` | ~130 | Readline interface |
| `js_string_decoder.cpp` | ~150 | String decoder |

### NPM Client (`lambda/npm/`)

| File | Purpose |
|------|---------|
| `npm_resolve_module.cpp/h` | Node.js module resolution algorithm |
| `npm_package_json.cpp/h` | package.json parser |
| `npm_installer.cpp/h` | Package download, extract, link |
| `npm_resolver.cpp/h` | BFS dependency tree resolution |
| `npm_registry.cpp/h` | npm registry HTTP client |
| `npm_tarball.cpp/h` | .tgz extraction (zlib + tar) |
| `npm_lockfile.cpp/h` | lambda-node.lock read/write |
| `semver.cpp/h` | Full semver parser and range matcher |

### Tests (`test/node/`)

25+ test files covering all built-in modules. Each test is a `.js` file with a corresponding `.txt` expected output file.

---

## 7. Known Limitations

- **No event loop**: Node.js async I/O patterns (streams, network callbacks) execute synchronously. `fs.readFile` callbacks are invoked immediately.
- **WeakMap/WeakSet**: No actual weak reference semantics — objects are retained normally (no GC integration).
- **eval()**: Limited to expression wrapping; some multi-statement eval patterns fail.
- **worker_threads**: Stub only — `isMainThread: true`, no actual thread spawning.
- **Strict mode**: Approximated by a global flag, not tracked per-scope.
- **npm**: No workspace support, no optional dependencies, no lifecycle scripts (pre/post install hooks).
- **HTTP/HTTPS**: Basic implementation suitable for simple requests; no full HTTP/2 or WebSocket support.
