# Lambda Native Module System (Jube Modules) — Design Proposal

> **Status**: proposal (discussed & direction approved 2026-07). POC targets: `radiant-dom` module and `node-*` module refactor.
> **Scope**: C/C++-level modules for the unified Lambda runtime — loadable at runtime (DLL/dylib/so), serving **all** front-ends (Lambda, JS, Node, Python, Ruby, Bash, future languages). This is *not* about script-level packages (those are covered in §7.3 only as far as distribution policy).

---

## 1. Goals

1. **C-level module system** ("Jube modules") — the extension unit is a native library, not a script package.
2. **Language-neutral**: one module registers once; every front-end on the unified runtime can call it.
3. **Runtime-loadable**: modules load as shared libraries (`.dylib`/`.so`/`.dll`) discovered on disk; no rebuild of `lambda.exe`.
4. **Open-ended module kinds**: a module can be a new language transpiler, a DSL, a native library (spell checker, raster imaging), an input parser/output formatter, or a script-visible namespace (e.g. Node's `fs`).
5. **Registry-ready**: local discovery now; the design leaves a clean slot for a future online registry without rework.
6. **Kill the hardcoded registry**: replace the monolithic `sys_func_registry.c` + inline dispatch chains with per-module, data-driven registration.

### Non-goals

- Distributing compiled MIR. MIR stays a **private, in-memory IR** with no compatibility promise (decision recorded in §7.3).
- Sandboxing/untrusted-code isolation. A Jube module is trusted native code, same as today's built-ins. (Integrity checking via manifest hash is in scope; capability sandboxing is not.)
- Script-level package management semantics (semver dep solving, lockfiles). Manifest reserves the fields; implementation is future work.

---

## 2. Prior art survey

The problem — *native extensions for a language runtime, loaded at runtime, with a stable interface* — is one of the most well-trodden in systems design. We survey the six most instructive systems and distill the practices Lambda should adopt.

### 2.1 SQLite loadable extensions

The minimal, proven shape. An extension exports one well-known symbol:

```c
int sqlite3_extension_init(sqlite3 *db, char **pzErrMsg,
                           const sqlite3_api_routines *pApi);
```

- **One entry point**, name derived from the file name (`sqlite3_<name>_init`) — no export-table scanning.
- **The host passes in the API** as a struct of function pointers (`sqlite3_api_routines`); the extension calls the host *only* through it (wrapped by macros so extension code looks like normal SQLite calls). The extension never links against the host binary — this is what makes it work on Windows, where executables don't export symbols.
- Extensions **register capabilities** by calling host functions (`sqlite3_create_function`, `sqlite3_create_module` for virtual tables, `sqlite3_create_collation`) — the host owns the registry; the module feeds it.
- Auto-init: statically linked extensions use the exact same init function — **static and dynamic modules share one registration path**.

**Adopt**: single entry symbol; host-API-as-struct; capabilities registered into host-owned registries; static/dynamic symmetry.

### 2.2 Node.js N-API (Node-API)

The strongest answer to *ABI stability across runtime versions*:

- All host services flow through an opaque `napi_env` handle + flat C functions (`napi_create_string_utf8(env, ...)`). Internal V8 types **never** cross the boundary.
- **Versioned, additive-only API**: `NAPI_VERSION` gates features; functions are never removed or re-typed. Addons built years ago load on current Node.
- Values are opaque handles (`napi_value`) with lifetime tied to **handle scopes**; long-lived references use explicit `napi_ref` (create/delete) — the GC contract is explicit API, not convention.
- Native objects wrap into JS via `napi_wrap` with a **finalizer callback** — the GC tells native code when to free.
- Errors: every call returns `napi_status`; JS exceptions are a *pending* flag queried via API — no unwinding across the boundary (exactly LambdaJS's pending-exception model).

**Adopt**: versioned additive-only host API; explicit GC rooting/reference API; finalizers for native resources; status-code error discipline at the boundary.

### 2.3 CPython C extensions

Instructive both positively and negatively:

- `PyInit_<name>()` returns a `PyModuleDef` — a **descriptor struct**: name, doc, per-module state size, method table (`PyMethodDef[]`: name, C function, calling-convention flags, doc). Function metadata is *data*, not code — the host builds the namespace from the table.
- **Capsules** (`PyCapsule`) — a void pointer + name string + destructor — the canonical way to pass branded opaque native pointers through the object system. Direct precedent for our branded native VMap wrappers (§6.3).
- Calling conventions are declared per-function (`METH_VARARGS`, `METH_FASTCALL`, `METH_O`) — modules opt into faster conventions per function.
- **Negative lesson**: the default C API exposes internals (struct layouts, refcount macros), so every minor release breaks extensions; the "Limited API/stable ABI" (`Py_LIMITED_API`) was retrofitted and adoption suffered because the full API remained available and faster. **The stable API must be the only API from day one**, or it rots.

**Adopt**: descriptor tables (`PyMethodDef` → `JubeFuncDef`); capsules → branded native wrappers (realized as VMap projections, §6.3); per-function calling conventions. **Avoid**: shipping an "internal API" beside the stable one for external modules.

### 2.4 Lua C modules

The minimalist benchmark:

- `luaopen_<name>(lua_State*)` returns the module table; `luaL_Reg` arrays ({name, fn} pairs) register functions.
- One universal calling convention: `int fn(lua_State *L)` — args on a stack, count returned. Trivially uniform; every binding generator targets it.
- **Userdata + metatables**: native memory blocks owned by the GC, branded by metatable identity, with `__index` metamethods projecting properties and `__gc` finalizers — 30 years of proof that *branded native wrapper + property projection + finalizer* covers essentially all native-interop needs without raw pointers in signatures. This maps one-to-one onto Lambda's existing VMap (vtable = metatable, vtable identity = brand, `destroy` = `__gc`) — §6.3.
- `require` merges C modules and script modules into **one namespace/search path** (`package.cpath` beside `package.path`) — users don't care whether `import foo` is native or script.

**Adopt**: uniform boxed calling convention as the default; branded userdata model; unified import namespace across native/script modules.

### 2.5 PostgreSQL extensions

The best model for *manifest-driven discovery* and *declared interfaces*:

- A **magic block** (`PG_MODULE_MAGIC`) embeds ABI-relevant build parameters in the library; the loader rejects mismatches *before* calling any code. Version checks are structural, not honor-system.
- Each C function carries a mandatory metadata macro (`PG_FUNCTION_INFO_V1`) declaring its calling convention — the host refuses undeclared functions.
- The **control file** (`extension.control`: name, version, dependencies) is read *without loading the library* — discovery and dependency resolution never execute foreign code.
- The function's **type signature lives in the host's own type language** (`CREATE FUNCTION ... RETURNS text AS 'module', 'symbol'`) — the interface is declared in SQL types, the C side only exports symbols. This is the direct precedent for declaring Jube signatures in *Lambda type syntax* (§6.2).

**Adopt**: magic/ABI block checked at load; manifest readable without dlopen; **interfaces declared in the host language's type syntax**.

### 2.6 Tree-sitter grammars (already in-tree)

Lambda already ships multiple Tree-sitter grammars, and Tree-sitter's own dynamic-loading convention is directly relevant for **language modules**:

- Each grammar compiles to a shared library exporting `tree_sitter_<lang>()` returning a `const TSLanguage*` — a pure data+function-table object.
- The `TSLanguage` struct carries an **ABI version number** checked by the runtime library before use.
- Grammar `.so` files are cached in a user directory (`~/.tree-sitter/`) keyed by grammar content — a working example of the *derived, regenerable native cache* pattern we adopt for script compilation caching (§7.4).

**Adopt**: language modules ship their `TSLanguage` the same way; the local-cache-keyed-by-content pattern.

### 2.7 Distilled good practices (the checklist this design follows)

| # | Practice | Source |
|---|----------|--------|
| P1 | One well-known entry symbol per module | SQLite, Python, Lua, Tree-sitter |
| P2 | Host API = versioned struct/handle passed *in*; module never links host symbols | SQLite, N-API |
| P3 | ABI/version check happens before any module code runs | Postgres magic block, Tree-sitter |
| P4 | Function interface = **descriptor data**, not discovery-by-symbol-scan | Python `PyMethodDef`, Lua `luaL_Reg` |
| P5 | Signatures declared in the **host's type language** | Postgres `CREATE FUNCTION` |
| P6 | Uniform boxed calling convention by default; faster typed conventions opt-in per function | Lua, Python `METH_FASTCALL` |
| P7 | Native pointers cross as **branded native wrappers with finalizers** (Lambda: VMap projections), never raw | Python capsules, Lua userdata, `napi_wrap` |
| P8 | Explicit GC reference/rooting API; no rooting-by-convention | N-API refs/handle scopes |
| P9 | Errors are status/sentinel at the boundary; no unwinding across it | N-API, (matches Lambda `ItemError` / JS pending-flag) |
| P10 | Additive-only host API evolution; version-gated features | N-API |
| P11 | **One API tier for everyone** — in-tree modules use the same API external ones do | anti-lesson of CPython |
| P12 | Manifest readable without executing the module | Postgres control file |
| P13 | Static and dynamic modules share one registration path | SQLite auto-init |
| P14 | Native compiled artifacts are **derived local caches**, never distribution formats | Tree-sitter grammar cache, V8 code cache |

---

## 3. Current state and its problems

### 3.1 The three hardcoding layers

Everything a front-end can call lives in **one 2,902-line file**, `lambda/sys_func_registry.c`, feeding three coupled layers:

1. **AST-level metadata** — `sys_func_defs[]` (`sys_func_registry.c:266`): name, arity, `Type*` return, `is_proc`, `can_raise`, method-eligibility, C-level ret/arg conventions, C symbol name. `build_ast.cpp` builds hashmaps over this table (`get_sys_func_info()`, `build_ast.cpp:92–220`) to resolve and type-check calls at compile time.
2. **JIT link table** — `jit_runtime_imports[]` (`sys_func_registry.c:1145`): ~1,650 `{name, fn_ptr}` entries — Lambda runtime helpers, **all** JS runtime helpers (~685 `js_*` entries), plus every Python/Ruby/Bash helper. The file `#include`s `py/*.h`, `bash/*.h`, `rb/*.h`, `js/*.h` (lines 176–196): every language runtime is compile-time-coupled to one translation unit.
3. **Link-time resolution** — `import_resolver()` (`mir.c:106`): checks a thread-local **dynamic import map** (`register_dynamic_import()`, `mir.c:91`) first, then the static table.

### 3.2 Concrete pain points

- **Adding one builtin touches 3+ places** in shared files: the function's own `.cpp`, `sys_func_defs[]` (if script-visible with typing), `jit_runtime_imports[]`, plus `build_ast.cpp` special cases for some. Merge conflicts concentrate in one file across every workstream.
- **The `LAMBDA_STATIC`/`FPTR` stub hack** (`sys_func_registry.h:18–25`): dylib/input-only builds can't link the full runtime, so all function pointers are swapped for a dummy stub via macros — a compile-time symptom that registration data and runtime linkage are fused when they should be separable.
- **Node builtin dispatch is an if/memcmp chain**: `js_module_get_builtin()` (`js_runtime.cpp:37808`) resolves `"fs"` / `"fs.js"` / `"node:fs"` by inline `memcmp` per module, inside a **39,431-line** `js_runtime.cpp`. Builtin-module name lists are duplicated in at least two more places in the same file (`:38158`, `:38785`) and again in the module-batch lowering (`js_mir_module_batch_lowering.cpp:1895`).
- **The DOM bridge is monolithic and JS-only**: `js_dom.cpp` (12,415 lines) + `js_dom_events.cpp` (2,145) + `js_dom_selection.cpp` (1,825) + `js_cssom.cpp` (1,518) ≈ **18k lines** wired through the central registry, with Radiant coupling flowing through `edit_bridge.cpp` relying on C linkage "matching sys_func_registry.c declarations" (`edit_bridge.cpp:822`). Lambda-side scripts have no modular access to the same engine.
- **Node compat can't be trimmed or extended**: `js_fs` (3,855 lines), `js_http` (6,024), `js_crypto` (8,711), `js_buffer` (3,084), `js_child_process` (3,734)… all statically linked always, all registered centrally. A user who needs only `path` still carries `crypto`'s OpenSSL binding; adding a new builtin means editing the memcmp chain.
- **No loading infrastructure**: the only `dlopen` in the tree is `js_crypto.cpp`'s lazy OpenSSL binding (`js_crypto.cpp:4972`) — ad-hoc, single-purpose, but proof the pattern already earns its keep in-tree.

### 3.3 What's already right (build on it, don't replace it)

- **`import_resolver`'s dynamic map is the extension hook.** MIR resolves imports *by name at link time*; anything registered via `register_dynamic_import()` before `MIR_link` is callable from JIT'd code. A loaded module only has to get its symbols into that map. Layer 3 needs no redesign.
- **`SysFuncInfo` is the right idea in the wrong shape** — it already captures exactly what a front-end needs (typing, arity, conventions, can_raise). The fix is making it *per-module data* with a declarative signature, not inventing a different metadata model.
- **The interop contract is already documented** (Item ABI, non-moving object zone, relocating data zone, interned names, error tiers — `doc/dev/Lambda_and_JS_Runtime.md` §4). The host API (§6.4) is largely a C projection of contracts that already exist.

---

## 4. Design overview

```
                                  ┌────────────────────────────────────────────┐
                                  │                lambda.exe                   │
   ~/.lambda/modules/…            │                                            │
   ./lambda/…                     │  ┌──────────────┐     ┌─────────────────┐  │
   $LAMBDA_MODULE_PATH            │  │ Module Loader │────▶│ Jube Registry   │  │
  ┌────────────────────┐  scan    │  │ (manifest,    │     │ funcs / langs / │  │
  │ spellcheck/1.2.0/  │─────────▶│  │  dlopen,      │     │ formats / ns    │  │
  │   module.json      │  lazy    │  │  ABI check)   │     └───────┬─────────┘  │
  │   spellcheck.dylib │  dlopen  │  └──────┬───────┘             │            │
  └────────────────────┘          │         │ JubeHostAPI*        │ consumed by│
                                  │         ▼                     ▼            │
                                  │  ┌──────────────┐   ┌───────────────────┐  │
                                  │  │ Jube module  │   │ front-ends:       │  │
                                  │  │ (dylib)      │   │  build_ast (.ls)  │  │
                                  │  │  entry →     │   │  JS transpiler    │  │
                                  │  │  JubeModuleDef│  │  py / rb / bash   │  │
                                  │  └──────────────┘   └─────────┬─────────┘  │
                                  │                               │ emit call  │
                                  │                     ┌─────────▼─────────┐  │
                                  │  register_dynamic_  │  MIR link         │  │
                                  │  import(name,ptr) ─▶│  import_resolver  │  │
                                  │                     └───────────────────┘  │
                                  └────────────────────────────────────────────┘
```

Life of a module function call, end to end:

1. **Discovery**: loader scans module paths, reads `module.json` manifests (no code executed — P12). Registry now knows `spellcheck` exports `fn check(dict: hunspell, word: string) -> bool` and declares native type `hunspell`.
2. **Import**: a script says `import spellcheck` (`.ls`) / `require('jube:spellcheck')` (JS) / `import spellcheck` (py). First import triggers `dlopen` → ABI check → `jube_module_entry()` → descriptor tables registered.
3. **Compile**: the front-end resolves `spellcheck.check` against the registry (replacing today's `get_sys_func_info` monolith lookup), type-checks against the parsed signature, and emits a MIR call to import name `spellcheck.check`.
4. **Link**: `import_resolver` finds `spellcheck.check` in the dynamic import map (registered at load) — existing mechanism, zero changes.
5. **Run**: the JIT'd code calls straight into the module's C function. No trampolines, no marshalling — the module traffics in `Item`s like every built-in today.

---

## 5. The module ABI

### 5.1 Entry point and module descriptor

Each module exports **one** symbol (P1). Everything else is reached through descriptor data (P4):

```c
// jube.h — the ONLY header a module needs. Pure C ABI. No C++ types, no internal headers.

#define JUBE_ABI_VERSION 1

typedef struct JubeModuleDef {
    uint32_t abi_version;            // MUST be first; loader checks before reading further (P3)
    uint32_t struct_size;            // sizeof(JubeModuleDef) as compiled — additive-evolution guard
    const char* name;                // "spellcheck", "radiant-dom", "node-fs", "lang-python"
    const char* version;             // semver string
    const char* description;

    // capability tables — NULL/0 when absent (§5.2)
    const JubeTypeDef*      types;          int32_t type_count;      // native types as VMap projections (§6.3)
    const JubeFuncDef*      functions;      int32_t function_count;
    const JubeNamespaceDef* namespaces;     int32_t namespace_count;
    const JubeLanguageDef*  language;       // at most one per module
    const JubeFormatDef*    formats;        int32_t format_count;

    // lifecycle
    int  (*init)(const JubeHostAPI* host);  // once, after load + ABI check; nonzero = load failure
    void (*shutdown)(void);                 // best-effort at runtime teardown
} JubeModuleDef;

// the single exported symbol:
//   const JubeModuleDef* jube_module_entry(void);
// dynamic loader: dlsym(handle, "jube_module_entry")
// static modules: registered via jube_register_static(&def) at startup (P13)
```

Rules:

- `abi_version` mismatch ⇒ the loader refuses the module with a clear error; no module code has run.
- The returned struct and all tables it points to must be **immutable statics** with module lifetime.
- `init()` receives the host API and is the only place the module may capture it (P2). Returning nonzero aborts the load cleanly (module unlisted, dlclose'd).
- Static/dynamic symmetry (P13): in-tree modules (`core`, `js`, `radiant-dom`, …) provide the *same* `JubeModuleDef` and register through the same code path, just without `dlopen`. One registration mechanism, tested constantly by the built-ins themselves.

### 5.2 Capability kinds

A module declares zero or more of:

**`JubeFuncDef` — script-callable functions** (the workhorse; §6 details the interface):

```c
typedef struct JubeFuncDef {
    const char* name;        // script-visible name within the module namespace
    const char* signature;   // Lambda type syntax, `fn`/`pn`-prefixed — parsed at registration (§6.2)
    fn_ptr      func;        // implementation, default (boxed) convention
    uint32_t    flags;       // JUBE_FN_METHOD_ELIGIBLE | JUBE_FN_VARARGS | ... (purity is NOT a flag — it's in the signature)
    // optional typed fast path (§6.5); NULL/absent for most functions
    const char* native_signature;  // e.g. "(f64, f64) -> f64"
    fn_ptr      native_func;
} JubeFuncDef;
```

**`JubeNamespaceDef` — a lazily-built script-visible namespace object.** This is what Node builtin modules are: `require('fs')` returns an object whose properties are functions. Rather than each front-end hand-building these, a namespace capability maps specifiers to a factory:

```c
typedef struct JubeNamespaceDef {
    const char* const* specifiers;  // e.g. {"fs", "node:fs", NULL} — aliases resolved uniformly
    int32_t specifier_count;
    Item (*build)(void);            // builds the namespace object on first import (cached by host)
    const JubeFuncDef* funcs;       // optional: descriptor-driven instead of/besides build()
    int32_t func_count;
} JubeNamespaceDef;
```

This single capability **replaces the memcmp chains** in `js_module_get_builtin()`: resolution becomes a registry hashmap lookup, and the three duplicated builtin-name lists collapse into the modules' own specifier tables.

**`JubeLanguageDef` — a language/DSL front-end**:

```c
typedef struct JubeLanguageDef {
    const char* const* extensions;      // {".py", NULL}
    const void* (*ts_language)(void);   // Tree-sitter grammar (TSLanguage*), or NULL for hand parser
    // compile source → MIR module registered into the shared context; host links & runs it.
    int (*compile)(JubeCompileCtx* cctx, const char* source, size_t len, const char* path);
} JubeLanguageDef;
```

This is a formalization of what `lambda/py|rb|bash/` already are: Tree-sitter front-end → typed AST → MIR lowering into the shared context. `JubeCompileCtx` (part of the host API) exposes the MIR context, `register_dynamic_import`, and the name pool — the things `transpile_py_mir.cpp` reaches into today.

**`JubeFormatDef` — input parsers / output formatters**:

```c
typedef struct JubeFormatDef {
    const char* format_name;              // "toml", "webp", ...
    const char* const* extensions;
    const char* const* mime_types;
    Item  (*parse)(JubeInputCtx* ictx, const char* data, size_t len);   // builds via Mark C API
    Str*  (*format)(JubeOutputCtx* octx, Item root);                    // either may be NULL
} JubeFormatDef;
```

Registered into `input.cpp`'s dispatcher and the formatter table, so `input()`/`convert` pick up module-provided formats transparently.

Capability kinds are **extensible**: future kinds (e.g. a Radiant paint backend, a `vmap` implementation, a validator plugin) add a new table pointer + count at the end of `JubeModuleDef` — `struct_size` gates old modules safely (P10 applied to the module-side struct too).

---

## 6. The function interface

This answers the core question: *how do modules define their interface, and how do front-ends discover and call it?* Answer: **descriptor tables with signatures in Lambda's own type syntax** (P4 + P5), loaded into the same registries `get_sys_func_info()` and `import_resolver()` already consult.

### 6.1 Why not "just a set of C functions"

A bare symbol list loses exactly the information the runtime lives on: front-ends need types for inference and boxing decisions (`transpile-mir.cpp` emits native `MIR_T_D` math only when it can prove types), arity/overload resolution happens at AST build time, `can_raise` drives error-propagation codegen, and method-eligibility drives `obj.method()` sugar. `SysFuncInfo` carries ~10 raw enum/bool fields for this today. The module interface must carry the same information — but declaratively.

### 6.2 Signatures in Lambda type syntax

Each function declares one string in Lambda's type language — the same syntax scripts use, parsed at registration with the machinery the validator already owns. The signature **must** begin with `fn` (pure function) or `pn` (procedure) — Lambda's purity split is part of the interface, not an afterthought:

```c
static const JubeFuncDef spellcheck_funcs[] = {
    {"open",    "pn (path: string) -> hunspell^",            (fn_ptr)sc_open,    0},
    {"check",   "fn (dict: hunspell, word: string) -> bool", (fn_ptr)sc_check,   JUBE_FN_METHOD_ELIGIBLE},
    {"suggest", "fn (dict: hunspell, word: string) -> [string]", (fn_ptr)sc_suggest, 0},
};
```

(`hunspell` is a module-registered native type — a VMap projection declared via `JubeTypeDef`, §6.3; other modules reference it as `spellcheck.hunspell`.)

- **`fn` vs `pn` is a semantic contract, enforced by the host on the Lambda side**: `pn` functions are callable only from procedural contexts (the safety analyzer, [LR_12](../doc/dev/lambda/LR_12_Procedural_Runtime.md), rejects them in pure code), while `fn` is the module author's *purity promise* — the compiler is free to CSE, reorder, or elide calls to it. Any function that mutates arguments, touches I/O, or holds hidden state **must** declare `pn` (e.g. `open` above does file I/O). There is no default: the prefix is mandatory, so purity is never accidental.
- Other front-ends may not care (JS/Python treat both as ordinary callables), but the information is free for them and load-bearing for Lambda — the "don't care" languages simply ignore the prefix, mirroring how `is_proc` in today's `SysFuncInfo` only gates the Lambda side.
- `-> T^` marks `can_raise` (existing error-tier syntax) — no separate flag.
- Union types, optionals, containers, `any` — the whole existing type grammar is available; front-ends consume the parsed `Type*` exactly as they consume `sys_func_defs` types today.
- The signature is **human-readable in the manifest** (§7.2 mirrors it), so `lambda module info spellcheck` can print a real API listing, and a future registry can index/search interfaces — Postgres's `CREATE FUNCTION` benefit for free.
- Parsing cost is once per module load; the parsed form is cached in the registry.

**One spec level** is preserved: the module interface language *is* the Lambda type language. No IDL, no second grammar.

### 6.3 Native C types: the VMap projection rule

**A raw C pointer never appears in a script-visible signature.** It appears in exactly two places: behind a **VMap projection** of the native struct, or at the C calling-convention layer where the runtime already knows the payload. (P7; direction approved.)

**VMap is the vehicle — it already exists for exactly this.** `LMD_TYPE_VMAP` (`lambda.hpp:426–442`) is a `Container` holding an opaque `void* data` plus a `VMapVtable*` (get/set/count/keys/key_at/value_at/destroy) — designed precisely for values that *appear* map/object-like but don't follow Lambda's native `Map` layout. A native C struct crossing the module boundary becomes a VMap whose vtable projects its fields as map entries:

- `data` → the native pointer (hunspell handle, `DomElement*`, image struct, …).
- `vtable->get/set` → read/write struct fields by key; `keys/count` → introspection/iteration.
- Scripts see a normal map-like value: `dict.encoding`, `img.width`, `len(v)`, `for k in keys(v)`, printing/formatting — **all existing map machinery works for free**, in every front-end (`type(v)` returns `"map"`; JS property access dispatches on the VMAP container type).
- Writes route through `vtable->set`, which the vtable header already scopes to **pn contexts** — the fn/pn contract (§6.2) composes cleanly: pure `fn` code can read native fields, mutation requires a procedure.
- **The finalizer already exists**: GC sweep calls `vm->vtable->destroy(vm->data)` today (`lambda-mem.cpp:721`) — the module frees its native resource there. No new GC feature is required (the `JubeHandle` alternative considered earlier would have needed one).
- **Branding = vtable identity.** The host materializes exactly one vtable per registered native type, so the boundary type check is one pointer compare: `vm->vtable == type->vtable`. A `hunspell` VMap cannot be passed where a `cairo_surface` is expected.
- An **opaque handle is just the degenerate case**: a vtable exposing zero keys. No separate handle concept needed — full-field projection and fully-opaque tokens are two ends of one mechanism.

**Declaring native types: the `JubeTypeDef` capability.** Modules register named native types; signatures reference them by name (`dict: hunspell`, cross-module as `spellcheck.hunspell`). Two authoring levels:

```c
typedef struct JubeFieldDef {          // level 1: declarative struct reflection
    const char* name;                  // script-visible field name
    const char* type;                  // Lambda type atom: "i32", "f64", "string", "bool", ...
    uint32_t    offset;                // offsetof() into the native struct
    uint32_t    flags;                 // JUBE_FIELD_READONLY | ...
} JubeFieldDef;

typedef struct JubeTypeDef {
    const char* name;                  // nominal type name: "hunspell", "dom_node", "image"
    const JubeFieldDef* fields;        // level 1: host SYNTHESIZES the vtable from this table
    int32_t field_count;               //   — plain C structs map with zero glue code
    const JubeVMapOps* vtable;         // level 2: hand-written ops for computed/lazy properties,
                                       //   native collections, views (NULL ⇒ synthesized)
    void (*destroy)(void* ptr);        // native destructor, wired into vtable->destroy
} JubeTypeDef;
```

For a plain C struct, level 1 makes the mapping declarative:

```c
static const JubeFieldDef image_fields[] = {
    {"width",  "i32", offsetof(NativeImage, width),  JUBE_FIELD_READONLY},
    {"height", "i32", offsetof(NativeImage, height), JUBE_FIELD_READONLY},
    {"dpi",    "f32", offsetof(NativeImage, dpi),    0},
};
static const JubeTypeDef raster_types[] = {
    {"image", image_fields, 3, NULL, image_destroy},
    {"hunspell", NULL, 0, NULL, hunspell_destroy},   // zero fields ⇒ opaque
};
```

(`JubeVMapOps` in `jube.h` is the C mirror of the internal `VMapVtable` — same function-pointer layout, kept in lockstep; the ops signatures traffic only in `Item` and `void*`, so the mirror is stable.)

Host API constructors/accessors: `jube_native_new(type, ptr)` wraps a native pointer as a branded VMap; `jube_native_get(item, type)` unwraps after the vtable-identity check (NULL + error on mismatch).

Two contracts the SDK states explicitly:

- **Vtable ops must be GC-safe**: the collector traverses VMap entries during marking (`gc_heap.c:1179`), so `get/keys/key_at/value_at` must not allocate; `destroy` must not allocate or call back into script (the classic finalizer trap, §10).
- **Items inside native structs must be marked**: if a native struct holds `Item` references, the type must expose them through the vtable (so marking sees them) or root them explicitly — a bare `Item` field invisible to `keys/value_at` is a collection bug.

**C-level types where they belong.** For the optional typed fast path (§6.5), the impl signature uses atoms the language already owns — `i8…u64`, `f16/f32/f64` exist today as literal suffixes (`grammar.js:34–37`) and as `NUM_SIZED` runtime types. Pointer-shaped values reduce to payloads the runtime knows:

| script type | C level | access |
|---|---|---|
| `string` | `String*` (never `char*`) | `jube_string_cstr()` for NUL-terminated view |
| `binary` / `ArrayNum` | `(ptr, len)` pair via host API | **invalid across any allocation** — data-zone relocation contract, stated explicitly in the SDK docs |
| named native type (`hunspell`) | its `void*` after vtable-identity check | `jube_native_get()` |
| `fn(...)` values | opaque `Item` | invoked via `jube_call()` host function (callbacks into script) |

No `*` token ever enters the type grammar; C's aliasing/ownership ambiguity stays out of the type system.

### 6.4 The host API (`JubeHostAPI`)

**Single tier, strict, for everyone** (P11 — committed decision): in-tree modules use the same `JubeHostAPI` external DLLs do. No internal-header side door for module-shaped code, or the stable API rots unused (the CPython lesson).

```c
typedef struct JubeHostAPI {
    uint32_t api_version;      // additive-only evolution (P10): functions appended, never removed/re-typed

    // -- values: C projection of the Mark API --
    Item (*item_int)(int64_t v);           Item (*item_float)(double v);
    Item (*item_string)(const char* s, size_t len);
    Item (*string_cstr_len)(Item s, const char** out, size_t* out_len);
    Item (*array_new)(int64_t cap);        void (*array_push)(Item arr, Item v);
    Item (*map_new)(void);                 void (*map_set)(Item m, Item key, Item v);
    Item (*map_get)(Item m, Item key);
    // element/MarkBuilder surface for format modules...
    TypeId (*type_of)(Item v);

    // -- native types as VMap projections (§6.3) --
    Item  (*native_new)(const JubeTypeDef* type, void* ptr);   // wrap native ptr as branded VMap
    void* (*native_get)(Item v, const JubeTypeDef* type);      // unwrap; NULL on vtable-identity mismatch

    // -- GC contract (P8) --
    void (*gc_root)(Item* slot);           // register a module-held writable root
    void (*gc_unroot)(Item* slot);
    void (*gc_root_range)(uint64_t* slots, size_t count);

    // -- errors (P9) --
    Item (*error_new)(const char* code, const char* msg);   // returns ItemError-tagged value
    // JS-facing modules: pending-exception raise/check pair (translated at bridge points, never leaked)

    // -- names, logging --
    Item (*intern)(const char* name, size_t len);       // name-pool ⇒ pointer-equality keys
    void (*log)(int level, const char* fmt, ...);       // routes to lib/log.h

    // -- calling back into script --
    Item (*call)(Item fn, int argc, Item* argv);

    // -- for language modules only: compile-context services (MIR module creation,
    //    register_dynamic_import, sub-compilation) via JubeCompileCtx --
} JubeHostAPI;
```

Design rules:

- **Derived by extraction, not speculation**: v1's exact function list = the deduplicated set of runtime symbols `py/`, `rb/`, `bash/`, and the POC modules actually use. Anything in-tree module code needs that isn't in the struct is a bug in the struct.
- **Hot inlines live in `jube.h`**: Item packing/unpacking macros (`i2it`, `get_type_id`, …) are already the frozen interop contract (`Lambda_and_JS_Runtime.md` §4), so they may appear as inlines in the public header without freezing anything new. The struct carries everything whose implementation can churn.
- Struct-of-pointers indirection cost ≈ a PLT call through any dylib — not a real overhead concern; the fast path for hot leaf functions is §6.5, not API bypass.

**Contracts stated in the SDK (mostly restating existing invariants):**

- *GC*: object-zone pointers are stable (non-moving); **data-zone views die at the next allocation**; module-held `Item`s that must survive a collection go through `gc_root`/`gc_root_range` (the CJS module registry already does exactly this — `js_mir_entrypoints_require.cpp`, `js_cjs_register_roots`). Native memory owned by modules is freed in the native type's `destroy` (called at VMap sweep), not leaked through malloc'd Items.
- *Errors*: return `error_new(...)` results; never `longjmp`/throw across the boundary; JS pending-exception is set only via the API and only in JS-facing bridges.
- *Threading*: module functions are called on the EvalContext's thread; modules must not call host API functions from their own threads (v1 restriction; revisit with the runtime's own threading story).

### 6.5 Calling conventions

Per-function, declared in the descriptor (P6):

1. **Default — direct boxed**: fixed arity, `Item fn(Item a, Item b, ...)` (what the transpiler emits for sys funcs today). Zero new machinery.
2. **Varargs boxed** (`JUBE_FN_VARARGS`): `Item fn(int argc, Item* argv)` for arity −1 functions.
3. **Typed fast path** (optional): `native_signature` + `native_func` — e.g. `"(f64, f64) -> f64"` — lets inference-proven call sites bypass boxing entirely (exactly today's `native_c_name` mechanism for `sin`/`fabs`, generalized). Only worth declaring for hot leaf functions.

### 6.6 Registration and namespacing

At load, for each `JubeFuncDef`:

1. Parse `signature` → `Type*`; build the AST-level entry the front-end lookup consults. The monolithic `sys_func_map` becomes a **per-module chain**: unqualified names resolve against `core` (today's builtins keep working unqualified); module functions resolve as `modname.func` after an import statement brings the module into scope.
2. `register_dynamic_import("modname.func", func_ptr)` — MIR import naming uses the qualified name; collisions are impossible by construction. (`.` is legal in MIR import names; if it proves troublesome, mangle as `modname$func` — cosmetic.)
3. Method-eligible functions join the method-dispatch table keyed by (name, first-param type), as today.

Front-end syntax mapping (each front-end does this once, thinly):

| front-end | syntax | resolves to |
|---|---|---|
| Lambda | `import spellcheck` … `spellcheck.check(d, w)` | registry `spellcheck` → `spellcheck.check` |
| JS/Node | `require('jube:spellcheck')` / `import ... from 'jube:spellcheck'` | same registry; returns namespace object |
| Python | `import spellcheck` | same |
| Ruby | `require 'spellcheck'` | same |

The `jube:` prefix on the JS side keeps the npm-package namespace unambiguous; Lambda/py/rb resolve bare names against (script modules first, then jube registry) — Lua's unified-`require` model.

---

## 7. Discovery, manifest, distribution, caching

### 7.1 Search paths

Precedence order:

1. `./lambda/` — project-local
2. `$LAMBDA_MODULE_PATH` — colon-separated
3. `~/.lambda/modules/<name>/<version>/` — user cache (the future registry's download target)

### 7.2 Manifest (`module.json`)

Readable **without loading the library** (P12) — discovery, listing, and version resolution never execute foreign code:

```json
{
    "name": "spellcheck",
    "version": "1.2.0",
    "kind": "native",
    "abi_version": 1,
    "description": "Hunspell-backed spell checking",
    "library": { "darwin-arm64": "spellcheck.dylib",
                 "linux-x86_64": "spellcheck.so",
                 "windows-x86_64": "spellcheck.dll" },
    "capabilities": ["types", "functions"],
    "types": ["hunspell"],
    "interface": {
        "open":    "pn (path: string) -> hunspell^",
        "check":   "fn (dict: hunspell, word: string) -> bool",
        "suggest": "fn (dict: hunspell, word: string) -> [string]"
    },
    "checksum": { "spellcheck.dylib": "sha256:..." },
    "dependencies": {}
}
```

- `kind`: `"native"` (Jube C module) or `"source"` (script package — future; reserved so one manifest schema serves both).
- `interface` mirrors the descriptor signatures. The **C descriptor table remains ground truth**; at load the runtime verifies manifest ⊆ descriptors and warns on drift. The manifest copy exists for no-load tooling and future registry indexing.
- The manifest schema is itself validated with Lambda's own schema validator — dogfooding `doc/Lambda_Validator_Guide.md`.
- `checksum`/`dependencies` are registry-readiness slots; local loading ignores absent fields.
- Loading is **lazy**: manifests are scanned eagerly (cheap), `dlopen` happens on first import.

### 7.3 Distribution policy (decisions recorded)

1. **Script packages distribute as source text.** Auditability, diffability, and — decisive for a gradually-typed suite — whole-program inference stays free to specialize per import context. Distribution of compiled artifacts would freeze lowering decisions (boxing, dispatch, future call-site-aware inference) at package-build time.
2. **MIR is never a distribution format.** No `.bmir` in manifests, no cross-machine MIR, no MIR-level spec or compatibility promise. Lambda maintains exactly one spec level: the source language. (The JVM/WASM compatibility treadmill — version-skew matrices, IR validation as a security surface — is cost without benefit for Lambda's use case.)
3. **Native Jube modules distribute as platform binaries** (or are built locally from a source-form module) — they're C code; that's inherent.

### 7.4 Local compiled-code cache (script modules; design direction)

Distribution format ≠ cache format (P14). Source is the truth; **everything compiled is disposable**:

```
source ──(parse → AST → lower → MIR codegen, first import)──▶ ~/.lambda/cache/<key>/
key = hash( runtime build ID
          + module source
          + interface/inference summary of every imported module
          + compile flags )
```

- The cache never leaves the machine and is keyed by **runtime build ID** ⇒ it is a memoization, not a format: any change to MIR, lowering, or codegen silently invalidates every entry. Zero spec burden.
- The key covers everything lowering depended on. If cross-module inference consumes only dependency *interfaces*, hashing interface summaries suffices and hits stay frequent; future call-site-specialized compilations either join the key or are marked uncacheable. Graceful degradation is always "compile from source," which is always correct.
- **Backend choice (deferred until the module system exists; invisible to all format decisions):**
  - *(a) mir2c → system C compiler → dylib*: works with today's pieces; needs a client toolchain; slow first compile. Viable stopgap.
  - *(b) code-image cache*: serialize JIT'd machine code + a relocation journal recorded during codegen; load = map + patch import fixups + W^X flip (the dance the JIT already does). No client toolchain, no linker formats, microsecond loads. Requires a contained MIR-gen patch to record relocations. **Recommended end-state.** Rationale: per `JS_01`/`JS_15`, link-time codegen dominates large-script cost — a cache that stops at MIR (skipping only parse/lower) leaves the dominant cost on the table.

---

## 8. POC plan

Two POCs, chosen to exercise different halves of the design. Both begin as **statically linked** Jube modules (P13 — proving descriptors, registry, host API) and then flip to dynamic loading (proving loader, manifest, ABI check).

### POC 1: `radiant-dom` — modular access to the Radiant engine

**What**: carve the DOM/CSSOM bridge (`js_dom.cpp`, `js_dom_events.cpp`, `js_dom_selection.cpp`, `js_cssom.cpp`, ~18k lines) into a Jube module exposing Radiant to **both** JS and Lambda.

**Why it's the right first target**:
- It has a real, large, existing consumer (the entire JS DOM test surface + Radiant UI-automation baseline) — regressions are instantly visible.
- It forces the host API to be honest about the hardest value-model cases: DOM nodes are Radiant-owned native structures surfaced as script values (today via `MapKind`-routed exotic Maps / `js_dom_wrap_element`). The POC is the **VMap-projection design's trial by fire**: `DomElement*` wrapped as a `dom_node` native type with a hand-written vtable (level 2 — properties are computed, not offset-mapped) is precisely the §6.3 mechanism at its hardest.
**Design rationale — why JS unifies on the module path (decided).** The JS engine's `MapKind` DOM special case is removed; JS DOM access routes through the module's VMap like every other language, so all languages access DOM in one standard manner. Two arguments carry this decision:

1. **The performance objection is weaker than it looks.** The MapKind fast path matters for *plain JS objects* — shaped maps with per-site inline caches doing O(1) typed slot loads. DOM nodes were never on that path: the DOM MapKind exists precisely to route DOM access **off** the IC path into C runtime calls (`js_dom_get_property` → per-property dispatch). Today's DOM access is already "kind check + C call per property"; a VMap access is "vtable-identity check + indirect call" — **the same dispatch structure, cost-parity by construction**. Unifying does not sacrifice a fast path; it replaces one C-dispatch route with another. Hot-property tricks the current code carries (interned-pointer key compares for `style`/`className`/`id`, the DOM wrapper cache at `js_dom.cpp:1508`) migrate *inside* the module's vtable, where every language benefits from them instead of only JS. And DOM-heavy workloads are dominated by layout/render cost, not property dispatch (document-mode JS already runs on the MIR interpreter for this reason). The existing baselines are still the proof: perf parity is asserted by measurement, not by this argument alone.

2. **It draws a principled architectural line that was previously missing.** MapKind and VMap stop being competing mechanisms and become two answers to two different questions:
   - **MapKind = ECMAScript-spec exotic behavior — engine-owned.** TypedArray, Proxy, iterator sentinels: these are *language semantics*, defined by the ECMAScript spec, and belong in the JS engine.
   - **VMap = host/native objects — module-owned.** DOM today; every native type any future Jube module registers, tomorrow.

   DOM was always a host object wearing an engine-internal costume. Removing the DOM MapKind means the JS engine no longer contains a hardcoded *host-object* special case — exactly the class of coupling this design exists to eliminate — and every future module gets DOM-class integration without touching the engine, because generic VMAP dispatch becomes the single host-object protocol. (It also frees one of the sixteen 4-bit MapKind slots.)

   What remains engine-side is only a thin **generic** JS semantic adapter — answering JS-*language* questions a language-neutral vtable cannot (`instanceof`, prototype identity, descriptor reflection) — driven by the module's type table and written once for *all* modules' native types, never per-module.

**Deferred: deeper JS semantics over VMap** — documented now, to be specified in full before migration step 3 below. These are the known items the semantic adapter and vtable protocol must eventually define; none blocks starting the migration, all must be answered before the MapKind path is deleted:

1. **Wrapper identity**: `el.parentNode === el.parentNode` must hold ⇒ exactly one VMap wrapper per `DomElement*`. Already solved once in the current code (thread-local wrapper cache, `js_dom.cpp:1508`) — the cache moves into the module/host, it is not new invention.
2. **`instanceof` / prototype identity**: `el instanceof HTMLElement` needs the module type table to map native types onto a registered prototype chain (`dom_node` → `Node.prototype` → `EventTarget.prototype`). The adapter resolves instanceof/`__proto__` queries from that registration.
3. **Expando properties and prototype patching**: scripts set `el._myFlag = 1` and patch `Element.prototype.foo` — the lookup protocol must define where vtable projection ends and the JS-side expando/prototype store begins (likely: vtable first, miss falls through to a per-wrapper expando map + prototype chain).
4. **Property-descriptor reflection**: `Object.getOwnPropertyDescriptor`/`defineProperty` on host objects — what the adapter synthesizes for vtable-projected properties.
5. **Live collections**: `el.children`/`childNodes` are live in the DOM spec. Computed vtable `get` models laziness naturally, but liveness + iteration protocol (`for..of`, index access, `length`) needs explicit definition.
6. **Vtable op gaps**: JS needs `has` (`'x' in el`) and `delete` semantics, and a guaranteed enumeration order — likely small additive extensions to the ops struct.
7. **Ownership**: DOM nodes are owned by the Radiant document, not the wrapper ⇒ `destroy` for `dom_node` is a non-owning no-op. `JubeTypeDef` needs an owning/non-owning distinction so `destroy` frees hunspell handles but never frees DOM nodes.
8. **Event handlers**: DOM holds script closures (Items) ⇒ rooted via the host GC API — the existing pattern (cf. the CJS registry's root ranges), restated as a module obligation.

**Migration readiness — nothing pressing blocks the start.** Audit result: the prerequisites are ordinary engineering, not research problems. (a) A minimal Phase-1 SDK slice (`JubeTypeDef` + registry + the host-API subset DOM needs) suffices — the POC co-evolves it; the dlopen loader is *not* required (the module starts statically linked, P13). (b) The JS engine already treats VMAP as a map-like peer in many paths (`js_assert`, `js_dom_events`, `js_event_loop` all check `MAP|OBJECT|VMAP` together) — what's needed is an audit of the core property get/set and IC paths for complete VMAP dispatch, not greenfield support. (c) The vtable extensions (item 6) are small and additive. (d) The wrapper cache ports, it isn't invented. The dense existing test umbrella (below) makes each step independently verifiable.

**Staged migration** (gates: editor 1931 JS tests, UI-automation 5714 baseline, jQuery/popper suites):

1. The module owns the property/method resolution tables (descriptors + vtable).
2. Existing MapKind dispatch functions **delegate** to them — behavior identical, implementation relocated. *Hold this step as a long-lived checkpoint*: it is the state where old-path and new-path behavior can be diffed per property under the full baseline before any representation changes — where subtle DOM regressions (live collections, event-handler properties) surface cheaply.
3. DOM wrapper creation switches MapKind-Map → VMap, baselines gating; the deferred-semantics list above must be specified by this point.
4. Delete the MapKind DOM kind and the `js_dom` dispatch layer.
- It delivers goal 2 visibly: **Lambda scripts gain DOM access** (`import dom` → query/mutate the same `DomElement` tree `layout`/`render` use), which today is JS-only.
- Its ~40 `jit_runtime_imports[]` entries and `edit_bridge.cpp`'s linkage-by-convention move into the module's own descriptor tables — a measurable bite out of the monolith.

**Shape**: capabilities = `types` (`dom_node` et al. as VMap projections over `DomElement*`) + `functions` (Lambda-facing DOM API, signatures in Lambda type syntax over `element`/`dom_node`) + `namespaces` (JS-facing `document`/DOM globals installation). Radiant itself stays in-process; the module is the *interface*, mediating through the host API.

**Exit criteria**: JS DOM tests + `make test-radiant-baseline` green with the bridge fully registry-driven (no `js_dom_*` entries left in `sys_func_registry.c`); the `MapKind` DOM kind deleted from the JS engine (JS routes through the module's VMap); a Lambda sample script drives a DOM mutation → layout query round-trip; module loads dynamically in a dev build.

### POC 2: `node-*` — Node compat as modules

**What**: refactor Node builtin modules (`fs`, `path`, `http`, `buffer`, `events`, `child_process`, `crypto`, …) from monolithic dispatch into per-module Jube modules using the `JubeNamespaceDef` capability.

**Why**:
- It kills the worst maintainability offender: the `memcmp` chains in `js_module_get_builtin()` (`js_runtime.cpp:37808`) and the builtin-name lists duplicated across ≥3 sites become registry lookups against module-declared specifier tables (`{"fs", "fs.js", "node:fs"}`).
- It's naturally incremental: one Node module at a time, with `make node-baseline` (1492/3517 currently) as a continuous regression gate — the refactor must hold that number.
- It proves the **namespace capability** and lazy namespace construction, which POC 1 only touches lightly.
- It opens real modularity wins: a minimal `lambda.exe` without OpenSSL-bound `crypto` (8.7k lines) or `http` (6k lines); third parties can add Node-API-shaped modules without touching the tree.

**Suggested order**: `path` (pure, small — pipe-cleaner) → `events`/`buffer` → `fs` (libuv surface) → `http`/`crypto` (largest, external deps — and `crypto`'s existing OpenSSL `dlopen` folds into module-local lazy binding naturally).

**Exit criteria**: `js_module_get_builtin`'s chain deleted; per-module specifier registration; node-baseline unchanged; at least `path` + `fs` loadable as dynamic modules; `crypto` optional at runtime (absent module ⇒ clean "module not found" error, not link failure).

### What the POCs deliberately defer

- Language modules (`JubeLanguageDef` for py/rb/bash carve-out) — the struct is designed now, exercised later; Python remains statically linked through the POC phase.
- The online registry client, dependency resolution, signature verification.
- The script-compilation native cache (§7.4) — independent workstream.

---

## 9. Migration phases

| Phase | Deliverable | Monolith impact |
|---|---|---|
| **1. SDK + registry core** | `jube.h` (ABI structs, host API v1 extracted from real usage), signature parser reuse, per-module registry chains behind `get_sys_func_info()`/`import_resolver()`, `jube_register_static()` | none yet (parallel plumbing) |
| **2. In-place split** | Convert `sys_func_registry.c` into static `JubeModuleDef`s (`core`, `js`, `py`, `rb`, `bash`) living in their own directories, each registering its own imports | the 2,902-line file and its cross-language `#include`s dissolve; `LAMBDA_STATIC`/`FPTR` hack loses its reason to exist |
| **3. Dynamic loader** | manifest scan, `dlopen`/`LoadLibrary`, ABI check, lazy load; `lambda module list/info` CLI | first out-of-tree module possible |
| **4. POC 1** | `radiant-dom` (static → dynamic) | DOM bridge leaves the central registry |
| **5. POC 2** | `node-*` modules, one at a time | memcmp chains deleted; optional `crypto`/`http` |
| **6. Later** | language carve-outs (Python first), registry client, native compile cache | core binary shrinks; ecosystem opens |

Each phase ships independently and keeps `make test-lambda-baseline` / `make test-radiant-baseline` / `make node-baseline` green throughout.

---

## 10. Open questions & risks

1. **JS semantic adapter for module native types** (POC 1 designs it): the representation question is **decided** — JS refactors onto the VMap path too; all languages access DOM through the module uniformly. The full rationale and the eight documented-but-deferred semantic items (wrapper identity, instanceof/prototypes, expandos, descriptor reflection, live collections, vtable op gaps, ownership, handler rooting) live in §8 POC 1; they must be specified before migration step 3, and none blocks starting.
2. **Host API completeness for format modules**: the Mark-builder C projection needs to be rich enough to write a real parser against — extract from an existing `input-*.cpp` conversion, same empirical rule as the rest of the API.
3. **Unloading**: v1 loads for process lifetime (`shutdown()` is best-effort at teardown only). True hot-unload interacts with JIT'd code holding function pointers — out of scope until needed (hot-*reload* of script modules is a separate, existing workstream).
4. **Windows**: `LoadLibrary`/`GetProcAddress` parity is straightforward *because* of P2 (modules never import host symbols); needs CI coverage from Phase 3.
5. **Signature-grammar gaps for native types**: accepted risk — direction approved; iron out atoms (e.g. buffer views, callback types) as POC modules hit them, extending the type grammar only when a real module needs it.
6. **Finalizer semantics**: `destroy` runs at sweep and must not allocate or call back into script — document and assert; this is the classic finalizer trap in every surveyed system. Likewise the marking-path vtable ops (`keys`/`key_at`/`value_at`) must be allocation-free (§6.3); synthesized level-1 vtables satisfy this by construction, hand-written level-2 vtables need review.

---

## Appendix A: decision log (from design discussion, 2026-07)

| Decision | Choice |
|---|---|
| Signature encoding | Lambda type syntax strings (over C enum descriptors) — approved |
| Purity in signatures | mandatory `fn`/`pn` prefix on every signature (purity promise / procedural marker); enforced on the Lambda side, ignored-but-carried by other front-ends |
| C native types in signatures | raw pointers never script-visible; details iterated during POC — approved as direction |
| Native struct/pointer mapping | **VMap projection** (existing `LMD_TYPE_VMAP` vtable container) — supersedes the earlier opaque-`handle<brand>` sketch; brand = vtable identity, finalizer = existing sweep-time `destroy`, opaque handle = zero-field degenerate case; named types via `JubeTypeDef` (declarative field table or hand-written ops) |
| JS DOM representation | **JS unifies onto the module's VMap path** — MapKind DOM special case removed from the engine; all languages access DOM identically. Principle: MapKind = ECMAScript-spec exotics (engine-owned), VMap = host/native objects (module-owned). Only a thin generic JS semantic adapter (instanceof/prototypes/reflection) stays engine-side |
| Host API strictness | single strict tier for all modules including in-tree — **must have** |
| Script package distribution | source text only |
| MIR as distribution format | **rejected** — MIR stays private in-memory IR, one spec level (source language) |
| Compiled-script caching | local derived cache keyed by (runtime build ID + sources + dep interfaces + flags); backend (mir2c vs. code-image) deferred; lean = native cache on client machine |
| POC targets | `radiant-dom` + `node-*` modules (instead of Python carve-out) |
