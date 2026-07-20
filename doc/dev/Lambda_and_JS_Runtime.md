# Lambda & LambdaJS Runtime — Combined Design Summary

> **One runtime, two languages.** Lambda Script (pure-functional, `.ls`) and LambdaJS (the embedded JavaScript engine, `.js`) are not two VMs with a bridge — they are two front-ends over a **single shared substrate**: the same tagged `Item` value, the same GC heap and pools, the same MIR JIT, the same input parsers/formatters, and the same Radiant DOM. They interop at runtime with **zero marshalling**: a value produced by one is directly readable by the other. This document is the concise map of that design — for developers, and for loading into AI-model context. Details live in the two detailed-design sets: **[LR_00 (Lambda core)](lambda/LR_00_Overview.md)** and **[JS_00 (LambdaJS)](js/JS_00_Overview.md)**.

---

## 1. The shared substrate

### 1.1 `Item` — the universal 64-bit tagged value ([LR_03](lambda/LR_03_Value_and_Type_Model.md), [JS_03](js/JS_03_Value_Model.md))

Every runtime value in both languages is one 64-bit word (`lambda.h`). The **high byte `[63:56]` is the `TypeId`**; the low 56 bits are an inline value or pointer. Three storage classes:

- **Packed scalars** — no heap object: `null`, `bool`, safe-band `int` (±(2⁵³−1) inside the 56-bit payload), compact `NUM_SIZED` values, and canonical self-tagged `FLOAT` values.
- **Tagged-reference scalars** — high byte = tag, low 56 bits = a payload address. `INT64`/`UINT64` and rare out-of-band `FLOAT` point to activation/caller homes or destination-owned words, with a counted GC fallback only for ownerless persistence. `DECIMAL`, `STRING`, `SYMBOL`, `DTIME`, and `BINARY` are GC/pool-owned; every `DTIME` is GC-owned.
- **Containers** — the word *is* the pointer (high byte 0); the `TypeId` is the first field of the pointee. All extend `struct Container`: `RANGE`, `ARRAY`, `ARRAY_NUM` (raw numeric vectors), `MAP`, `VMAP`, `ELEMENT`, `OBJECT`, `FUNC`, `TYPE`.

`get_type_id(Item)` dispatches all variants uniformly. Packing macros: `i2it`/`d2it`/`s2it`/`l2it`/… JS-specific encodings ride on the same scheme: `undefined` = distinct `LMD_TYPE_UNDEFINED` tag; TDZ = undefined-tag|1; JS Symbols = negative int56 beyond `-JS_SYMBOL_BASE` (2^40); BigInt = `DECIMAL` with a marker; array-hole and iterator-done sentinels use unused tags `0x7E`/`0x7F`.

**Maps/objects share one shape system**: a `Map` points to a `TypeMap` (shape) whose `ShapeEntry` chain + `slot_entries[]` array + inline FNV-1a hash lay out a packed `data` buffer. `Element` is simultaneously a list of children and a map of attributes — the document node type every input parser produces. ([JS_06](js/JS_06_Objects_Properties_Prototypes.md), [LR_03](lambda/LR_03_Value_and_Type_Model.md))

### 1.2 Memory & GC ([LR_08](lambda/LR_08_Memory_and_GC.md), [JS_03 §6](js/JS_03_Value_Model.md))

One `EvalContext` owns all regions; both languages allocate from them:

- **GC heap** (`lib/gc/`) — dual-zone, **non-moving mark-and-sweep**. The *object zone* (size-class free lists) holds object structs, which **never move** — tagged pointers held by JIT code, envs, or either language stay valid across collections. The *data zone* (bump) holds variable-size buffers (`Map.data`, array items); it **is relocated** (copied to a tenured zone) at collection with owner-slot fixup. Generated heap-capable locals are published to the precise root side-stack before allocation-capable calls ([LR_07](lambda/LR_07_MIR_Transpiler_JIT.md)). Collection triggers on a data-zone threshold; roots include registered slots/ranges, the live root-side-stack slice, and the conservative C-stack backstop.
- **Execution side stacks** — separate stable regions for precisely rooted `Item` slots and raw out-of-band `FLOAT`/`INT64`/`UINT64` payloads. Generated Lambda and JS functions save/restore both watermarks through one epilogue; caller-donated homes and owned tails preserve escaping wide values. `DTIME` is not a number-stack class.
- **Mempool** — module-lifetime data such as cached compiled-function wrappers, module-var arrays, and `TypeMap`s. Escaping JS closures and closure environments are GC-owned and traced instead of permanently pinned.
- **Name pool** — `heap_create_name` interns property keys/identifiers: same name ⇒ same `String*` ⇒ **pointer-equality key comparison** everywhere (both languages, all parsers).

### 1.3 The MIR JIT ([LR_07](lambda/LR_07_MIR_Transpiler_JIT.md), [JS_01](js/JS_01_Compilation_Pipeline.md))

Both front-ends lower their typed ASTs **directly to MIR IR** (vnmakarov/mir) and share `lambda/mir.c` (`jit_init`, link, codegen; default opt level 2). Runtime helpers are C functions referenced by name and resolved at link via the shared registry (`lambda/sys_func_registry.c` — one table feeds both engines' imports). Codegen strategy is identical in spirit for both: **boxed `Item` by default, native `MIR_T_I64`/`MIR_T_D` registers when inference proves numeric**, boxing only at generic boundaries. Execution is eager native codegen by default; LambdaJS additionally selects the **MIR interpreter** for large/cold/document scripts, since link-time codegen dominates their cost ([JS_01 §4](js/JS_01_Compilation_Pipeline.md), [JS_15 §3](js/JS_15_Performance.md)).

---

## 2. Lambda core runtime (`lambda/`) — [LR set](lambda/LR_00_Overview.md)

- **Pipeline:** Tree-sitter grammar (`tree-sitter-lambda/grammar.js`, regenerated via `make generate-grammar`) → typed AST with build-time inference (`build_ast.cpp`, [LR_02](lambda/LR_02_Parsing_AST.md)) → **MIR Direct transpiler** (`transpile-mir.cpp`, the default, [LR_07](lambda/LR_07_MIR_Transpiler_JIT.md)) or the legacy **C2MIR** text backend (`transpile.cpp`, [LR_06](lambda/LR_06_C_Transpiler.md)) → link + `MIR_gen` → run `main_func(context)` under a stack-overflow guard. There is **no tree-walking interpreter**; `lambda-eval.cpp` is the C-ABI runtime support library ([LR_09](lambda/LR_09_Runtime_Builtins.md)).
- **Language split:** pure `fn` (functional core) vs `pn` procedures (`run` command) with in-place mutation builtins (`push`, `splice`, index-assign), statement loops, and a conservative safety analyzer ([LR_12](lambda/LR_12_Procedural_Runtime.md)).
- **Numerics:** `int`/`integer`/`float`/`decimal` value tower plus Go-style sized lanes; plain-`int` overflow enters float, and full-width sized/non-sized mixing enters `integer` by type. DateTime is GC-owned ([LR_04](lambda/LR_04_Numbers_Decimal_DateTime.md)). `ArrayNum` provides raw numeric vectors with auto-vectorization, broadcasting, and mutable views ([LR_05](lambda/LR_05_Strings_and_Vectors.md)).
- **Errors:** `ItemError` sentinel + rich heap `LambdaError`; `T^E` return types, `raise`, `?` propagation, `GUARD_ERROR` in the runtime ([LR_10](lambda/LR_10_Error_Handling.md)).
- **Data construction boundary:** the **Mark API** (`mark_builder/reader/editor.hpp`) — every input parser (`lambda/input/`: JSON, XML, HTML, CSS, Markdown, PDF, YAML, LaTeX, …) builds Lambda data through it; formatters (`lambda/format/`) serialize it back ([LR_11](lambda/LR_11_Mark_Data_API.md)).
- **Validator:** schema-based validation of runtime data against `type` definitions ([LR_13](lambda/LR_13_Schema_Validator.md)).
- **Execution side frames:** both transpilers reserve static root slots, publish heap-capable locals before allocation-capable calls, restore the root/number watermarks through one epilogue, and rebuild escaping wide scalars in the caller extent ([LR_07](lambda/LR_07_MIR_Transpiler_JIT.md)).

---

## 3. LambdaJS engine (`lambda/js/`) — [JS set](js/JS_00_Overview.md)

- **Pipeline:** Tree-sitter JS → typed `JsAstNode` + six-phase early-error validation ([JS_02](js/JS_02_Parsing_AST.md)) → 14-step lowering (`JsMirTranspiler`) with multi-phase type/class/capture inference → MIR module → JIT or interpreter → `js_main` → drain the libuv event loop ([JS_01](js/JS_01_Compilation_Pipeline.md)).
- **Values:** JS types are a projection onto `TypeId` ([JS_03](js/JS_03_Value_Model.md)). `js_make_number` always emits Lambda's canonical `FLOAT` representation: most doubles self-tag in the Item, while only the out-of-band residue uses a number home. It never retypes a JS Number as Lambda `int`.
- **Objects:** Lambda `Map` + `TypeMap` shapes. A 4-bit **MapKind** in the header routes exotic objects (TypedArray, DOM, Proxy, iterator, …) off the plain-object fast path ([JS_06](js/JS_06_Objects_Properties_Prototypes.md)). Constructor call sites cache a shared shape (`js_constructor_create_object_shaped_cached`) enabling O(1) typed slot access (`js_get_slot_f`, raw doubles inline); named property loads/stores have per-site MONO/POLY inline caches (`JsLoadIC`); prototype-chain method lookup is still uncached per call.
- **Functions & calls:** cached compiled wrappers are pool-allocated, while escaping closures/bound functions and their environments are GC-owned with precise trace callbacks. Call arguments use a single GC-rooted bump **args stack** (`js_args_push/save/restore`); top-level bindings live in indexed per-module root arrays; closures capture through traced env arrays with owned scalar tails ([JS_05](js/JS_05_Functions_Closures.md), [JS_03 §7–8](js/JS_03_Value_Model.md)). Static dispatch (direct `MIR_CALL`) fires for `function` declarations, const-bound functions, and provably-monomorphic class methods; everything else goes through `js_call_function`.
- **Exceptions:** no C++ exceptions/longjmp — a thread-global pending flag (`js_throw_value`/`js_check_exception`) plus emitted check-and-branch after every throwing call; stack overflow alone uses a signal + `sigsetjmp` recovery ([JS_04 §9](js/JS_04_MIR_Lowering.md)).
- **Semantics coverage:** ES2020-era language: classes, generators (state-machine transform, reused for async/await), Promises + microtask queue on libuv, ES modules + CommonJS `require`, Proxy/Reflect, BigInt, TypedArrays/Atomics, RegExp via RE2 with a backtracking fallback ([JS_07](js/JS_07_Classes.md)–[JS_12](js/JS_12_TypedArrays.md)).
- **Host bridges:** DOM/CSSOM over Radiant ([JS_13](js/JS_13_Web_DOM.md)); Node.js compat layer (fs/path/http/Buffer/EventEmitter/npm client, [JS_14](js/JS_14_Node_Compat.md)).

---

## 4. Interop by design — why one substrate

The two runtimes are documented together because the interop is structural, not layered:

1. **No value marshalling, ever.** `JSON.parse` in JS calls Lambda's shared JSON parser and returns ordinary Lambda `Map`/`Array` Items; a document parsed by `input()` in Lambda is the same `Element` tree JS walks via the DOM API; a JS object can be serialized by Lambda's formatters. One allocation, both languages read it.
2. **One GC, one heap, one name pool.** Cross-engine references need no handles or pinning: object structs never move, and interned names give pointer-equality keys to both sides. A collection triggered by JS marks Lambda roots and vice versa.
3. **One JIT.** Both transpilers emit into the same MIR context style, resolve imports from the same `sys_func_registry.c`, and share `mir.c` codegen/link — so improvements (and regressions) in the JIT and GC land on both languages at once.
4. **One document engine.** Radiant's `DomElement` trees are the shared substrate for Lambda's `layout`/`render`/`view` commands and JS's DOM — JS event handlers mutate the same nodes Radiant lays out, with lazy layout on metric queries ([JS_13](js/JS_13_Web_DOM.md), [Radiant design](radiant/RAD_00_Overview.md)).
5. **One polyglot family.** The Jube runtimes (Python/Bash/Ruby, `lambda/py|bash|rb/`) follow the same pattern: Tree-sitter front-end → MIR lowering → shared Item/GC/JIT ([Lambda_Jube_Runtime](../Lambda_Jube_Runtime.md)).

### Invariants both engines must preserve (the interop contract)

- **Item ABI**: high-byte tag layout, the safe-band `int` bound inside its 56-bit payload, `NUM_SIZED` packing, canonical float encoding, the `JS_SYMBOL_BASE` boundary, and the `0x7E`/`0x7F` sentinels are shared constants — changing any requires auditing both engines, the GC, and all parsers/formatters.
- **Containers start with `TypeId`** (the `Container` header), and object structs are **non-moving** — JIT code and pools may hold raw pointers.
- **Data-zone pointers relocate**: any *naked* pointer into the data zone (boxed numerics, hoisted buffer pointers) must be reachable through a precise, writable root across allocations.
- **Names are interned once**: never compare key strings by content when a pooled `String*` identity comparison is available; never mutate a pooled string.
- **Errors don't cross raw**: Lambda-side errors are `ItemError`/`LambdaError` return values (Go-style error tier); JS-side errors are the pending-exception flag. Bridge points (e.g. DOM callbacks, `input()`/`format()` from JS) must translate, not leak — the pending flag is never set while control is in Lambda code, and an `ItemError` never enters JS expression evaluation. The unification protocol (shared zero-copy payload + two choke-point converters) is specified in [`vibe/Lambda_Tuning_Proposal.md`](../../vibe/Lambda_Tuning_Proposal.md) Part 6 §6.6.
- **`log_*` only** (`lib/log.h`) for diagnostics; `./temp/` for scratch files; C+ convention (`doc/dev/C_Plus_Convention.md`), no `std::` containers — `lib/` equivalents (`Str`, `StrBuf`, `ArrayList`, `HashMap`, mempool).

---

## 5. Entry points & CLI

| Command | Path exercised |
|---|---|
| `./lambda.exe script.ls` | Lambda functional pipeline (MIR Direct JIT) |
| `./lambda.exe run script.ls` | Lambda procedural runtime (`pn`/`main()`) |
| `./lambda.exe js file.js` | LambdaJS engine |
| `./lambda.exe layout/render/view page.html` | HTML/CSS → Radiant layout → SVG/PDF/PNG/window (JS in documents runs on the MIR interpreter) |
| `./lambda.exe validate data.json -s schema.ls` | Schema validator |
| `./lambda.exe convert in.json -t yaml` | Input parser → formatter, no script |

| Area | Start here |
|---|---|
| Value model / ABI | `lambda/lambda.h`, `lambda/lambda-data.hpp` |
| Shared memory/GC | `lambda/lambda-mem.cpp`, `lib/gc/` |
| Shared JIT | `lambda/mir.c`, `lambda/sys_func_registry.c` |
| Lambda transpiler & runtime | `lambda/transpile-mir.cpp`, `lambda/lambda-eval.cpp` |
| JS transpiler & runtime | `lambda/js/js_mir_*.cpp`, `lambda/js/js_runtime*.cpp` |
| Data construction | `lambda/mark_builder.hpp` + `lambda/input/`, `lambda/format/` |
| Tests | `make test-lambda-baseline`, `make test-radiant-baseline`, test262 batch runner ([JS_16](js/JS_16_Testing.md)) |

---

## 6. Performance model in one paragraph

Both engines are fast where inference keeps values **native** and slower where values fall back to the **boxed generic path**. Most doubles self-tag; only the out-of-band residue consumes raw number-home slots. Property/method dispatch on non-shaped objects still goes through runtime calls, and allocation-heavy workloads pay whole-heap mark-and-sweep. The optimization catalog and measured history are in [JS_15](js/JS_15_Performance.md); benchmark snapshots live in `test/benchmark/Overall_Result*.md`.
