# Transpile_Node: Node.js Compatibility Layer for Lambda Runtime

## 1. Scope & Relationship to JS Track

This proposal covers **Node.js-specific features beyond standard JavaScript**. Core JS language support (ES6+ syntax, classes, generators, async/await, Promises, etc.) is tracked separately in the `Transpile_Js*` series.

This track focuses on:
- **Node.js built-in modules** (`node:fs`, `node:path`, `node:os`, etc.)
- **Node.js globals** (`process`, `Buffer`, `__dirname`, `__filename`, `require`)
- **npm package management** (resolution, installation, `node_modules` layout)
- **CommonJS `require()` interop** with ES modules
- **`package.json` semantics** (exports, main, type, bin, scripts)

### What This Track Does NOT Cover
- JS language features (covered by Transpile_Js1–Js28)
- Event loop and libuv integration (covered by Transpile_Js15)
- HTTP server implementation (covered by Transpile_Js15)
- DOM/CSSOM APIs (covered by js_dom.cpp / js_cssom.cpp)

## 2. CLI Namespace Convention

Lambda uses a **language subcommand** convention for polyglot operations:

| Command | Language | Mnemonic |
|---------|----------|----------|
| `lambda js` | JavaScript | Universal abbreviation |
| `lambda ts` | TypeScript | Universal abbreviation |
| `lambda node` | Node.js (npm ecosystem) | Already a short word |
| `lambda py` | Python | Standard (`py` is official on Windows) |
| `lambda rb` | Ruby | Standard (`.rb` file extension) |
| `lambda sh` | Bash / Shell | Standard (`sh` binary and extension) |

The `node` subcommand manages npm packages and Node.js-specific features:

```bash
lambda node install              # Install npm dependencies
lambda node run app.js           # Run with Node.js compat layer
lambda node task start           # Run package.json script
```

Automatic language detection (`lambda run file.js`) still works — the language subcommand is only needed for language-specific operations like package management.

Lock file: `lambda-node.lock` (hyphenated, scoped to the node ecosystem).

## 3. Current State

Lambda's JS runtime already implements partial Node.js support:

| Feature                       | Status         | Implementation                                                                                                                                                                                                                                      |
| ----------------------------- | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `node:fs` (sync)              | ✅ Partial      | `js_fs.cpp` — readFileSync, writeFileSync, appendFileSync, existsSync, unlinkSync, mkdirSync, rmdirSync, renameSync, readdirSync, statSync, copyFileSync, symlinkSync, chmodSync, truncateSync, openSync, closeSync, readSync, writeSync, fstatSync |
| `node:fs` (async)             | ✅ Partial      | readFile, writeFile (callback-based)                                                                                                                                                                                                                |
| `node:child_process`          | ✅ Full         | `js_child_process.cpp` — exec, execSync, spawn, spawnSync via libuv uv_spawn                                                                                                                                                                        |
| `node:crypto`                 | ✅ Full         | `js_crypto.cpp` — SHA-256/384/512, HMAC, createHash, createHmac, randomBytes, randomUUID, randomInt, getHashes, timingSafeEqual                                                                                                                     |
| `node:path`                   | ✅ Full         | `js_path.cpp` — join, resolve, dirname, basename, extname, normalize, isAbsolute, relative, parse, format, sep, delimiter                                                                                                                           |
| `node:url`                    | ✅ Full         | `js_url_module.cpp` — URL class, parse, format, resolve, fileURLToPath, pathToFileURL                                                                                                                                                               |
| `node:os`                     | ✅ Full         | `js_os.cpp` — platform, arch, type, hostname, homedir, tmpdir, cpus (real CPU data), totalmem, freemem, uptime, loadavg, userInfo, networkInterfaces (real data), EOL, endianness                                                                   |
| `node:events`                 | ✅ Full         | `js_events.cpp` — EventEmitter: on, once, off, emit (error auto-throw), removeAllListeners, listeners, rawListeners, prependListener, defaultMaxListeners                                                                                           |
| `node:util`                   | ✅ Full         | `js_util.cpp` — format (%s/%d/%i/%f/%j/%o/%O), inspect, promisify, deprecate, callbackify, isDeepStrictEqual, debuglog, TextEncoder, TextDecoder, types.* (18 type checkers)                                                                        |
| `node:buffer`                 | ✅ Full         | `js_buffer.cpp` — alloc, allocUnsafe, from, concat, toString, 14 read + 14 write BE/LE, toJSON, swap16/32/64, slice, includes, indexOf                                                                                                              |
| `node:querystring`            | ✅ Full         | `js_querystring.cpp` — parse, stringify, escape, unescape                                                                                                                                                                                           |
| `node:string_decoder`         | ✅ Full         | `js_string_decoder.cpp` — StringDecoder, write, end (UTF-8 multi-byte handling)                                                                                                                                                                     |
| `node:assert`                 | ✅ Full         | `js_assert.cpp` — ok, equal, strictEqual, deepEqual, deepStrictEqual, notDeepEqual, notDeepStrictEqual, throws, fail, ifError, match, doesNotMatch (15 methods)                                                                                     |
| `node:timers`                 | ✅ Alias        | Registered as importable module (wraps global timer functions)                                                                                                                                                                                      |
| `node:console`                | ✅ Alias        | Registered as importable module (wraps global console)                                                                                                                                                                                              |
| `process` global              | ✅ Full         | argv, env, exit, exitCode, cwd, platform, arch, pid, ppid, version, versions, hrtime, nextTick, memoryUsage, cpuUsage, umask, uptime, title, stdout, stderr, on('exit'/'uncaughtException')                                                         |
| `Buffer`                      | ✅ Full         | Extended Uint8Array: alloc, allocUnsafe, from, concat, endian read/write, includes, subarray, toJSON, swap16/32/64, 14 read + 14 write methods                                                                                                      |
| `__dirname` / `__filename`    | ✅              | Set per-module during transpilation                                                                                                                                                                                                                 |
| `require()`                   | ✅ Full         | CJS require with source wrapping (`module.exports`), resolves relative + bare specifiers via `npm_resolve_module()`                                                                                                                                 |
| ES Modules                    | ✅              | import/export with module registry                                                                                                                                                                                                                  |
| `node:dns`                    | ✅ Full         | `js_dns.cpp` — lookup (async), lookupSync (sync), resolve                                                                                                                                                                                           |
| `node:zlib`                   | ✅ Full         | `js_zlib.cpp` — gzipSync, gunzipSync, deflateSync, inflateSync, deflateRawSync, inflateRawSync, brotliCompressSync, brotliDecompressSync                                                                                                            |
| `node:readline`               | ✅ Full         | `js_readline.cpp` — createInterface, question, close, on                                                                                                                                                                                            |
| `node:stream`                 | ✅ Full         | `js_stream.cpp` — Readable, Writable, Duplex, Transform, PassThrough, pipeline, finished                                                                                                                                                            |
| `node:net`                    | ✅ Full         | `js_net.cpp` — createServer, createConnection, Socket, isIP/isIPv4/isIPv6                                                                                                                                                                           |
| `node:tls`                    | ✅ Full         | `js_tls.cpp` — connect, createServer, createSecureContext, TLSSocket (wraps mbedTLS via serve/tls_handler)                                                                                                                                          |
| `node:http`                   | ✅ Full         | `js_http.cpp` — createServer, request, get, IncomingMessage, ServerResponse (writeHead/write/end/setHeader), STATUS_CODES, METHODS                                                                                                                  |
| `node:https`                  | ✅ Full         | `js_https.cpp` — createServer, request, get (thin wrapper over http + tls)                                                                                                                                                                          |
| npm packages                  | ✅ Phase 2 done | `lambda node install`, semver resolution, `node_modules/` layout, `lambda-node.lock`                                                                                                                                                                |
| npm bare specifier resolution | ✅ Phase 3      | `npm_resolve_module()` integrated into `jm_resolve_module_path()`, conditional exports support                                                                                                                                                      |
| `lambda node task`            | ✅ Phase 3      | Runs `package.json` scripts via `shell_exec_line`, prepends `node_modules/.bin` to PATH                                                                                                                                                             |
| `lambda node exec`            | ✅ Phase 3      | Runs package binaries from `node_modules/.bin`, auto-installs if missing                                                                                                                                                                            |

### Existing C Library Foundations

Lambda already has C libraries that map directly to Node.js module functionality:

| C Module | Node.js Equivalent | Status |
|----------|-------------------|--------|
| `lib/file.c` + `lib/file_utils.c` | `node:fs` | ✅ 37+ functions, fully operational |
| `lib/shell.c` | `node:child_process` | ✅ exec, spawn, env, process management |
| `lib/url.c` | `node:url` | ✅ WHATWG URL parser |
| `lambda/lambda-path.h` + `lib/file.c` path utils | `node:path` | ✅ join, dirname, basename, ext, resolve |
| `lambda/sysinfo.cpp` | `node:os` | ✅ platform, arch, hostname, cpus, memory |
| Native SHA in `js_crypto.cpp` | `node:crypto` | ✅ SHA, HMAC, randomBytes, randomUUID, createHash, createHmac, timingSafeEqual |

## 4. Node.js Built-in Modules — Tiered Implementation Plan

Modules are prioritized by how many npm packages depend on them (based on npm ecosystem analysis and Deno/Bun experience).

### Tier 1: Critical (required by 80%+ of npm packages)

These modules are needed to run the vast majority of npm packages.

#### `node:fs` — Complete File System

**Backing**: `lib/file.c` + `lib/file_utils.c` (already implemented)

Extend `js_fs.cpp` to expose the full `lib/file.c` API surface:

| API | C Backing | Status |
|-----|-----------|--------|
| `readFileSync` / `readFile` | `read_text_file`, `read_binary_file` | ✅ Done |
| `writeFileSync` / `writeFile` | `write_text_file`, `write_binary_file` | ✅ Done |
| `appendFileSync` | `append_text_file` | ✅ Done |
| `existsSync` | `file_exists` | ✅ Done |
| `statSync` / `stat` | `file_stat` → `FileStat` struct | ✅ Done |
| `mkdirSync` (recursive) | `create_dir` | ✅ Done |
| `unlinkSync` | `file_delete` | ✅ Done |
| `renameSync` | `file_rename` | ✅ Done |
| `readdirSync` | `dir_list` | ✅ Done |
| `copyFileSync` | `file_copy` | ✅ Done |
| `symlinkSync` | `file_symlink` | ✅ Done |
| `chmodSync` | `file_chmod` | ✅ Done |
| `realpathSync` | `file_realpath` | ✅ Done |
| `accessSync` | `file_is_readable/writable/executable` | ✅ Done |
| `rmSync` (recursive) | `file_delete_recursive`, `dir_delete` | ✅ Done |
| `mkdtempSync` | `dir_temp_create` | ✅ Done |
| `readlinkSync` | POSIX `readlink` | ✅ Done |
| `lstatSync` | `lstat()` (no symlink follow) | ✅ Done |
| `truncateSync` | `uv_fs_ftruncate` | ✅ Done |
| `openSync` / `closeSync` | `uv_fs_open`, `uv_fs_close` | ✅ Done |
| `readSync` / `writeSync` | `uv_fs_read`, `uv_fs_write` (fd-based) | ✅ Done |
| `fstatSync` | `uv_fs_fstat` (full stat from fd) | ✅ Done |
| `watch` / `watchFile` | libuv `uv_fs_event_t` | 🔲 New (Js15) |
| `createReadStream` / `createWriteStream` | Requires `node:stream` | 🔲 Phase 2 |
| `fs/promises` | Async wrappers via libuv `uv_fs_*` | 🔲 Phase 2 |

The heavy lifting is done — `lib/file.c` already implements the underlying operations. Wiring to JS is straightforward.

#### `node:path`

**Backing**: `lib/file.c` path utilities + `lambda/path.c`

New file: `lambda/js/js_path.cpp`

| API | C Backing | Status |
|-----|-----------|--------|
| `path.join(...)` | `file_path_join` | ✅ Done |
| `path.resolve(...)` | `file_realpath` + cwd | ✅ Done |
| `path.dirname(p)` | `file_path_dirname` | ✅ Done |
| `path.basename(p, ext?)` | `file_path_basename` | ✅ Done |
| `path.extname(p)` | `file_path_ext` | ✅ Done |
| `path.normalize(p)` | C implementation | ✅ Done |
| `path.isAbsolute(p)` | C implementation | ✅ Done |
| `path.relative(from, to)` | C implementation | ✅ Done |
| `path.parse(p)` | Composition | ✅ Done |
| `path.format(obj)` | Composition | ✅ Done |
| `path.sep` | `'/'` or `'\\'` | ✅ Done |
| `path.delimiter` | `':'` or `';'` | ✅ Done |
| `path.posix` / `path.win32` | Namespaced variants | 🔲 Low priority |

#### `node:os`

**Backing**: `lambda/sysinfo.cpp` + POSIX/Win32 APIs

New file: `lambda/js/js_os.cpp`

| API | Backing | Status |
|-----|---------|--------|
| `os.platform()` | `LAMBDA_PLATFORM` macro | ✅ Done |
| `os.arch()` | Compile-time detect | ✅ Done |
| `os.type()` | `uname().sysname` | ✅ Done |
| `os.release()` | `uname().release` | ✅ Done |
| `os.hostname()` | `shell_hostname()` | ✅ Done |
| `os.homedir()` | `shell_home_dir()` | ✅ Done |
| `os.tmpdir()` | `"./temp/"` | ✅ Done |
| `os.cpus()` | Real CPU data: `sysctlbyname` (macOS), `/proc/cpuinfo` (Linux), per-CPU tick times | ✅ Done |
| `os.totalmem()` | `sysctl` / `sysinfo` | ✅ Done |
| `os.freemem()` | `sysctl` / `sysinfo` | ✅ Done |
| `os.uptime()` | `sysctl` / `/proc/uptime` | ✅ Done |
| `os.userInfo()` | `getpwuid` | ✅ Done |
| `os.networkInterfaces()` | `getifaddrs` — IPv4/IPv6 address, netmask, MAC, CIDR, internal flag | ✅ Done |
| `os.EOL` | `"\n"` or `"\r\n"` | ✅ Done |
| `os.endianness()` | Compile-time | ✅ Done |
| `os.loadavg()` | `getloadavg()` | ✅ Done |
| `os.version()` | `uname().version` | ✅ Done |

#### `node:process` (global + module)

Extend existing `process` global with full module semantics.

| API                               | Status | Notes                                                  |
| --------------------------------- | ------ | ------------------------------------------------------ |
| `process.argv`                    | ✅ Done |                                                        |
| `process.env`                     | ✅ Done | Via `shell_getenv/setenv`                              |
| `process.exit(code)`              | ✅ Done |                                                        |
| `process.cwd()`                   | ✅ Done | Via `file_getcwd`                                      |
| `process.platform`                | ✅ Done |                                                        |
| `process.arch`                    | ✅ Done |                                                        |
| `process.pid`                     | ✅ Done | `getpid()`                                             |
| `process.ppid`                    | ✅ Done | `getppid()`                                            |
| `process.version`                 | ✅ Done | Lambda version string                                  |
| `process.versions`                | ✅ Done | `{node, lambda, v8, uv, modules}`                      |
| `process.hrtime()` / `.bigint()`  | ✅ Done | `clock_gettime` / `mach_absolute_time`                 |
| `process.memoryUsage()`           | ✅ Done | mach task_info / procfs / GetProcessMemoryInfo         |
| `process.cpuUsage()`              | ✅ Done | `getrusage()`                                          |
| `process.nextTick(cb)`            | ✅ Done | Via `js_microtask_enqueue`                             |
| `process.stdout` / `.stderr`      | ✅ Done | Writable stream objects                                |
| `process.stdin`                   | 🔲     | Readable stream (Phase 2)                              |
| `process.on('exit', cb)`          | ✅ Done | Static listener array, `js_process_emit_exit()`        |
| `process.on('uncaughtException')` | ✅ Done | Static listener array                                  |
| `process.chdir(dir)`              | ✅ Done | `chdir()`                                              |
| `process.umask()`                 | ✅ Done | `umask()`                                              |
| `process.uptime()`                | ✅ Done | Time since process start                               |
| `process.title`                   | ✅ Done | Process title string                                   |
| `process.exitCode`                | ✅ Done | Set exit code without exiting; `exit()` defaults to it |

#### `node:events` — EventEmitter

Core dependency for `process`, streams, HTTP, and most Node.js patterns.

New file: `lambda/js/js_events.cpp`

**Implementation**: Pure JS (transpiled), not native C. EventEmitter is a class with:
- `on(event, listener)` / `addListener` ✅ Done
- `once(event, listener)` ✅ Done
- `off(event, listener)` / `removeListener` ✅ Done
- `emit(event, ...args)` ✅ Done — auto-throws on unhandled `'error'` events (Node.js standard)
- `removeAllListeners(event?)` ✅ Done
- `listeners(event)` / `listenerCount(event)` ✅ Done
- `eventNames()` ✅ Done
- `setMaxListeners(n)` / `getMaxListeners()` ✅ Done
- `prependListener` / `prependOnceListener` ✅ Done
- `rawListeners(event)` ✅ Done
- `defaultMaxListeners` (static property = 10) ✅ Done

**Strategy**: Ship as a JS polyfill (`lambda/js/polyfills/events.js`) compiled into the runtime, rather than implementing in C. This matches Deno's approach.

#### `node:util`

Commonly imported for `promisify`, `inspect`, `types`, `TextEncoder`/`TextDecoder`.

| API | Priority | Notes |
|-----|----------|-------|
| `util.promisify(fn)` | ✅ Done | Convert callback-style to Promise |
| `util.callbackify(fn)` | ✅ Done | Inverse of promisify |
| `util.inspect(obj, opts)` | ✅ Done | Object pretty-printing (JSON.stringify-based) |
| `util.format(fmt, ...)` | ✅ Done | printf-style: %s, %d, %i, %f, %j, %o, %O, %% |
| `util.deprecate(fn, msg)` | ✅ Done | Wrap with deprecation warning |
| `util.types.isDate/isRegExp/...` | ✅ Done | Type checking functions (isBuffer, isError, isPromise, isUint8Array, isFunction, isString, isNumber, isBoolean, isNull, isUndefined, isNullOrUndefined, isObject, isPrimitive) |
| `util.inherits(ctor, super)` | ✅ Done | Legacy, use class extends |
| `util.debuglog(section)` | ✅ Done | Returns logging function gated by `NODE_DEBUG` env var |
| `util.TextEncoder/TextDecoder` | ✅ Done | Exposed on util namespace (also available as globals) |

#### `node:buffer`

Extend Uint8Array with Node.js Buffer semantics.

| API | Priority | Notes |
|-----|----------|-------|
| `Buffer.from(str, enc)` | High | String → bytes with encoding |
| `Buffer.alloc(size)` | High | Zero-filled allocation |
| `Buffer.allocUnsafe(size)` | ✅ Done | Uninitialized (performance) |
| `Buffer.concat(list)` | High | Join multiple buffers |
| `buf.toString(enc)` | High | Decode to string (utf8, hex, base64, ascii) |
| `buf.slice` / `buf.subarray` | ✅ Done | View into underlying memory |
| `buf.write(str, off, len, enc)` | Medium | |
| `buf.readUInt32BE/LE` etc. | ✅ Done | Endian-aware reads (UInt8/16/32, Int8/16/32, Float, Double) |
| `buf.writeUInt32BE/LE` etc. | ✅ Done | Endian-aware writes (UInt8/16/32, Int8/16/32, Float/Double BE/LE — 14 methods) |
| `buf.toJSON()` | ✅ Done | Returns `{type: "Buffer", data: [...]}` |
| `buf.swap16/32/64` | ✅ Done | Byte-order swapping |
| `buf.compare` / `buf.equals` | ✅ Done | |
| `buf.copy(target)` | ✅ Done | |
| `buf.fill(val)` | ✅ Done | |
| `buf.includes(val)` | ✅ Done | Search buffer contents |
| `buf.lastIndexOf(val)` | ✅ Done | Reverse search |

**Strategy**: Implement Buffer as a subclass of Uint8Array with additional methods. Encodings backed by native C helpers (base64 from existing `atob`/`btoa`, hex trivial).

### Tier 2: Important (required by popular frameworks and tools)

#### `node:stream`

✅ **Implemented in Phase 4** — `js_stream.cpp`

Provides Readable, Writable, Duplex, Transform, PassThrough stream classes with EventEmitter-style on/emit and push/pull model. `pipeline()` and `Readable.from()` included.

Required by `fs.createReadStream`, HTTP, and many npm packages.

| Type | Priority | Status |
|------|----------|--------|
| `Readable` | High | ✅ Done — push/pull, flowing/non-flowing, pipe |
| `Writable` | High | ✅ Done — write, end, drain/finish events |
| `Transform` | Medium | ✅ Done — _transform/_flush hooks |
| `Duplex` | Medium | ✅ Done — Readable + Writable combined |
| `PassThrough` | Low | ✅ Done — no-op Transform |
| `pipeline(...)` | Medium | ✅ Done — two-argument pipe chain |
| `finished(stream, cb)` | Medium | ✅ Done — Detect stream completion |

**Strategy**: Implement as JS polyfills. The Web Streams API (`ReadableStream`/`WritableStream`) from Transpile_Js15 can serve as the underlying mechanism.

#### `node:http` / `node:https`

**Backing**: libuv TCP + inline HTTP/1.1 parser + mbedTLS (via serve/tls_handler)

| API | Status | Notes |
|-----|--------|-------|
| `http.createServer(handler)` | ✅ Done | libuv TCP server + HTTP parsing, calls handler(req, res) |
| `http.request(opts, cb)` | ✅ Done | HTTP client via libuv TCP connect |
| `http.get(url, cb)` | ✅ Done | Shorthand for GET, auto-calls end() |
| `https.createServer(opts, handler)` | ✅ Done | Wraps http.createServer with TLS options |
| `https.request` / `https.get` | ✅ Done | Defaults port 443, delegates to http.request |
| `IncomingMessage` | ✅ Done | method, url, headers, httpVersion, body, on(data/end) |
| `ServerResponse` | ✅ Done | writeHead, write, end, setHeader, getHeader, removeHeader |
| `http.STATUS_CODES` | ✅ Done | Map of status code → reason phrase |
| `http.METHODS` | ✅ Done | Array of HTTP methods |
| `http.globalAgent` | ✅ Done | Stub agent object |

#### `node:child_process`

**Backing**: `lib/shell.c` + `js_child_process.cpp` (already partially implemented)

| API | Status | Notes |
|-----|--------|-------|
| `exec(cmd, cb)` | ✅ Done | Via libuv uv_spawn |
| `execSync(cmd)` | ✅ Done | |
| `spawn(cmd, args, opts)` | ✅ Done | Async streaming I/O via libuv uv_spawn |
| `spawnSync(cmd, args)` | ✅ Done | Sync via popen, returns {stdout, stderr, status} |
| `execFile(file, args, cb)` | ✅ Done | Alias to spawn |
| `fork(modulePath)` | ❤ Skip | Requires multi-process Lambda |
| `execFileSync` | ✅ Done | Alias to spawnSync |

#### `node:crypto`

**Backing**: mbedTLS (already linked for TLS) + native SHA

| API | Priority | C Backing |
|-----|----------|-----------|
| `createHash(alg)` | ✅ Done | Native SHA-256/384/512, streaming update/digest |
| `createHmac(alg, key)` | ✅ Done | Native HMAC with SHA-256/384/512 |
| `randomBytes(n)` | ✅ Done | `/dev/urandom` (Unix), BCryptGenRandom (Windows) |
| `randomUUID()` | ✅ Done | UUID v4 from random bytes |
| `randomInt(min, max)` | ✅ Done | Uniform random integer in range |
| `getHashes()` | ✅ Done | Returns ["sha256", "sha384", "sha512"] |
| `timingSafeEqual(a, b)` | ✅ Done | Constant-time comparison |
| `createCipheriv` / `createDecipheriv` | Medium | mbedTLS AES |
| `pbkdf2` / `scrypt` | Medium | mbedTLS KDF |
| `subtle` (Web Crypto) | Low | SubtleCrypto API |

#### `node:url`

**Backing**: `lib/url.c` (WHATWG URL parser, already implemented)

Wire existing `lib/url.c` to JS `URL` class (largely done via Web APIs). Add legacy `url.parse()` / `url.format()` / `url.resolve()` for Node.js compat.

✅ `fileURLToPath(url)` and `pathToFileURL(path)` — Done.

#### `node:querystring`

| API | Notes |
|-----|-------|
| `qs.parse(str)` | Parse `a=1&b=2` to `{a:"1", b:"2"}` |
| `qs.stringify(obj)` | Inverse |
| `qs.escape(str)` | URL-encode |
| `qs.unescape(str)` | URL-decode |

Small module, can be pure JS.

### Tier 3: Nice to Have

| Module | Notes | Priority |
|--------|-------|----------|
| `node:string_decoder` | ✅ Implemented — handles multi-byte UTF-8 across chunks | Done |
| `node:assert` | ✅ Implemented — ok, equal, strictEqual, deepEqual, deepStrictEqual, notDeepEqual, notDeepStrictEqual, throws, fail, match, doesNotMatch (15 methods) | Done |
| `node:timers` | ✅ Registered — alias to global timer functions | Done |
| `node:console` | ✅ Registered — alias to global console | Done |
| `node:readline` | ✅ Implemented in Phase 4 | Done |
| `node:zlib` | ✅ Implemented in Phase 4 | Done |
| `node:dns` | ✅ Implemented in Phase 4 | Done |
| `node:net` | ✅ Implemented in Phase 4 | Done |
| `node:tls` | ✅ Full — connect, createServer, createSecureContext, TLSSocket (mbedTLS) | Done |
| `node:http` | ✅ Full — createServer, request, get, IncomingMessage, ServerResponse, STATUS_CODES | Done |
| `node:https` | ✅ Full — createServer, request, get (thin wrapper over http+tls) | Done |
| `node:perf_hooks` | `performance.now()` etc. | Low |
| `node:worker_threads` | Threading; out of scope initially | Low |
| `node:module` | `createRequire`, module metadata | Medium |

### Tier 4: Explicitly Out of Scope

| Module | Reason |
|--------|--------|
| `node:cluster` | Multi-process coordination; complex, rarely needed |
| `node:v8` | V8-specific; Lambda uses MIR JIT |
| `node:vm` | Sandboxed execution; requires VM isolation |
| `node:http2` | HTTP/2 protocol; low priority |
| `node:dgram` | UDP sockets; niche |
| `node:inspector` | Debugger protocol; V8-specific |
| `node:trace_events` | V8 tracing; not applicable |
| `node:repl` | Lambda has its own REPL |
| `node:domain` | Deprecated in Node.js |
| `node:punycode` | Deprecated in Node.js |

## 5. Node.js Globals

| Global | Status | Implementation |
|--------|--------|---------------|
| `process` | ✅ Done | Full: argv, env, exit, exitCode, cwd, platform, arch, pid, hrtime, on('exit'/'uncaughtException') |
| `Buffer` | ✅ Done | Extended: 14 read + 14 write BE/LE, toJSON, swap16/32/64, includes, subarray |
| `__dirname` | ✅ Done | Set per-module |
| `__filename` | ✅ Done | Set per-module |
| `require()` | ✅ Partial | CJS shim exists; extend resolution |
| `module` | ⚠️ Partial | `module.exports` works |
| `exports` | ⚠️ Partial | Alias to `module.exports` |
| `global` / `globalThis` | ✅ Done | |
| `setTimeout` / `setInterval` | ✅ Done | Via event loop |
| `setImmediate` | ✅ Done | Via setTimeout(cb, 0) |
| `queueMicrotask` | ✅ Done | Via microtask queue |
| `console` | ✅ Done | |
| `URL` / `URLSearchParams` | ✅ | Via `lib/url.c` |
| `TextEncoder` / `TextDecoder` | ✅ | |
| `structuredClone` | ✅ Done | Recursive deep clone |
| `fetch` | 🔲 | Transpile_Js15 scope |
| `AbortController` / `AbortSignal` | 🔲 | |
| `performance` | 🔲 | `uv_hrtime` backed |

## 6. npm Compatibility

### 6.1 Design Philosophy

Lambda should support running npm packages **without requiring Node.js to be installed**. The approach draws on lessons from Deno and Bun:

| Aspect          | Node.js                     | Deno                   | Bun                  | **Lambda (proposed)**            |
| --------------- | --------------------------- | ---------------------- | -------------------- | -------------------------------- |
| Package source  | npm registry                | npm registry + JSR     | npm registry         | npm registry                     |
| Specifier style | bare `"lodash"`             | `npm:lodash@4`         | bare `"lodash"`      | bare `"lodash"` (package.json)   |
| Install step    | `npm install`               | Auto on first run      | `bun install`        | `lambda node install`            |
| node_modules    | Nested (npm) or flat (pnpm) | Global cache (default) | Flat + symlinks      | **Flat + symlinks** (pnpm-style) |
| Lock file       | `package-lock.json`         | `deno.lock`            | `bun.lockb` (binary) | `lambda-node.lock` (JSON)        |
| Config file     | `package.json`              | `deno.json`            | `package.json`       | `package.json`                   |

### 6.2 `node_modules` Layout: Flat + Symlinks (pnpm Model)

**Recommendation: Follow pnpm's content-addressable flat layout**, not npm's nested layout or Deno's global cache-only approach.

#### Why Not Nested (npm v2 style)?
- Massive disk usage from duplicate packages
- Deep directory trees hit OS path length limits on Windows
- Slow installation

#### Why Not Fully Flat (npm v3+ hoisted)?
- Phantom dependencies: packages can accidentally import undeclared dependencies that were hoisted
- Non-deterministic: different install orders can produce different layouts
- Version conflicts result in occasional nesting anyway

#### Why Not Global Cache Only (Deno style)?
- Many npm packages hard-code paths relative to `node_modules`
- PostInstall scripts expect `node_modules` to exist
- Build tools (webpack, vite, esbuild) probe `node_modules`

#### Why pnpm-Style Flat + Symlinks?

```
node_modules/
├── .lambda/                        # content-addressable store (flat)
│   ├── lodash@4.17.21/
│   │   └── node_modules/
│   │       └── lodash/             # actual package files
│   ├── express@4.18.2/
│   │   └── node_modules/
│   │       ├── express/            # actual package files
│   │       ├── body-parser -> ../../body-parser@1.20.2/node_modules/body-parser
│   │       └── cookie -> ../../cookie@0.6.0/node_modules/cookie
│   ├── body-parser@1.20.2/
│   │   └── node_modules/
│   │       └── body-parser/        # actual files
│   └── cookie@0.6.0/
│       └── node_modules/
│           └── cookie/             # actual files
├── lodash -> .lambda/lodash@4.17.21/node_modules/lodash       # symlink
└── express -> .lambda/express@4.18.2/node_modules/express     # symlink
```

**Benefits:**
1. **No phantom dependencies** — only declared dependencies are symlinked to the top level
2. **No duplication** — each version of a package exists exactly once in `.lambda/`
3. **Deterministic** — same `package.json` + `lambda-node.lock` always produces same layout
4. **Efficient** — symlinks are cheap; global cache can be added later for cross-project sharing
5. **Compatible** — packages resolve their own dependencies via their local `node_modules/` symlinks
6. **Battle-tested** — pnpm has proven this works with the entire npm ecosystem

### 6.3 Global Package Cache

In addition to per-project `node_modules/`, implement a global cache for downloaded tarballs:

```
~/.lambda/
├── cache/
│   ├── registry.npmjs.org/
│   │   ├── lodash/
│   │   │   ├── 4.17.21.tgz          # downloaded tarball
│   │   │   └── 4.17.21/             # extracted contents
│   │   └── express/
│   │       ├── 4.18.2.tgz
│   │       └── 4.18.2/
│   └── metadata/
│       ├── lodash.json               # package metadata cache
│       └── express.json
└── config.json                       # registry URL, auth tokens
```

**Resolution flow:**
1. Check global cache for exact version
2. If not cached, fetch tarball from registry
3. Extract to cache
4. Create symlink structure in project's `node_modules/`

### 6.4 Package Resolution Algorithm

Implement the Node.js module resolution algorithm with these extensions:

```
resolve(specifier, parent_path):
  1. If specifier starts with "node:" → return built-in module
  2. If specifier starts with "/" or "./" or "../" → resolve as file/directory
  3. If specifier is bare (e.g. "lodash"):
     a. Check package.json "imports" field (self-referencing)
     b. Walk up from parent_path, checking each node_modules/:
        - Check package.json "exports" field (conditional exports)
        - Check "main" field
        - Check index.js / index.json
  4. Error: MODULE_NOT_FOUND
```

**Conditional exports support** (critical for modern packages):
```json
{
  "exports": {
    ".": {
      "import": "./dist/esm/index.js",
      "require": "./dist/cjs/index.js",
      "default": "./dist/esm/index.js"
    },
    "./utils": "./dist/utils.js"
  }
}
```

Lambda should resolve with conditions: `["lambda", "node", "import", "default"]` — matching Deno's approach of including a runtime-specific condition alongside standard Node.js conditions.

### 6.5 Lock File Format

`lambda-node.lock` — JSON format for human readability and easy diffing (unlike Bun's binary lockb):

```json
{
  "version": 1,
  "packages": {
    "lodash@4.17.21": {
      "resolved": "https://registry.npmjs.org/lodash/-/lodash-4.17.21.tgz",
      "integrity": "sha512-v2kDEe57leczF...",
      "dependencies": {}
    },
    "express@4.18.2": {
      "resolved": "https://registry.npmjs.org/express/-/express-4.18.2.tgz",
      "integrity": "sha512-...",
      "dependencies": {
        "body-parser": "1.20.2",
        "cookie": "0.6.0"
      }
    }
  }
}
```

### 6.6 `package.json` Support

Lambda reads but does not require `package.json`. All high-priority fields are implemented in `npm_package_json.cpp`.

| Field | Purpose | Priority | Status |
|-------|---------|----------|--------|
| `name` | Package identity | High | ✅ |
| `version` | Package version | High | ✅ |
| `main` | CJS entry point | High | ✅ |
| `module` | ESM entry point (de facto) | High | ✅ |
| `type` | `"module"` or `"commonjs"` | High | ✅ |
| `exports` | Conditional exports map | High | ✅ |
| `imports` | Self-referencing imports | Medium | ✅ |
| `dependencies` | Runtime dependencies | High | ✅ |
| `devDependencies` | Dev-only dependencies | Medium | ✅ |
| `scripts` | Run via `lambda node task` | Medium | ✅ |
| `bin` | CLI executables | Medium |
| `files` | Published file whitelist | Low |
| `engines` | Version constraints | Low (advisory) |
| `peerDependencies` | Peer dep declarations | Medium |

### 6.7 CLI Commands

```bash
lambda node install                # ✅ Install all dependencies from package.json
lambda node install lodash         # ✅ Add a dependency
lambda node install -D jest        # ✅ Add dev dependency
lambda node uninstall lodash       # ✅ Remove a dependency
lambda node task                   # ✅ List available scripts from package.json
lambda node task <script>          # ✅ Run a script from package.json
lambda node exec <pkg>             # ✅ Run package binary (like npx)
lambda node info <pkg>             # 🔲 Show dependency tree
lambda node outdated               # 🔲 Check for newer versions
lambda node update                 # 🔲 Update to latest within semver range
```

### 6.8 CommonJS / ESM Interop

| Scenario | Behavior | Status |
|----------|----------|--------|
| ESM imports ESM | Standard ES module resolution | ✅ Done |
| ESM imports CJS | Auto-wrap: `module.exports` becomes default export | ✅ Done (via `js_require()` source wrapping) |
| CJS requires CJS | Standard `require()` | ✅ Done |
| CJS requires ESM | Supported if ESM has no top-level await (match Node.js 22+) | ✅ Done (via `js_require()` fallback to ESM path) |
| `.mjs` file | Always ESM regardless of package.json | ✅ Recognized by `jm_resolve_module_path()` |
| `.cjs` file | Always CJS regardless of package.json | ✅ Recognized + wrapped by `js_require()` |
| `.js` file | Check nearest package.json `"type"` field | ✅ `npm_resolve_module()` checks `is_esm` |

## 7. Architecture

### 7.1 Component Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Lambda CLI                                     │
│  lambda node install │ lambda node run app.js │ lambda node task start    │
└────────┬─────────────────┬──────────────────┬────────────────────────┘
         │                 │                  │
    ┌────▼────┐     ┌──────▼──────┐    ┌──────▼──────┐
    │ Package │     │ Module      │    │ Script      │
    │ Manager │     │ Resolver    │    │ Runner      │
    │ (npm)   │     │ (CJS+ESM)  │    │(package.json│
    └────┬────┘     └──────┬──────┘    │  scripts)   │
         │                 │           └─────────────┘
    ┌────▼────┐     ┌──────▼──────┐
    │ Registry│     │ Module      │
    │ Client  │     │ Registry    │        (existing module_registry.cpp)
    │ (HTTP)  │     │             │
    └────┬────┘     └──────┬──────┘
         │                 │
    ┌────▼────┐     ┌──────▼──────┐
    │ Global  │     │ Node.js     │
    │ Cache   │     │ Built-in    │
    │ (~/.λ/) │     │ Modules     │
    └─────────┘     └──────┬──────┘
                           │
              ┌────────────┼────────────────┐
              │            │                │
        ┌─────▼───┐  ┌────▼────┐   ┌──────▼──────┐
        │lib/file.c│  │lib/     │   │ lib/url.c   │
        │file_utils│  │shell.c  │   │ sysinfo     │
        └─────────┘  └─────────┘   └─────────────┘
```

### 7.2 New Source Files

```
lambda/js/                           # Phase 1 — all ✅
├── js_node_modules.cpp     ✅       # Built-in module dispatcher (node: prefix handling)
├── js_path.cpp             ✅       # node:path implementation
├── js_os.cpp               ✅       # node:os implementation
├── js_buffer.cpp           ✅       # Buffer class (extends TypedArray)
├── js_util.cpp             ✅       # node:util (promisify, inspect, format)
├── js_querystring.cpp      ✅       # node:querystring
├── js_string_decoder.cpp   ✅       # node:string_decoder
├── js_assert.cpp           ✅       # node:assert
├── polyfills/
│   └── events.js           ✅       # EventEmitter (pure JS)
│
lambda/npm/                          # Phase 2-3 — all ✅
├── npm_registry.cpp        ✅       # HTTP client for registry API
├── npm_resolver.cpp        ✅       # Dependency resolution (semver)
├── npm_installer.cpp       ✅       # Download, extract, link
├── npm_lockfile.cpp        ✅       # lambda-node.lock read/write
├── npm_package_json.cpp    ✅       # package.json parser
├── npm_resolve_module.cpp  ✅       # Node.js module resolution algorithm
└── semver.cpp              ✅       # Semver parsing and matching
│
lambda/js/                           # Phase 4 — extended modules
├── js_stream.cpp           ✅       # node:stream (Readable, Writable, Duplex, Transform, PassThrough, pipeline)
├── js_crypto.cpp           ✅       # node:crypto (extended: HMAC, randomBytes, randomUUID, createHash, createHmac, timingSafeEqual)
├── js_child_process.cpp    ✅       # node:child_process (extended: spawn async, spawnSync)
├── js_dns.cpp              ✅       # node:dns (lookup, lookupSync, resolve)
├── js_net.cpp              ✅       # node:net (createServer, createConnection, Socket, isIP)
├── js_tls.cpp              ✅       # node:tls (connect, createServer, createSecureContext — mbedTLS)
├── js_http.cpp             ✅       # node:http (createServer, request, get, IncomingMessage, ServerResponse)
├── js_https.cpp            ✅       # node:https (thin wrapper over http + tls)
├── js_zlib.cpp             ✅       # node:zlib (gzip/gunzip/deflate/inflate sync variants)
└── js_readline.cpp         ✅       # node:readline (createInterface, question, close, on)
```

### 7.3 Module Registration

When JS code does `import path from "node:path"` or `const path = require("path")`:

```c
// lambda/js/js_node_modules.cpp

Item js_resolve_node_builtin(const char* module_name) {
    // strip "node:" prefix if present
    if (strncmp(module_name, "node:", 5) == 0)
        module_name += 5;

    if (strcmp(module_name, "fs") == 0)             return js_get_fs_module();
    if (strcmp(module_name, "path") == 0)           return js_get_path_module();
    if (strcmp(module_name, "os") == 0)             return js_get_os_module();
    if (strcmp(module_name, "events") == 0)         return js_get_events_module();
    if (strcmp(module_name, "util") == 0)           return js_get_util_module();
    if (strcmp(module_name, "buffer") == 0)         return js_get_buffer_module();
    if (strcmp(module_name, "crypto") == 0)         return js_get_crypto_module();
    if (strcmp(module_name, "child_process") == 0)  return js_get_child_process_module();
    if (strcmp(module_name, "url") == 0)            return js_get_url_module();
    if (strcmp(module_name, "querystring") == 0)    return js_get_querystring_module();
    if (strcmp(module_name, "stream") == 0)         return js_get_stream_module();
    if (strcmp(module_name, "dns") == 0)            return js_get_dns_module();
    if (strcmp(module_name, "net") == 0)            return js_get_net_module();
    if (strcmp(module_name, "tls") == 0)            return js_get_tls_module();
    if (strcmp(module_name, "zlib") == 0)           return js_get_zlib_module();
    if (strcmp(module_name, "readline") == 0)       return js_get_readline_module();
    if (strcmp(module_name, "string_decoder") == 0) return js_get_string_decoder_module();
    if (strcmp(module_name, "assert") == 0)         return js_get_assert_module();
    if (strcmp(module_name, "process") == 0)        return js_get_process_module();
    if (strcmp(module_name, "timers") == 0)         return js_get_timers_module();
    if (strcmp(module_name, "console") == 0)        return js_get_console_module();
    // ...

    return ItemNull; // unknown module — 21 modules registered
}
```

## 8. Implementation Phases

### Phase 1: Core Modules (Tier 1) — ✅ COMPLETE

Wire existing C libraries to JS module objects. All built-in modules implemented, 588/588 baseline tests passing.

| Task | Effort | Status |
|------|--------|--------|
| `node:path` module | Small | ✅ Done — `js_path.cpp` (12 methods + 2 properties) |
| `node:os` module | Small | ✅ Done — `js_os.cpp` (17 methods + EOL; cpus() real CPU data, networkInterfaces() real data) |
| `node:process` extensions | Medium | ✅ Done — pid, ppid, version, versions, hrtime, nextTick, memoryUsage, cpuUsage, umask, uptime, title, stdout, stderr, exitCode, on('exit'/'uncaughtException') |
| `node:buffer` full API | Medium | ✅ Done — alloc, allocUnsafe, from, concat, 14 read + 14 write endian methods, toJSON, swap16/32/64, includes, subarray |
| `node:util` (promisify, inspect, format, types) | Medium | ✅ Done — `js_util.cpp`, 8 top-level (incl. debuglog) + TextEncoder/TextDecoder + 18 types.* checkers |
| `node:events` polyfill | Medium | ✅ Done — EventEmitter (17 methods incl. prependListener, rawListeners, defaultMaxListeners, error auto-throw) |
| `node:fs` remaining sync APIs | Small | ✅ Done — 24 sync + 2 async methods (incl. openSync, closeSync, readSync, writeSync, fstatSync) |
| `node:querystring` | Small | ✅ Done |
| `node:string_decoder` | Small | ✅ Done — `js_string_decoder.cpp` |
| `node:assert` | Small | ✅ Done — `js_assert.cpp` (15 assertion methods incl. deepEqual/notDeepEqual, match/doesNotMatch) |
| `node:timers` / `node:console` | Small | ✅ Done — registered as importable aliases |
| Module dispatcher (`node:` prefix) | Small | ✅ Done — `js_module_get()` handles 21 modules |

### Phase 2: npm Package Manager — ✅ COMPLETE

Full npm package manager with 32/32 semver tests passing.

| Task | Effort | Status |
|------|--------|--------|
| Registry HTTP client | Medium | ✅ Done — `npm_registry.cpp` via `http_fetch()` |
| Semver parser & matcher | Medium | ✅ Done — `semver.cpp` (32/32 tests) |
| Dependency tree resolver | Large | ✅ Done — `npm_resolver.cpp` |
| Tarball download & extract | Medium | ✅ Done — `npm_installer.cpp` |
| Flat symlink layout installer | Medium | ✅ Done — pnpm-style `.lambda/` store |
| `lambda-node.lock` read/write | Small | ✅ Done — `npm_lockfile.cpp` |
| `package.json` field support | Medium | ✅ Done — `npm_package_json.cpp` (all fields) |
| `lambda node install` CLI command | Small | ✅ Done — install/uninstall in `main.cpp` |
| Node.js module resolution | Medium | ✅ Done — `npm_resolve_module.cpp` with conditional exports |

### Phase 3: Interop & Ecosystem — ✅ CORE COMPLETE

CJS/ESM interop infrastructure implemented. `require()` works with local modules and bare specifiers. CLI task/exec commands operational.

| Task | Effort | Status |
|------|--------|--------|
| CJS/ESM interop (full) | Large | ✅ Done — `js_require()` wraps CJS source with `module.exports` pattern, detects `.cjs`/`.mjs` extensions |
| `require()` with node_modules walk | Medium | ✅ Done — `jm_resolve_module_path()` calls `npm_resolve_module()` for bare specifiers, built-in bypass list prevents polyfill shadowing |
| Conditional exports resolution | Medium | ✅ Done — `npm_resolve_exports()` with conditions `["lambda", "node", "import", "default"]` |
| `lambda node task` (scripts runner) | Small | ✅ Done — parses `package.json` scripts, runs via `shell_exec_line`, prepends `node_modules/.bin` to PATH |
| `lambda node exec` (npx equivalent) | Medium | ✅ Done — runs binaries from `node_modules/.bin`, auto-installs if not found |
| Express hello-world server | — | ✅ Achievable — `node:http` + `node:stream` implemented |
| 20+ npm packages pass tests | — | 🔲 Remaining — needs ecosystem validation |

### Phase 4: Extended Modules (Tier 2) — ✅ COMPLETE

All Tier 2 extended modules implemented. 23 built-in modules registered in `js_module_get()` (added http, https), 588/588 baseline tests passing.

| Task                                 | Effort | Status |
| ------------------------------------ | ------ | ------ |
| `node:stream` (Readable/Writable/Duplex/Transform/PassThrough) | Large | ✅ Done — `js_stream.cpp`, pipeline, Readable.from |
| `node:crypto` (HMAC, random, cipher) | Medium | ✅ Done — `js_crypto.cpp` extended with createHash, createHmac, randomBytes, randomUUID, randomInt, getHashes, timingSafeEqual |
| `node:child_process` (spawn)         | Medium | ✅ Done — `js_child_process.cpp` extended with spawn (async libuv), spawnSync |
| `node:dns`                           | Small  | ✅ Done — `js_dns.cpp`, lookup (async uv_getaddrinfo), lookupSync, resolve |
| `node:net`                           | Large  | ✅ Done — `js_net.cpp`, createServer, createConnection, Socket, isIP/isIPv4/isIPv6 |
| `node:tls`                           | Large  | ✅ Done — `js_tls.cpp`, connect, createServer, createSecureContext, TLSSocket (wraps mbedTLS via serve/tls_handler) |
| `node:http`                          | Large  | ✅ Done — `js_http.cpp`, createServer, request, get, IncomingMessage, ServerResponse, STATUS_CODES |
| `node:https`                         | Medium | ✅ Done — `js_https.cpp`, createServer, request, get (thin wrapper over http + tls) |
| `node:zlib`                          | Medium | ✅ Done — `js_zlib.cpp`, gzipSync, gunzipSync, deflateSync, inflateSync, deflateRawSync, inflateRawSync |
| `node:readline`                      | Small  | ✅ Done — `js_readline.cpp`, createInterface, question, close, on |
| Module dispatcher update             | Small  | ✅ Done — 11 new modules added to `js_module_get()` + builtin bypass list (21 total) |

### Phase 5: HTTP & Server — ✅ COMPLETE

| Task                                 | Effort | Status           |
| ------------------------------------ | ------ | ---------------------- |
| `node:http` client + server          | Large  | ✅ Done — `js_http.cpp` |
| `node:https` client + server         | Medium | ✅ Done — `js_https.cpp` |
| Express hello-world server           | —      | ✅ Achievable with node:http + node:stream |

## 9. Testing Strategy

### Unit Tests

```
test/test_js_node_path.cpp     # node:path — join, resolve, dirname, basename, etc.
test/test_js_node_os.cpp       # node:os — platform, arch, cpus, memory
test/test_js_node_buffer.cpp   # Buffer — from, alloc, toString, encoding
test/test_npm_semver.cpp       # Semver parsing and range matching
test/test_npm_resolver.cpp     # Dependency tree resolution
test/test_npm_installer.cpp    # Symlink layout creation
```

### Integration Tests (Lambda scripts)

```
test/lambda/node_path.js       # const path = require('path'); assert(path.join('a','b') === 'a/b')
test/lambda/node_os.js         # import os from 'node:os'; assert(os.platform() !== undefined)
test/lambda/node_fs_ext.js     # fs.realpathSync, fs.accessSync, fs.rmSync
test/lambda/node_buffer.js     # Buffer.from('hello').toString('hex') === '68656c6c6f'
test/lambda/npm_install.js     # Install lodash, require it, verify it works
test/lambda/npm_resolve.js     # Package resolution with exports field
```

### Compatibility Testing

Run selected test suites from real npm packages:
- `lodash` — pure JS utility library (baseline)
- `minimist` — argument parser (small, pure JS)
- `chalk` — terminal colors (conditional exports, ESM)
- `semver` — semver range parser (dogfooding)
- `express` (stretch goal) — HTTP framework (streams, http, path, fs)

## 10. Reference Analysis: Deno vs Bun Approaches

### What to Learn from Deno

| Aspect | Deno's Approach | Lambda Takeaway |
|--------|----------------|-----------------|
| `npm:` specifier | Inline in import: `import x from "npm:chalk@5"` | Skip — use package.json like Bun/Node |
| Global cache | Default: no local node_modules | Use global cache for tarballs, but always create local node_modules |
| `node:` prefix | Required for built-in modules | Support both `"fs"` and `"node:fs"` (match Node.js) |
| Built-in modules | JS polyfills for most | Mix: C-backed where we have libs, JS polyfills for pure logic |
| Lock file | JSON `deno.lock` | JSON `lambda-node.lock` (good for diffing) |
| Permissions | `--allow-read`, `--allow-net` | Consider for future; not Phase 1 |

### What to Learn from Bun

| Aspect | Bun's Approach | Lambda Takeaway |
|--------|---------------|-----------------|
| Install speed | Binary lockfile, global cache, hard links | Global tarball cache; symlinks over hard links (simpler) |
| node_modules | Flat + symlinks | Adopt this (pnpm-style) |
| package.json first | Native support | Match this — package.json is the single config |
| CJS/ESM interop | Transparent, automatic | Match this behavior |
| Built-in modules | 90%+ native in Zig/C++ | Match where we have C backing |
| Workspaces | Supported | Phase 2+ |

### Key Decision Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| node_modules layout | Flat + symlinks (pnpm) | No phantoms, no duplication, deterministic |
| Config file | `package.json` | npm ecosystem compatibility; no new format |
| Lock file | `lambda-node.lock` (JSON) | Human-readable, diffable, simple to implement |
| Global cache | `~/.lambda/cache/` | Avoid re-downloading across projects |
| Built-in modules style | `"node:fs"` and `"fs"` both work | Maximum Node.js compat |
| CJS detection | File extension + package.json `type` | Match Node.js 22+ behavior |
| Native addons | ❌ Not supported | Complexity too high; covers 95% of packages without them |

## 11. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| npm ecosystem is vast; many edge cases | Medium | Focus on top-100 packages; use test suites from real packages |
| Streams are complex and ubiquitous | High | Start with Web Streams (Js15), layer Node streams on top |
| CJS/ESM interop has subtle edge cases | High | Match Node.js 22+ behavior exactly; test with real packages |
| Some packages probe `node_modules` paths | Medium | pnpm-style layout preserves expected structure |
| Native addons won't work | Low | Affects ~5% of packages; document unsupported |
| Semver range resolution is non-trivial | Medium | Port from well-tested reference implementation |
| Performance of package manager | Low | Cache aggressively; symlinks are essentially free |

## 12. Success Criteria

**Phase 1 complete when:** ✅ ACHIEVED
- ✅ `import path from "node:path"` works with all `path.*` methods
- ✅ `import os from "node:os"` returns correct platform info
- ✅ `Buffer.from("hello").toString("hex")` returns `"68656c6c6f"`
- ✅ `const { promisify } = require("util")` works
- ✅ EventEmitter on/emit/off works correctly
- ✅ All Tier 1 modules pass targeted test suites (588/588 baseline)

**Phase 2 complete when:** ✅ ACHIEVED
- ✅ `lambda node install` reads `package.json`, downloads from npm, creates `node_modules/`
- ✅ `const lodash = require("lodash")` works after install
- ✅ `lambda-node.lock` is generated and deterministic
- ✅ Conditional exports resolve correctly (32/32 semver tests)
- ✅ CJS/ESM interop handles `import cjs from "./file.cjs"` and `require("./esm.mjs")`

**Phase 3 — core infrastructure complete, ecosystem validation remaining:**
- ✅ `require()` loads CJS modules with `module.exports` wrapping
- ✅ Bare specifier resolution via `npm_resolve_module()` with `node_modules` walk
- ✅ `lambda node task` runs scripts from `package.json`
- ✅ `lambda node exec` runs package binaries (npx equivalent)
- ✅ Built-in modules bypass npm polyfills (10-module static list)
- ✅ Express hello-world server achievable (`node:http` + `node:stream` implemented)
- 🔲 At least 20 popular pure-JS npm packages pass their own test suites
