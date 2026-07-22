# Lambda Static Module Split — Design Proposal

**Status**: IMPLEMENTED WITH USER-DEFERRED CLASS F — rev 27 (2026-07-21; revs 2–26 as recorded below; rev 27 completes the five real validation DSOs, moves the remaining active Path/PDF/resource-provider seams to their owning layers, and ratchets the user-approved Class-F rt→Radiant deferment at 165 imports)
**Scope**: split the monolithic `lambda.exe` build into four static libraries + one executable, with a fixed, enforced inter-module interface.
**Related**: `lib/mem_factory.h` / `vibe/Memory_Context.md` (allocator registry), `vibe/Lambda_Native_Module_Design.md` (Jube host API), `vibe/Lambda_Design_Code_Dedup.md` (DD1–DD5 coherent-header doctrine), `vibe/Lambda_Design_MIR_Cache.md` (MC1–MC8 — the rt-layer MIR cache of §2.1), `utils/check_hosted_python_architecture.py` (boundary-check precedent).

---

## 1. Goal & non-goals

**Goal.** Make the codebase modular with a proper, fixed inter-module interface:

- Four internal **static libraries** — `lib`, `lambda-data`, `lambda-rt`, `radiant` — plus the `lambda.exe` shell.
- Each module has a declared public surface (umbrella headers + `LAMBDA_API` annotations) and a declared set of allowed lower-layer dependencies.
- The layering is **enforced by tooling** (lint rule + linker-level boundary build), not by convention.

**Non-goals.**

- Shipping dynamic libraries. Dynamic linking remains reserved for optional Jube guest/native modules (`lang-python` precedent).
- A stable binary ABI across releases. The module interface is a *source-level* contract (headers); C++ classes like `MarkBuilder` stay C++.
- Windows `dllexport` annotation. Deferred until a real DLL consumer exists (see §7).

---

## 2. Module charters

What each module contains, in general — the scope statement each boundary question resolves against. Six conceptual modules, packaged as four static libraries + the executable (`lambda-core` + `lambda-io` ship together as `liblambda-data.a`, §6):

| Module | Charter | Packaged as |
|---|---|---|
| **lib** | General library data structures and functions: strings (`Str`/`StrBuf`), containers (`ArrayList`, `HashMap`, AVL, LRU), memory (mempool, arena, MemContext/mem_factory), log, URL parsing, raw file descriptors/byte reads, MIME detection, datetime, digest/base64, … No knowledge of Lambda values. These are primitive mechanisms only; they do not choose resource policy, cache resources, or interpret Lambda/JS IO semantics. | `lib.a` |
| **lambda-core** | The static Mark/Lambda document and Item data model: tagged Item values and containers (array/list/map/element), the type system (Type descriptors, `TypeType`), names and shapes (name pool, shape pool/builder), pure pool-free `MarkReader`, arena/pool value constructors, decimal/datetime value representations, and value printing. Its active value headers contain only APIs provided by core or lib. No `Input`, DOM, GC, runtime `Context`, root frames, script IO, or UI-mode behavior. | in `liblambda-data.a` |
| **lambda-io** | Document and shared-resource IO: JSON/XML/HTML/CSS/Markdown/PDF/YAML/LaTeX/… inputs — including the HTML5 tree builder and the CSS parser/cascade engine with its DOM — output formatters, `MarkBuilder`/`MarkEditor`, Target/resource resolution, URL/file byte acquisition, curl-based HTTP fetching, cookie/public-suffix policy, downloader/scheduler/thread-pool, resource/file/network caches (`lambda/network/**`), and the input/doc manager + doc cache (§2.1). Builds/edits arena-owned Mark data over core values; never touches the GC. | in `liblambda-data.a` |
| **lambda-rt** | The active script engines: Lambda parser (tree-sitter), AST builder, MIR-Direct transpiler/JIT, GC and runtime memory, evaluator + system functions, procedural runtime, the LambdaJS runtime, validator. Owns the active runtime C/C++ headers, runtime `Context`, root frames/side stacks, the script manager and script/MIR caches (§2.1), and language-runtime IO bindings whose event-loop/JS semantics are inseparable from rt (for example Node-compatible `fs`, `net`, `dns`, and TLS bindings; §2.2). Shared resource acquisition and caching are consumed from lambda-io. Retired C2MIR remains a frozen legacy enclave: no module-split edits, redesign, or validation work is performed on it. | `liblambda-rt.a` |
| **radiant** | CSS style resolution, layout (block/inline/flex/grid/table/positioned), rendering (paint IR → SVG/PDF/PNG/GL), font & media *loading and rendering* — the files themselves arrive via lambda-io (§2.1) — events & UI (window shell, editing, interaction state). | `radiant.a` |
| **lambda-exe** | The shell: CLI command wiring and the REPL (`main.cpp`, `main-repl.cpp`). Two profiles: full `lambda.exe` and engine-only `lambda-cli` (§13.1). | `lambda.exe` |

### 2.1 SM12 refined: managers and caches per layer

The resource-IO rule is specific about which managers and caches live where — **bytes and files cache below; derived artifacts (compiled scripts, loaded fonts/media) live above**:

| Layer | Owns | Concern |
|---|---|---|
| **lambda-io** | input/doc manager; input/doc file IO + doc cache; **network cache** | Acquiring and caching resources — disk or network, raw bytes and parsed docs. A script *as a file* is just another resource: downloaded and cached by the network cache like any other URL. Same for font and media files. |
| **lambda-rt** | script manager; **script cache**; **MIR cache** (`vibe/Lambda_Design_MIR_Cache.md`) | Compiled artifacts — the script cache is keyed on compiling/transpiling (source identity → AST/C/MIR artifacts) and is *not* concerned with network; the MIR cache stores JIT'd module images. |
| **radiant** | font & media *loading/rendering* | Fonts and media files are downloaded and cached **as files** by io's network cache; radiant loads them (font faces, image decode) and renders them. Radiant never fetches. |

The seam between the tiers is one call: the consumer asks io for "the bytes/file at this path/URL" (served from io's caches, or fetched). Everything after is the consumer's — parse/transpile/JIT and artifact caching for rt; face loading, media decoding, and painting for radiant — and no consumer re-implements shared resource fetching.

### 2.2 SM12 refined: resource IO is not every OS syscall

The earlier phrase "all file/network IO lives in lambda-io" was too broad for the live LambdaJS runtime. `lambda/js/js_fs.cpp`, `js_net.cpp`, `js_dns.cpp`, `js_fetch.cpp`, and `js_tls.cpp` contain Node-compatible APIs whose callbacks, handles, promises, and libuv event-loop state are rt-owned. Moving those translation units into io would invert the value/runtime dependency rather than improve the architecture.

The enforceable split is:

| Concern | Owner | Rule |
|---|---|---|
| Raw OS mechanisms | **lib** | Context-free byte/file/socket/URL primitives only; no Lambda values, caching, or policy. |
| Shared resource/document acquisition | **lambda-io** | Target resolution, file/URL byte loading, curl transport, cookies, download scheduling, and resource/document caches. This is the only reusable fetch/cache implementation. |
| Language-runtime IO semantics | **lambda-rt** | Lambda procedural IO and Node-compatible `fs`/`net`/`dns`/TLS/stdio bindings when their behavior is coupled to Items, promises, JS handles, or the rt event loop. Reuse lambda-io for shared resource acquisition; direct low-level IO requires an explicit runtime-native classification. |
| Font/media file acquisition | **lambda-io** | Bytes/files and their cache. |
| Font/media decode, loaded objects, rendering | **radiant** | No independent resource fetch/cache implementation. |

Tier 1 therefore enforces both dependency direction and capability ownership: direct curl/resource-cache use is confined to io; rt/radiant may not bypass io for shared fetching; runtime-native exceptions are an explicit, reviewed list rather than a loophole in the ordinary include DAG.

---

## 3. Decision summary

| ID | Decision | Status |
|----|----------|--------|
| SM1 | Static libraries, not dynamic (§5) | **DECIDED** |
| SM2 | Module set: `lib`, `lambda-data` (core + io), `lambda-rt`, `radiant`, `lambda.exe` (§6) | **DECIDED** |
| SM3 | Layering DAG `lib → lambda-data → lambda-rt → radiant → lambda.exe`; upward references only via registered hooks (§6) | PROPOSED |
| SM4 | `LAMBDA_API` export macros on each module's public surface now; compile with `-fvisibility=hidden` (§7) | **DECIDED** |
| SM5 | Two-tier boundary enforcement: include/capability rules inside `make lint`; **five** shared-link validation DSOs (`lib`, core, io, rt, radiant) as `make check-module-boundary`. `lib`, core, io, and radiant have no project undefined symbols; rt's user-approved Class-F deferment is an exact ratcheted import baseline (§8) | **IMPLEMENTED WITH CLASS-F DEFERMENT** |
| SM6 | Pure data-model/reader files move to `lambda/core/`; mixed data/runtime TUs and active headers split first. `MarkBuilder`/`MarkEditor` move io; GC allocation/rooting stays rt and is not hidden behind a permissive core heap hook (§9) | **DECIDED** |
| SM7 | Validator moves into `lambda-rt` (§10) | **DECIDED** |
| SM8 | CSS property tables relocate so `lambda/input/css` is self-contained (§11) | **DECIDED** |
| SM9 | All runtime→radiant reverse edges normalized to the `render_map.h` registration pattern; `headless_stubs.cpp` deleted (§13) | **DECIDED** |
| SM10 | radiant ↔ lambda-rt relationship: **Option A (layered)** adopted; B (shielded) and C (Jube-mediated) documented for future reference (§12) | **DECIDED** |
| SM11 | Tests link the module libs; black-box vs white-box classification, case-by-case (§14) | **DECIDED** (execution case-by-case) |
| SM12 | Shared resource/document acquisition and caching live in lambda-io; raw context-free mechanisms stay lib; event-loop/Item-coupled language IO stays rt under an explicit runtime-native classification. `lambda/network/**` reclassifies by concern (§2.1–§2.2, §6, §8.2) | **DECIDED** |
| SM13 | **Provider ownership**: every function declared by a module's public header is defined by that module or a lower module. Generated aggregate headers inherit the ownership of their highest layer (§7.1) | **DECIDED** |
| SM14 | C2MIR is retired and frozen. The module split does not edit, redesign, regenerate for, or add validation gates for the C2MIR implementation/artifacts (§9.1) | **DECIDED** |

---

## 4. Current state (evidence)

Findings from the tree as of 2026-07-21:

1. **A proto-module already links standalone.** `lambda-input-full` in `build_lambda_config.json` bundles input + format + validator — and had to pull in exactly the Mark data-model files (`lambda-data.cpp`, `name_pool`, `shape_pool`, `mark_builder/reader/editor`, `print`, `lambda-decimal`, `lambda-error`, `utf_string`, …) plus `parse.c`/`build_ast.cpp` (validator schema parsing) and `test/test_stubs.cpp`. That file list is ground truth for what the data layer needs.
   Beyond that it is a kitchen sink: it also inlines ~44 `lib/*.c` sources (mempool/arena/mem_context, the GC zones under `lib/gc/`, sqlite, url, cmdedit, shell, …) and four `radiant/*.cpp` files — see §6.1.
2. **The dynamic-module pattern exists and works**: `lang-python` builds as a PIC dylib into `modules/lang-python/` with `-undefined dynamic_lookup` (macOS) / `--allow-shlib-undefined` (Linux).
3. **The obvious `heap_alloc` call count is small, but the GC/lifetime coupling is not**: `push_k` directly uses `heap_alloc`, while list growth uses weak `heap_data_alloc` plus `RootFrame`/`Rooted` because allocation can compact data-zone buffers. The same TU contains weak root/weak-slot/overflow fallbacks and multiple rooted collection safepoints; `lambda-decimal.cpp` has a separate weak heap seam. Heap-backed constructors already largely live in `lambda-data-runtime.cpp`, so P1 completes that behavioral split (§9.2–§9.3).
4. **The registration pattern for reverse edges already exists**: `lambda/render_map.h` — "keeps the lambda layer radiant-agnostic: callers (radiant) register a path recorder … lambda layer holds only the function pointer to avoid a build-time dep on radiant."
5. **The active reverse edges are now normalized**: MIR-Direct event emission/selection use the rt-owned event hook, CSS counters and symbols use io-owned hooks, network-to-Radiant resource attachment uses the io-owned processor hook, and `log_mem_stage` is provided by `lib/log`. `lambda/headless_stubs.cpp` and the C2MIR-facing declarations it supports remain frozen under SM14; they are not evidence of an active module edge.
6. **The CSS/radiant straddle**: `lambda-input-full` must compile four radiant sources — `radiant/view_prop_defaults.cpp`, `radiant/view_prop_ensure.cpp`, `radiant/layout_counters.cpp`, `radiant/symbol_resolver.cpp` — because the CSS/DOM layer under `lambda/input/css/` uses radiant's view-property tables, counter formatting, and symbol resolution. Those files include radiant's `view.hpp`/`layout.hpp`.
7. **Radiant's include surface into lambda** (by count): dominated by `lambda/input/css/*` and `lambda-data.hpp`/`mark_reader.hpp` (downward, fine once SM2 lands), plus a runtime-embedding surface: `js/js_runtime.h`, `js/js_event_loop.h`, `js/js_dom*.h`, `transpiler.hpp` (an *internal* rt header), `render_map.h`, `network/*`.
8. **Validator is not referenced by input/format code** (only by tests and the CLI) — moving it up is clean.
9. **Boundary-check precedent**: `make check-hosted-python-module-boundary` runs `utils/check_hosted_python_architecture.py`; `make lint` runs the unified linter (`utils/lint/run.sh`, ~10 s fast pass: ast-grep + alint + hybrid + structural rules under `utils/lint/rules/{c-cpp,lambda,structural}`), `make lint-full` adds clang-tidy (~4 min).
10. **The Jube path for radiant already exists**: `import radiant;` resolves through `JubeModuleDef` (`ast.hpp:501`), and `lambda/module/radiant/radiant_dom_bridge.hpp` is already in the tree.
11. **`lambda.h`/`lambda.hpp`/`lambda-data.hpp` are mixed-layer headers, not yet core headers.** `lambda.h` declares `g_dry_run` (defined in `lambda-proc.cpp`), stack/root overflow entry points, heap allocation/root APIs, runtime collection helpers, `Context`, and many rt-provided constructors/evaluators. `lambda.hpp` now forwards the native `RootFrame`/`Rooted`/no-GC helpers from `runtime/lambda-root-frame.hpp` and Target from IO, but remains an aggregate compatibility header. `lambda-data.hpp` defines `EvalContext`, references the validator/scheduler/runtime error state, and mixes core type descriptors with runtime scalar materialization. Include-cleanliness alone does not establish ownership; SM13 requires these declarations/types to split (§9.1).
12. **C2MIR is retired and out of scope.** Its Makefile `lambda.h`→`lambda-embed.h` pipeline and C transpiler remain as frozen legacy code. P1 does not modify that pipeline, its generated artifact, or C2MIR sources, and C2MIR is not a migration gate. Active MIR-Direct/core/runtime headers must be separated without turning the retirement into a compatibility project (§9.1).
13. **The GC coupling in `lambda-data.cpp` is broader than two `heap_alloc` calls.** The file defines weak fallbacks for data-zone allocation, root/weak-slot registration, and root-frame overflow; it uses `RootFrame`/`Rooted` at multiple collection safepoints and mixes the rt TLS `context` with the io TLS `input_context`. `lambda-decimal.cpp` separately supplies a weak `heap_alloc` fallback and performs runtime decimal/BigInt boxing. P1 must split behavior before abstracting allocation (§9.2–§9.3).
14. **Mark API ownership is naturally split.** `MarkReader` is a pool-free value wrapper over `Item` and belongs core. `MarkBuilder`/`MarkEditor` take and dereference `Input*`, cache its arena/pools/type registry, and implement UI-mode `DomElement`/`DomText` allocation/relinking; they belong io. This resolves the former core→io edge by ownership rather than adding a callback abstraction (§9.4).
15. **`LAMBDA_STATIC` is a semantic include-order switch, not a static-link flag.** `input.hpp` and `format.h` define it before including `lambda-data.hpp`; it removes runtime declarations and changes scalar/list behavior in `lambda-data.cpp`. The future build consists primarily of static libraries, so retaining that name/behavior would be actively misleading. P1a/P1b replace it with ownership-specific headers and entry points; public headers must not define build-profile macros as a side effect.

---

## 5. SM1 — Static libraries, not dynamic

The property we want — a fixed, enforced interface — comes from the link DAG, per-module include rules, and umbrella headers. Dynamic linking adds cost without adding enforcement we can't get more cheaply (§8).

| Factor | Static libs | Dynamic libs |
|---|---|---|
| Interface enforcement | Via premake dependency DAG + include hygiene + boundary build (§8) | Stronger by default (linker-enforced per .so), but replicable for free in CI |
| Performance | Keeps LTO/inlining across `pool_calloc`/arena/`Str` hot paths | PLT indirection on allocator-grade calls; no cross-module LTO |
| ABI | None needed — `MarkReader`/`MarkBuilder`, input APIs, and `Item` helpers include C++ header surfaces | C++ headers can't form a stable ABI; would force a C shim layer |
| Packaging | One ~8 MB exe on macOS/Linux/Windows | rpath/`$ORIGIN`/DLL placement ×3 platforms; `dllexport` sweep for Windows |
| White-box tests | Archives expose all symbols at link time — tests keep working | Hidden symbols unreachable; would need test-only export builds |
| MIR JIT | In-process symbols; import resolution stays table-driven | Works (cross-image resolution proven by `lang-python`) but adds failure modes |
| Plugins / guest languages | N/A | Correct choice — already used by `lang-python`; future Jube native modules |

**Revisit triggers** (flip an individual module to dynamic only when one occurs):

- An external product embeds `liblambda` and needs independent updates.
- Radiant becomes an optional install (note: a *slim engine-only exe* does **not** require this — that is profile A / `lambda-cli`, §13.1).
- A Jube native module must ship as a prebuilt binary (currently source-only per `Lambda_Native_Module_Design.md`).

---

## 6. SM2/SM3 — Modules and layering

```
lib  →  lambda-data  →  lambda-rt  →  radiant  →  lambda.exe
        (core + io)      (engine)      (layout/render/shell UI)
```

Rules:

- A module may `#include` and link only from itself and layers to its **left**.
- Upward references are forbidden at link time; where a lower layer must call an upper one at runtime, the lower layer owns a **hook header** (typedef'd function pointers + `*_register()` + null-safe defaults) and the upper layer installs at init (§13).
- Third-party vendored libs (tree-sitter, MIR, mpdec, curl, ThorVG, …) stay static and attach at the layer that owns them.

| Module | Contents | Public surface (the contract) | Allowed deps |
|---|---|---|---|
| `lib` | `lib/` as today minus Item/runtime-specific files moved upward: `str`, `strbuf`, `arraylist`, `hashmap`, `mempool`, `arena`, `mem_context`/`mem_factory`, `log`, context-free `url`/raw `file`, `mime-detect`, … | Individual `lib/*.h` (already coherent) | libc, rpmalloc |
| `lambda-data` | `lambda/core/` (Mark/Item data model + `MarkReader`, §9) + io = `lambda/io/**` + `lambda/input/**` + `lambda/format/**` incl. `MarkBuilder`/`MarkEditor` and the CSS engine (§11) + shared-resource portions of `lambda/network/**` (SM12; radiant-facing TUs hookified or relocated, §8.2) | core: active pure value-model C/C++ headers, `lambda-data.hpp`, `mark_reader.hpp`; io: `mark_builder.hpp`, `mark_editor.hpp`, Target/resource API, `lambda/input/input.hpp`, `lambda/format/format.h`, CSS/DOM headers (`css_*.hpp`, `dom_*.hpp`, `selector_matcher.hpp`, `style_epoch.hpp`), network/resource API headers | `lib`; tree-sitter grammars used by inputs (latex-ts), utf8proc, mpdec, re2, zlib/brotli, curl + mbedtls + nghttp2 (SM12) |
| `lambda-rt` | `parse.c`, `build_ast.cpp`, active `transpile-mir.cpp`, `lambda-eval*.cpp`, `lambda-mem.cpp`, `lambda-data-runtime.cpp`, `lambda-stack.cpp`, `lambda-proc.cpp`, `runner.cpp`, `module_registry.cpp` + `lambda/module/**`, `lambda/js/**`, `lambda/validator/**` (SM7), `lib/gc/**`, `side_stack.c`, concurrency, sysinfo, `render_map.cpp` and other hook registries; the script manager + script/MIR caches (§2.1). Runtime-native JS/Lambda IO remains here per §2.2. Retired C2MIR sources/artifacts remain physically where they are and are excluded from migration edits/checks (SM14) | active `lambda/lambda-runtime.h` + `lambda-runtime.hpp`, a narrow **embed header** (`lambda/embed.h`: runtime create/run/eval + JS engine entry), validator API, hook-registry headers (`render_map.h`, event/counter hooks §13) | `lib`, `lambda-data`; MIR, tree-sitter(-lambda/-js/-ts), libuv (JS event loop; the uv shim itself is `lib/uv_loop.h`); runtime-native protocol dependencies only where §2.2 classifies them |
| `radiant` | `radiant/**` minus files relocated by SM8 | `radiant/radiant.hpp` umbrella (+ `view.hpp`/`layout.hpp`/`render.hpp`/`event.hpp` as semi-public, per the header-consolidation doctrine) | `lib`, `lambda-data`, `lambda-rt` (per SM10-A; revisit if SM10-B chosen); GLFW, ThorVG, freetype, png/jpeg/gif |
| `lambda.exe` | `main.cpp`, `main-repl.cpp`, CLI command wiring | — | everything above; the `lambda-cli` profile links without `radiant` (§13.1) |

Placement to confirm during P1 inventory (flagged, not blocking): `vmap.cpp`/`pack.cpp` (Jube projections → rt), `sys_func_registry.c` (rt; it is the JIT-import registry even though `lambda-input-full` lists it today), `template_registry/state.cpp` (rt; uses path-recorder hooks), `edit_bridge.cpp` (rt↔radiant seam), `path.c`/`re2_wrapper.cpp` (split pure value/path logic from runtime boxing/IO), `network/font_resource_faces.*` (split cached font-file acquisition in io from loaded face objects in radiant), `http_module.cpp` (script-facing Item glue stays rt; reusable transport belongs io), and whether any *input* (Mark format) still needs `parse.c` once the validator moves up.

### 6.1 Realigning the existing build groups: `lambda-input-full` → `lambda-data`

`lambda-input-full` is the **seed of `lambda-data`, not a casualty** — realign and rename it in place rather than building a parallel target. Today it is a kitchen-sink test library: `link: dynamic`, referenced by ~114 test-suite entries in `build_lambda_config.json`, two Makefile targets, and name-special-cased in `utils/generate_premake.py` (derived `lambda-input-full-*` project names, plus a **phantom `lambda-runtime-full`** branch — no such target exists in the config; dead generator code to remove). Its contents = the §6 data layer **plus** an inlined copy of ~44 `lib/*.c` sources, the validator + `parse.c`/`build_ast.cpp`, four `radiant/*.cpp` files (§4.6), and `test/test_stubs.cpp`.

Staged with the §15 plan:

| Step | Change |
|---|---|
| P2 | **Rename** `lambda-input-full` → `lambda-data`: one-shot sed of the ~114 test-dep references; update the generator's name special-cases and drop the phantom `lambda-runtime-full` branch; repoint the two Makefile targets |
| P2 | **Stop inlining `lib/`**: the ~44 `lib/*.c` entries become a link dependency on `lib.a` — ends double-compilation of lib across the test fleet. `lambda-process` retires the same way (it is a lib subset; archives dead-strip by member, so a curated subset buys nothing) |
| P2 | **`link: dynamic` → static** per SM1. The old "force DLL on Windows to avoid static library dependency issues" workaround is already commented out in `generate_premake.py`; re-verify Windows once layering is clean — the historical static-link trouble most plausibly came from the kitchen-sink grouping itself |
| P3 | **Shed upward content**: `lambda/validator/**` + `parse.c` + `build_ast.cpp` → `lambda-rt` (SM7); the four radiant files drop out via the css_prop split (SM8); `test/test_stubs.cpp` moves to the individual test targets that still need it (gone by P6) |

End state: one name everywhere — config target, premake project, archive (`liblambda-data.a`), and lint-matrix module id are all `lambda-data`.

---

## 7. SM4 — `LAMBDA_API` and hidden visibility

Add `lib/lambda_api.h`:

```c
#if defined(_WIN32) && defined(LAMBDA_BUILD_SHARED)
  #define LAMBDA_API __declspec(dllexport)      /* per-module import/export split
                                                   deferred until a DLL consumer exists */
#elif defined(LAMBDA_BUILD_SHARED)
  #define LAMBDA_API __attribute__((visibility("default")))
#else
  #define LAMBDA_API
#endif
```

- Compile all modules with `-fvisibility=hidden` (and `-fvisibility-inlines-hidden`).
- Annotate the public surfaces listed in §6 — functions, and classes whose vtable/typeinfo crosses the boundary (e.g. `MarkBuilder` if virtuals appear).
- **In the normal static build this is inert**: archive members expose all symbols to the final link, so nothing changes for the exe or for white-box tests. The annotations become *load-bearing only in the boundary build* (§8), which is exactly the point: the macro documents the contract today and makes any future flip to dynamic mechanical.
- One macro for now. Per-module macros (`LDATA_API`, `LRT_API`, …) only become necessary for Windows DLLs; don't pay that tax yet.

### 7.1 SM13 — provider ownership is stronger than include ownership

For every function declared by a module's public header, the definition must be supplied by that module or a layer to its left. A header is not core-owned merely because it includes only core/lib headers. In particular, a core header may not declare rt-provided heap, root-frame, evaluator, or procedural-IO functions and rely on the final executable to complete them.

Enforcement:

- Tier 1 associates every active public header with an owning module and checks its declarations against a generated symbol-provider inventory. Any future generated aggregate header belongs to the highest layer it aggregates; frozen C2MIR compatibility artifacts are excluded under SM14.
- Tier 2 links a tiny consumer probe for each public surface against that module plus only its allowed lower dependencies. This catches a declaration whose implementation actually lives above the claimed owner even when no production source in the lower module happens to call it yet.
- Umbrella headers may re-export lower-layer APIs, but they do not transfer ownership. `lambda/embed.h` is rt-owned and may include core/io public headers; `lambda/core/lambda.h` remains satisfiable by core + lib alone.

---

## 8. SM5 — Boundary enforcement and `make lint` integration

Answering "can the boundary build be part of `make lint`": **partly — split it into two tiers so the fast sweep stays ~10 s.**

**Tier 1 — include/capability/provider rules, run inside `make lint`.**
A structural rule (`utils/lint/rules/structural/module-include-boundary`) checks every `#include` against the normative matrix (adopted rev 4):

| Source | May include/link only |
|---|---|
| `lib/**` | `lib/` |
| `lambda/core/**` | `lib/` + `lambda/core/` |
| io = `lambda/io/**` + `lambda/input/**` + `lambda/format/**` + shared-resource `lambda/network/**` (SM12) | `lib/` + `lambda/core/` + io |
| rt = rest of `lambda/**` | `lib/` + core + io + rt — never `radiant/`, never shell files; runtime-native IO exceptions are classified per §2.2 |
| `radiant/**` | everything above + `radiant/` — never shell files |

- Layering is **transitive**: each module also uses all layers beneath it (radiant legitimately includes lib/core/io directly — ~580 such includes today). Within the rt allowance, radiant is further narrowed to `lambda/embed.h` + intentionally public `js/*.h` per SM10-A (tightens finding §4.7).
- Shell (`main.cpp`, `main-repl.cpp`) sits on top and may include anything; `test/**` is governed by the white-box whitelist (§14); vendored `tree-sitter-*`, the `lang-python` guest module, and frozen retired C2MIR sources/artifacts (SM14) are out of scope.
- SM12 adds capability checks that the ordinary DAG cannot express: direct curl/shared-resource-cache use is io-only; rt/radiant shared-fetch call sites go through io; runtime-native libuv/file/socket sites are held to the explicit §2.2 inventory.
- SM13 adds the public-header/provider inventory and per-surface consumer probes (§7.1). Include-clean headers that advertise upward implementations are violations. Frozen C2MIR compatibility headers are not active public module surfaces and are excluded under SM14.

The architectural inventory remains a fast report so it can show the remaining migration backlog without blocking unrelated work. The linker rule is enforceable on demand through `make lint ARGS='--rule ^module-boundary-link$'`: its four strict DSOs reject project undefined symbols. The one approved exception is rt → radiant Class F (§8.1), whose exact import baseline is ratcheted: an addition fails the gate and a removal is reported as progress.

**Tier 2 — linker-enforced boundary build, in CI / `lint-full` / on demand.**
A premake config (`make check-module-boundary`, modeled on `check-hosted-python-module-boundary`) builds **five real validation SharedLibs** with `-fvisibility=hidden`: `lib-boundary`, `lambda-core-boundary`, `lambda-io-boundary`, `lambda-rt-boundary`, and `radiant-boundary`. Each validation target compiles the exact source manifest of its shipped owner (rather than a reduced probe); core remains separately compiled so the core source subset is validated without io. The shipped artifact still combines core + io into `liblambda-data.a`.

- `lib`, core, io, and radiant use fatal undefined-symbol linking (Linux `-Wl,--no-undefined`; macOS default two-level namespace). Any upward call or missed hook names its exact symbol at link time.
- The rt DSO is the deliberate exception: Class-F JS DOM/CSSOM/editing/clipboard/history bindings still resolve in Radiant. It uses platform dynamic lookup **only** until that binding move is scheduled. `utils/check_module_boundary.py` extracts those imports from the emitted image and compares them to `utils/static_module_rt_class_f_baseline.txt`: the baseline is 165 imports, additions fail, and deletions are visible progress. The frozen C2MIR PDF resolver compatibility symbol is included in that explicit baseline; no C2MIR source or generated artifact is changed.
- This is a full compile, so it remains an on-demand lint rule (`make lint ARGS='--rule ^module-boundary-link$'`) and a required release/CI gate where CI is configured. It does not join the fast lint sweep.

The shipped artifact remains the fully static exe; the five-DSO config exists purely as the strictest available linter. The boundary build is link-checked, not a supported dynamic runtime profile.

### 8.1 Audit baseline (2026-07-21): the matrix is achievable with zero exceptions

The rev-4 full-tree scan found **56 violating includes in ~24 files** under the earlier classification. Rev 11 assigns `MarkBuilder`/`MarkEditor` to io, which resolves class E's 5 includes by correct ownership and leaves **51 violations in ~22 files** across the remaining classes (`test/**`, vendored `tree-sitter-*`, `lang-python`, and frozen C2MIR excluded). None requires a standing exception; class A still requires a real active-header/provider split rather than mechanical include relocation.

| # | Violation | Cnt | Resolution class → phase |
|---|---|---|---|
| 1 | core → rt: mixed active use of `lambda.h`/`lambda.hpp` plus `ast.hpp`/`ast-core.hpp` pulled by `lambda-data.hpp`, `name_pool.hpp`, `shape_pool.hpp`, `shape_builder.hpp`, `binary.h`, `utf_string.h`, `lambda-error.cpp`, `print.cpp`, `lambda-data.cpp` | 14 | **A — split active headers by provider, not include cleanliness** (rev 11): active core value layouts/pure APIs → core headers; rt `Context`, GC/rooting/runtime declarations → `lambda/lambda-runtime.h/.hpp`; Target pure identity/equality stays core while resource/existence operations move io. Retired C2MIR/header embedding is frozen and excluded (SM14). Type descriptors are core; **`ast.hpp`/`ast-core.hpp` + AST builder stay rt**; `print_ast_node` extracts from core `print.cpp` → rt. → P1a/P1b |
| 2 | io → rt (per-file detail in §8.2) | 6 | Mostly **dissolves** once active core value headers replace mixed `lambda.h`/`lambda.hpp`; `windows_compat.h` → `lib/`; two apparently-vestigial includes (`ast.hpp` in `format.cpp`, `ts-enum.h` in `input-latex-ts.cpp`) dropped after compile-verify → P1 |
| 3 | lib → core/rt/radiant: `item_tagged.hpp`, `lambda_typed.hpp`, `tagged.hpp`, `test_utils_runtime.cpp`, `gc/gc_heap.c`, `side_stack.c` | 7 | **B/C — per-file review** under the decided policy: *generalize so it doesn't depend on core/rt/radiant, else move up* (outcomes in §8.2). `gc_heap.c` traces Lambda Items and JS environments; `side_stack.c` mutates rt `Context` root/number/recovery state. Both are **rt**, not core. → P1b |
| 4 | former core → io: `mark_builder.cpp`/`mark_editor.cpp` include and dereference `Input`, `dom_element.hpp`, `dom_node.hpp` | 5 | **E — resolved by ownership (rev 11)**: move `MarkBuilder`/`MarkEditor` headers and implementations to io. They are document construction/editing services over io-owned `Input`/DOM state, not core value primitives. `MarkReader` remains core. → P1b |
| 5 | io → radiant: `dom_element.cpp`, `dom_node.cpp`, `format-html.cpp` include `view.hpp` | 4 | **D — SM8 css_prop split** (§11) → P3 |
| 6 | rt → radiant: `js_dom.cpp`, `js_dom_observers.cpp`, `js_dom_selection.cpp`, `js_clipboard.cpp`, `js_formdata.cpp`, `module/radiant/*`, `network_resource_manager.cpp`, `resource_loaders.cpp` | 20 | **F — DEFERRED** (decided rev 5): settle rt and the layers below first. The eventual shape remains the browser-style engine/bindings split (bindings compile into `radiant.a`, register via `js_dom.h`/`module_registry`); until scheduled, these 20 includes stay a **ratcheted report-only baseline** in the tier-1 rule → future |

Findings that adjust earlier sections:

- `emit_sexpr.cpp` is the AST s-expression emitter (formal-semantics bridge) — reclassified **rt**, removed from the §9.1 core list; `print.cpp`'s `print_ast_node` follows it. `lambda/js/js_exec_profile_weak.h` is rt instrumentation and remains rt; lower code must not include it after the data/GC split. `lambda-number-runtime.hpp` is rt by its scalar-home/boxing role; extract any representation-only definitions needed by core into a core number header rather than moving the runtime header wholesale.
- Even rule 1 (`lib` self-contained) is not clean today: six lib files need the §8.2 outcomes.
- The core→rt heap seam is currently papered over by **multiple weak-symbol fallbacks**: `heap_alloc` in `lambda-decimal.cpp`, and data-zone/root/weak/overflow fallbacks in `lambda-data.cpp`. P1 removes them by splitting runtime behavior upward rather than replacing them with an optional core heap service (§9.2–§9.3). Weak definitions hide missing-registration errors and neutralize the tier-2 no-undefined check.
- The include audit cannot see **extern-declared** upward calls (`heap_alloc` core→rt [SM6]; `dispatch_emit` rt→radiant, `counter_format`/`resolve_symbol*` io→radiant [SM8/SM9]) — catching those is exactly the tier-2 link build's job.

### 8.2 Violation work list (the backlog)

Per-file inventory from the audit with decided resolutions — the concrete migration backlog the tier-1 rule ratchets against.

**core → rt (class A, P1)**

| File | Offending includes | Fix |
|---|---|---|
| `lambda/lambda-data.hpp` | `ast-core.hpp`, mixed `lambda.hpp` | include the split core `lambda.hpp`; relocate representation definitions it still needs from `ast-core.hpp` into core and sever that include |
| `lambda/name_pool.hpp`, `shape_pool.hpp`, `shape_builder.hpp`, `utf_string.h`, `binary.h` | mixed legacy/active `lambda.h` | migrate active code to the core value header; rt declarations move to `lambda-runtime.h`; frozen C2MIR compatibility surfaces are not edited (SM14) |
| `lambda/lambda-error.cpp` | mixed `lambda.h` | include the active core value header; any runtime behavior in the TU moves rt |
| `lambda/print.cpp` | `ast.hpp` | `TypeType` already core (`lambda-data.hpp:623`); extract `print_ast_node` (:936) → rt |
| `lambda/lambda-data.cpp` | `ast.hpp`, `js/js_exec_profile_weak.h`, `lambda-number-runtime.hpp`, rt `Context`/roots/heap calls | split the TU by behavior (§9.2–§9.3): representation and pool/arena operations → core; safepoint/rooting/scalar-home/runtime boxing → `lambda-data-runtime.cpp` or another rt TU |
| `lambda/emit_sexpr.cpp` | `transpiler.hpp` | file reclassified → rt |

**io → rt (P1)** — the full detail:

| File | Offending includes | Fix |
|---|---|---|
| `lambda/input/input.cpp` | mixed `lambda.h`; also constructs the current mixed `Context` | use active core values plus an io-owned `InputContext`; no rt `Context` |
| `lambda/input/input-latex-ts.cpp` | mixed `lambda.h` (comment: "for `it2s()`"), `ts-enum.h` | include core value API; `ts-enum.h` shows no symbol use — drop after compile-verify |
| `lambda/format/format.cpp` | `ast.hpp` | no AST usage found — vestigial; drop after compile-verify |
| `lambda/input/css/dom_element.hpp` | mixed `lambda.hpp` | include split core value API; DOM/Target IO effects do not move into core |
| `lambda/format/format-xml.cpp` | `windows_compat.h` | `lambda/windows_compat.h` → `lib/` |

**lib → core/rt/radiant (classes B/C, P1)** — generalize-or-move review:

| File | Depends on | Outcome |
|---|---|---|
| `lib/item_tagged.hpp`, `lib/lambda_typed.hpp` | `lambda-data.hpp` (Item-typed; users: core + js) | move → `lambda/core/` (not generalizable — Item is the point) |
| `lib/tagged.hpp` | `radiant/view.hpp` (`ViewTagToType` templates; users: radiant only) | move → `radiant/` |
| `lib/test_utils_runtime.cpp` | `transpiler.hpp` | test support — move → `test/` |
| `lib/side_stack.c` | mixed `lambda.h`; directly reads/writes rt `Context` side-root, side-number, and recovery watermarks | move → `lambda-rt`; keep only an rt public header if generated/native code needs the API |
| `lib/gc/gc_heap.c` (+ zone files) | Item layouts, JS-environment tag/trace hooks, runtime collection/finalization policy | move → `lambda-rt`; the collector consumes core value-layout definitions but is not part of the static Mark model |

**former core → io (class E, P1b; resolved by reclassification)**

| File | Offending includes | Fix |
|---|---|---|
| `lambda/mark_builder.cpp/.hpp` | `Input`, `dom_node.hpp`, `dom_element.hpp` | move intact to io (for example `lambda/io/mark_builder.*`, with compatibility forwarding includes during migration); its `Input*` and UI/DOM behavior are charter-aligned there |
| `lambda/mark_editor.cpp/.hpp` | `Input`, `dom_node.hpp`, `dom_element.hpp` | move intact to io alongside the builder; DOM relinking remains an io implementation detail |

**io → radiant (class D, P3)**: `dom_element.cpp`, `dom_node.cpp`, `format-html.cpp` → `view.hpp`; cleared by the SM8 css_prop split.

**rt → radiant (class F, DEFERRED)**: `js_dom.cpp`, `js_dom_observers.cpp`, `js_dom_selection.cpp`, `js_clipboard.cpp`, `js_formdata.cpp`, `module/radiant/radiant_dom_bridge.cpp`, `module/radiant/radiant_module.cpp` → `view/event/render/layout.hpp`; sorted out after rt and the layers below are settled.

**resource network → radiant (pulled forward out of class F by SM12, P3)**: `network/network_resource_manager.cpp` + `network/resource_loaders.cpp` are the only two `lambda/network/**` files touching radiant (the other nine have lib-only + intra-network includes). `network_resource_manager.cpp` is the shared download/cache hub → **stays io**, with radiant notification supplied through a lower-owned registration surface; `resource_loaders.cpp` loads media for rendering → **belongs radiant**. `font_resource_faces.*` splits by the same rule: font-file acquisition/cache stays io; loaded face objects belong radiant. This inventory does **not** classify the separate LambdaJS `js_fs`/`js_net`/`js_dns`/`js_fetch`/`js_tls` paths; those remain rt or split reusable transport portions into io under §2.2.

---

## 9. SM6 — `lambda/core/`: split ownership before moving files

### 9.1 Active header ownership; C2MIR frozen

The active `lambda.h`/`lambda.hpp`/`lambda-data.hpp` usage is split by provider:

| Surface | Owner | Contents |
|---|---|---|
| active core value C header | core | C-compatible Item tags/layouts, container/value structs, pure conversions and operations provided by core/lib. Its final name is chosen so it does not require modifying the retired C2MIR compatibility header. |
| active core value C++ header | core | C++ value/model helpers provided by core/lib. No `RootFrame`, GC registration, runtime `Context`, dry-run state, or effectful Target operations. |
| `lambda/core/lambda-data.hpp` | core | Type descriptors, shapes, Decimal/value layouts, and core-owned data operations. AST nodes needed only as opaque references are forward-declared; no `EvalContext`, validator/scheduler state, or runtime scalar materializers. |
| `lambda/lambda-runtime.h` | rt | Active runtime ABI: GC allocation, root/side-stack entry points, evaluator/runtime constructors, stack errors, dry-run/procedural runtime state, and other symbols provided by rt. Includes/reuses the active core C value model. |
| `lambda/lambda-runtime.hpp` | rt | `Context`/`EvalContext`, `RootFrame`/`Rooted`, no-GC guards, runtime allocation/scalar materialization helpers, validator/scheduler/error state, and other C++ rt facilities. |
| `lambda/embed.h` | rt | Narrow native embedding façade: runtime create/run/eval plus intentional JS entry points. |

`Target` also splits by capability: its identity representation, normalized key/hash, and pure equality required by `Name` stay core; cwd/filesystem resolution, `exists`/`is_dir`, and resource acquisition live io. `target.cpp` becomes core identity/conversion code plus an io effects TU. This avoids replacing the current mixed header with a new core→io edge.

**SM14 freeze rule:** do not edit C2MIR source, its `lambda.h`→`lambda-embed.h` pipeline, or its generated artifact as part of this proposal. Do not add compatibility work or a C2MIR test gate. Active native/MIR-Direct code migrates to the new ownership-specific headers. If an active header extraction would require a C2MIR edit, choose a new active header name and leave the retired compatibility surface untouched; the frozen legacy surface is not a public module contract and is excluded from SM5/SM13 checks.

P1a also retires `LAMBDA_STATIC` as a public-header side effect and semantic selector. `input.hpp`/`format.h` stop defining it. Core vs rt behavior is selected by calling ownership-specific APIs, not by whether an io header happened to be included first; any temporary build define used during migration must have a narrow name and disappear by P1b.

### 9.2 File classification

Create `lambda/core/` only after 9.1 identifies the pure subset. The expected destinations are:

```
core value subset of lambda.h/.hpp     name_pool.cpp         core part of print.cpp
core part of lambda-data.cpp/.hpp      shape_pool.cpp        binary.cpp
decimal representation/pure ops       shape_builder.cpp     target identity/equality
core error value representation        mark_reader.cpp/.hpp
utf_string.cpp                         item_tagged.hpp       lambda_typed.hpp
```

The following move to io: `mark_builder.cpp/.hpp` and `mark_editor.cpp/.hpp` (prefer `lambda/io/` as their durable home), retaining their `Input`/DOM-aware behavior. The following stay or move to rt: `Context`/`EvalContext`, `RootFrame`/`Rooted`, `lambda-data-runtime.cpp`, heap/safepoint portions extracted from `lambda-data.cpp`, runtime decimal/BigInt boxing, `lambda-number-runtime.hpp`, `lambda-mem.cpp`, `lambda-stack.cpp`, `lib/gc/**`, `side_stack.c`, and `mem_factory_rt.*`. `emit_sexpr.cpp` and `print_ast_node` stay rt with the AST. `ast.hpp`/`ast-core.hpp` and AST builder stay rt. Retired C2MIR files remain untouched in place under SM14.

This is not a mechanical `git mv` until the active core/rt behavior split is complete. P1a changes active interfaces; P1b performs the mechanical moves after those boundaries compile. Moving builder/editor to io is itself mechanical apart from include-path/build-list updates because their current behavior already matches the io charter.

### 9.3 `lambda-data.cpp`: GC allocation is a safepoint, not an allocator flavor

The live coupling is wider than `push_k()` plus list expansion:

- weak fallbacks exist for `heap_data_alloc`, root/weak registration, and root-frame overflow;
- `RootFrame`/`Rooted` protects lists, arrays, maps, source Items, and incoming values across allocations that may compact the data zone;
- both rt TLS `context` and io TLS `input_context` influence behavior;
- `lambda-decimal.cpp` has its own weak `heap_alloc` fallback and runtime boxing paths.

Therefore P1 does **not** introduce `CORE_ALLOC_HEAP` or a null-safe `CoreHeapOps`. A raw `alloc`/`expand` callback cannot express the invariant that an allocation may collect, relocate the owner, and require the caller to reload every protected pointer. A missing heap service is not an optional feature and must never degrade to a weak/null/malloc fallback.

The split is:

- core owns explicit pool/arena constructors and non-allocating value/container algorithms;
- rt owns heap constructors, GC data-zone growth, runtime boxing, and every algorithm containing a collection safepoint or `RootFrame`;
- shared algorithmic shapes may be extracted only after the ownership-specific entry points are correct, using an internal parameterized helper whose contract includes relocation/rooting—not a generic raw allocator callback;
- `push_k`/runtime datetime materialization and decimal/BigInt boxing move rt; parser-built datetime/decimal values use arena/pool-owned core forms;
- `expand_list` becomes ownership-explicit arena growth in core and GC-safe heap growth in rt. The ambient "no arena means heap" choice is removed;
- `input_context` no longer supplies hidden allocation policy to core. IO-owned builder/editor/input paths call ownership-explicit pool/arena core APIs; rt passes its explicit runtime context.

Constructor consolidation is P1c, after focused allocation/lifetime tests. It is not bundled into the first file move merely to eliminate superficially similar variants.

### 9.4 Mark API ownership

- **Core:** `MarkReader` only. It is a stack/value traversal API over `Item`, requires no `Input`, pool, arena, DOM, or GC, and is useful to every upper layer.
- **IO:** `MarkBuilder` and `MarkEditor`. They construct/edit Input-owned documents, take `Input*`, cache its pool/arena/name/type state, and implement DOM-aware UI mode. Those are io responsibilities, not accidental dependencies to abstract away.
- Upper layers (`lambda-rt`, radiant, tests) include the io public builder/editor headers through the normal downward DAG. No hook, `MarkContext`, subclass, or provider exception is needed.
- During relocation, compatibility forwarding headers may preserve include paths temporarily, but the forwarding headers are io-owned and removed once call sites migrate. There must be one implementation and one authoritative declaration.

### 9.5 P1 exit invariants

- Source-group link probes (and, once P2 creates them, `lambda-core-boundary`/`lambda-io-boundary`) resolve with only their allowed lower groups. Neither has weak or undefined rt symbols.
- Every core public declaration is satisfied by core + lib (SM13 consumer probe).
- Static parsing/Mark construction runs without initializing a GC/runtime context.
- Runtime list/array growth and decimal/datetime boxing pass focused stress-GC tests that force collection at the allocation boundary.
- `input.hpp`, `format.h`, and all other public headers are macro-hygienic: none defines `LAMBDA_STATIC` or another module/profile selector that changes downstream declarations or semantics.
- `lib/gc/**`, `side_stack.c`, and `mem_factory_rt.*` are rt-owned; core contains no side-stack, root-frame, collection, JS-environment, or runtime recovery policy.
- Retired C2MIR sources, embedded-header generation, and artifacts have no diff and are absent from the validation/test plan (SM14).

**Alignment with Memory Context**: future Heap/Nursery factories belong in rt because they implement runtime GC ownership. IO-owned builders/editors continue using pool/arena facilities provided below; core does not gain a heap mode.

---

## 10. SM7 — Validator moves to `lambda-rt`

- Evidence (§4.8): nothing under `lambda/input/`/`lambda/format/` calls the validator; only the CLI (`lambda.exe validate`), runtime, and tests do.
- The validator needs the Lambda front-end (`parse.c` + `build_ast.cpp`) to parse `.ls` schemas — both live in `lambda-rt`, so the move *removes* the front-end from the data layer (today `lambda-input-full` must compile them solely for the validator).
- `lambda-input-full` test targets that exercise the validator re-point to `lambda-rt` (§14).
- Follow-up check in P1: whether Mark-format input needs `parse.c` independently; if yes, that specific dependency needs its own decision (small mark-reader in data layer vs. accepting the parser there).

---

## 11. SM8 — CSS property tables: make `lambda/input/css` self-contained

Today the dependency points the wrong way: the CSS/DOM layer (destined for `lambda-data`) uses radiant-owned code — `view_prop_defaults.cpp`, `view_prop_ensure.cpp` (typed against radiant `view.hpp`), `counter_format` from `layout_counters.cpp`, and `resolve_symbol*` — which is why `lambda-input-full` compiles four `radiant/*.cpp` files (§4.6, incl. `symbol_resolver.cpp`) and why `headless_stubs.cpp` exists.

Plan:

1. **Split the style-side property model out of `view.hpp`** into `lambda/input/css/css_prop.hpp/.cpp`: the specified-style property structs, their defaults (`view_prop_defaults`), and ensure/instantiation helpers (`view_prop_ensure`). Radiant includes `css_prop.hpp` from below instead of the CSS layer reaching up. Layout-only view state stays in `view.hpp`.
2. **Counters**: the CSS-generic part (counter value formatting for list styles etc.) moves beside the css engine; the layout-tree traversal part stays in radiant. If the css layer genuinely needs layout-time counter state, that becomes a registered hook (§13) rather than an extern.
3. **`resolve_symbol*`**: same treatment — move if it's css-generic, hook if it's radiant-specific.
4. Exit criterion: `lambda-data` builds with **no radiant sources and no radiant includes**, and the counter/symbol stubs disappear from `headless_stubs.cpp`.

---

## 12. SM10 — The radiant ↔ lambda relationship (DECIDED: Option A)

Both directions exist today: radiant **embeds** the engine (script_runner compiles/runs Lambda pages and JS; includes `js_runtime.h`, `js_event_loop.h`, `js_dom*.h`, `transpiler.hpp`, `network/*`), and the engine **calls up** into radiant (`dispatch_emit`, counters, symbol resolution, path recording). Three candidate shapes:

| Option | Shape | Pros | Cons |
|---|---|---|---|
| **A. Layered** (formalize today's reality) | `radiant` sits above `lambda-rt`; embeds it through a narrow `lambda/embed.h` + public `js/*.h`; reverse edges become hooks (SM9) | Smallest delta; matches the state-management design (pages persist by regeneration — Lambda scripts are load-bearing inside radiant); DAG stays acyclic | Radiant not reusable without the engine; embed surface must be actively policed (today it includes `transpiler.hpp` internals) |
| **B. Shielded** | `radiant` depends only on `lib` + `lambda-data`; **`lambda.exe` inter-links** both and installs all script↔UI glue (script_runner, JS DOM bindings move to a bridge layer owned by the exe) | Radiant becomes an engine-free C++ layout/render library over Mark DOM; cleanest reuse story | Large extraction: scripting/event/state paths are woven through radiant by design; the bridge layer re-creates today's coupling one level up; no current consumer needs an engine-free radiant |
| **C. Jube-module mediated** | Script-facing radiant surface exposed via `JubeModuleDef` descriptors (`import radiant;` — `ast.hpp:501`, `lambda/module/radiant/radiant_dom_bridge.hpp` already exists); radiant consumes the single-tier host API struct per `Lambda_Native_Module_Design.md` | Script→radiant direction becomes data-driven (no link-time dep from rt); congruent with `lang-python`; the radiant-dom POC is already the plan of record | Only covers the script→radiant direction; radiant embedding the engine (running scripts at all) still needs A or B |

**Decision (rev 3): Option A** — keep it simple. Radiant sits above `lambda-rt` and embeds it; the module split proceeds on A alone:

- The engine-inside-radiant direction is architectural (the Radiant state-management and concurrency designs assume Lambda scripts run inside radiant); the DAG stays `… → lambda-rt → radiant`.
- Narrow the embedding surface to `lambda/embed.h` + intentionally public `js/*.h`; ban `transpiler.hpp`-grade internal includes from radiant via the tier-1 lint matrix (applied in P4).

B and C remain documented above **for future reference**:

- **B (shielded)** — KIV. Revisit trigger: an engine-free radiant consumer appears (embedding radiant as a pure C++ layout/render library elsewhere).
- **C (Jube-mediated script surface)** — not part of the module split, but fully compatible with A. The `radiant_dom_bridge.hpp` / `import radiant;` track continues independently under `Lambda_Native_Module_Design.md`; adopting C later removes rt→radiant *script-binding* knowledge without changing the A-shaped link DAG.

---

## 13. SM9 — Normalize the runtime→radiant reverse edges

Uniform pattern (the `render_map.h` precedent): the **lower** module owns a hook header — typedef'd function pointers, a `*_register()` call, null-safe defaults — and radiant installs implementations during its init. No `extern` into an upper module, ever.

Inventory to convert (from `headless_stubs.cpp` and direct externs):

| Edge | Today | Target |
|---|---|---|
| `lambda-proc.cpp` → `dispatch_emit()` / selection in `radiant/event.cpp` | rt-owned event hook; Radiant registers | complete for active MIR-Direct |
| css/DOM → `counter_format` (`layout_counters`) | io-owned counter hook; Radiant registers | complete |
| css/DOM → `resolve_symbol`, `resolve_symbol_string` | io-owned symbol hook; Radiant registers | complete |
| `log_mem_stage` | `lib/log.h` API, provided by `lib/log.c` | complete |
| source-path recording | `render_map.h` registration — **already correct** | keep as the template |

Exit criteria for active MIR-Direct: the boundary baseline reports zero upward externs and all reverse calls use lower-owned hooks or lib providers. `headless_stubs.cpp` and `LAMBDA_HEADLESS` remain a frozen C2MIR compatibility enclave under SM14; they are explicitly excluded rather than redesigned or deleted. See §13.1.

### 13.1 Headless is two profiles, not one

| Profile | Link shape | What "headless" means |
|---|---|---|
| **A — `lambda-cli`** | `lib + lambda-data + lambda-rt`, **no `radiant.a`** | No layout/render capability at all: convert/validate/run/REPL. This is the profile that `headless_stubs.cpp` + the `LAMBDA_HEADLESS` define serve today; after SM9 the stubs are replaced by the hooks' null-safe defaults, and both the file and the define disappear. No freetype/GLFW/ThorVG in the binary. |
| **B — `lambda` (full) in headless mode** | everything incl. `radiant.a` | Headless-browser mode: full layout/render/JS with no visible window. Already a **runtime** mode, not a build variant — `ui_context_init(uicon, headless)` runs windowless by default (optionally a hidden GLFW window via `LAMBDA_HEADLESS_GLFW_WINDOW` for GL-dependent paths); GLFW initialization is confined to `ui_context.cpp`; `window.cpp` provides the headless layout-test entry and headless animation-frame ticking for JS. This is the substrate for `make layout`, event_sim/WebDriver-style automation, and CI rendering. |

Consequences for the split:

- The two must not be conflated. Profile A is a **link-time** choice (omit `radiant.a`); profile B is a **runtime** flag inside radiant. No `#ifdef`-stripping of display code — the *same* `radiant.a` serves windowed and headless runs.
- SM9's stub-deletion exit criterion applies to profile A only; profile B never needed stubs.
- Profile B imposes an invariant on radiant internals as SM8 moves files around: display/window initialization stays confined behind `ui_context`, and no layout/render/font path may hard-require a live display (audit — §16.1).

---

## 14. SM11 — Test builds

- Test executables link the module static libs instead of recompiling source groups: `lambda-input-full` is realigned and renamed to `lambda-data` per §6.1 (+ `lambda-rt` for validator/runtime suites); `test_stubs.cpp` retires wherever real layering makes it unnecessary. Expected side effect: `make build-test` gets faster (shared compilation, smaller relinks).
- **Classification, case-by-case** (the user-flagged concern — some tests use functions that are not public API):
  - **Black-box** tests: include only the §6 public surfaces (layout suites via radiant public API, CLI-level lambda tests, input/format round-trips).
  - **White-box** tests: include internal headers deliberately (GC internals, transpiler, shape pools). These stay legal — static archives expose all symbols at final link, and `-fvisibility=hidden` does not bite in static builds (§7). They are recorded in a whitelist consumed by the tier-1 lint rule, so internal-header usage is *declared*, not accidental.
  - The whitelist doubles as the worklist: any test that could become black-box by adding one missing accessor to the public surface tells us the public surface is incomplete — fix the surface, shrink the whitelist.
- The boundary build (tier 2) excludes `test/**` entirely; it checks module-to-module edges only.

---

## 15. Implementation plan

Each phase ends green: `make build` + `make test-lambda-baseline` + `make test-radiant-baseline` (+ `make layout suite=baseline` where radiant is touched).

| Phase | Work | Gate |
|---|---|---|
| **P0** | **COMPLETE.** Added `lib/lambda_api.h` (`LAMBDA_{LIB,CORE,IO,RT}_API`, `RADIANT_API`) as the future public-surface annotation. Static target compilation now uses hidden visibility; the existing hosted-language DSO keeps its reviewed visibility path until it has an explicit exported host ABI. Added `utils/check_static_module_architecture.py` to the unified lint structural checks in report-only mode; it inventories frozen C2MIR exclusions, active public-header provider markers, include direction debt, §2.2 runtime-native IO classifications, and the predicted unresolved/weak-link baseline that the five DSOs must retire. `static-module-inventory` and `static-module-boundary-baseline` write JSON review artifacts under `temp/`. Declared all five planned boundary DSOs under `static_module_validation` in the build config; generation/enforcement waits for P2/P5 when source groups are real. | `make build`; `make lint ARGS='--rule ^static-module-architecture$'`; `make static-module-inventory`; `make static-module-boundary-baseline`; no C2MIR diff |
| **P1a** | **IMPLEMENTED.** Public `input.hpp` and `format.h` no longer define `LAMBDA_STATIC`; the narrow internal `LAMBDA_IO_STATIC_VALUES` mode is only compiled into the static input implementation. `InputAllocationContext` owns parser/document Pool/Arena/UI policy through its own TLS slot, while runtime allocation, roots, stack limits, scalar homes, and dry-run state remain on `EvalContext`/`Context`. Active paths select their owner explicitly rather than inheriting allocation behavior from include order. The P0 report continues to inventory public selector regressions, excluding frozen C2MIR. | static input/core DSO links; macro-hygiene inventory; `make test-lambda-baseline`; no C2MIR diff |
| **P1b** | **IMPLEMENTED.** `MarkBuilder`/`MarkEditor` and Target URL/path normalization live in `lambda/io/`; `MarkReader`, `item_tagged.hpp`, `lambda_typed.hpp`, core item-printing declarations, the NamePool/ShapePool allocator registry, allocation-free collection tail rebasing, and Target identity/equality live in `lambda/core/`; Context-coupled side-stack, stack-overflow recovery, native root-frame/rooted/no-GC helpers, heap/GC registries, complete collector (`gc_heap`, object/data zones), procedural runtime/dry-run state, AST emitter, scalar-home number helpers, GC-safe collection growth, runtime Path traversal, and runtime PDF wrappers live in `lambda/runtime/`. `format.cpp` uses the core print header instead of `ast.hpp`; `lambda.hpp` is compatibility-only, re-exporting IO/rt facilities whose providers are in those lower modules. IO has an explicit Pool/Arena collection provider and PDF parser entry point, while `runner` registers the active runtime Path pool provider; neither path falls back to ambient GC allocation. `runtime/heap_api.h` and `runtime/runtime-state.h` replace active ad-hoc heap/root/dry-run declarations without extending frozen C2MIR headers. The resource manager now asks its IO-owned processor hook to release decoded image objects and schedule layout updates; Radiant registers the implementation, so IO never dereferences `DocState`. The old collection implementations in `lambda-data.cpp` are disabled compatibility text only; the monolithic and static manifests compile the owner implementations. | focused Mark build/read/edit + stress-GC gates; core/io source-group probes; `make build-lambda-static`; no C2MIR diff |
| **P1c** | Consolidate genuinely duplicate pool/arena constructors only after P1a–P1b tests establish equivalent ownership semantics. Do not add a core heap mode. | allocation/lifetime focused tests; baselines |
| **P2** | **IMPLEMENTED.** `build_lambda_config.json` defines `lambda-lib`, `lambda-data`, `lambda-rt`, `radiant`, and the static executable. The generator expands each validation target from the shipped source manifest so a boundary DSO cannot become a reduced probe. `lambda-input-full` is now `lambda-data`; the former `lambda-process` subset is `lambda-lib`; `lambda-static` links the module archives in exact dependency order. | `make build-lambda-static`; `lambda-static --help`; core/io DSO checks green |
| **P3** | **IMPLEMENTED.** Validator and its schema front end are rt-owned. CSS counters, symbols, resource attachment/release, and layout-update notifications cross upward only through IO-owned registration hooks. `resource_loaders.*` and `network_integration.*` are Radiant, while IO retains shared download/cache management. The runtime PDF wrapper delegates parsing to the IO entry point with an explicit Pool; static input never needs a runtime context. | IO DSO strict link; static executable link; Lambda baseline focused checks |
| **P4** | **IMPLEMENTED EXCEPT CLASS F.** Active MIR-Direct `pn_emit` and `pn_set_selection` call the lower-owned `runtime/radiant_event_hook`; counters, symbols, resource attachment/release, layout updates, and memory-stage hooks follow the same rule. The active boundary report has zero upward extern declarations. The only remaining rt→Radiant interface is the user-deferred Class-F JS DOM/CSSOM/editing/clipboard/history binding set, ratcheted by the rt boundary image. The frozen C2MIR `lambda.h` declarations and compatibility stubs remain out of scope under SM14. | active boundary baseline; `make check-module-boundary`; no C2MIR diff |
| **P5** | **IMPLEMENTED WITH CLASS-F DEFERMENT.** `make check-module-boundary` builds all five real DSOs. lib/core/io/radiant link strictly; rt's 165 approved Class-F imports are compared to a checked-in baseline and cannot grow. The rule is exposed through the unified lint dispatcher as `module-boundary-link` without slowing the normal fast sweep. Provider ownership is enforced by source ownership and the real source-manifest link images; aggregate compatibility headers are classified at their highest layer and frozen C2MIR is excluded. | `make check-module-boundary`; `make lint ARGS='--rule ^module-boundary-link$'`; no C2MIR diff |
| **P6** | **IMPLEMENTED.** Test builds consume the module archive dependencies from `build_lambda_config.json`; tests that deliberately reach implementation detail remain explicit source/dependency entries rather than implicit monolithic reuse. The two Lambda script binaries are outer-scheduler exclusive because each already owns a nested `test-batch` process pool; this prevents a regression gate from oversubscribing those process pools. | `make build-test`; `make test-lambda-baseline` green |

**Rev 24 P2 checkpoint.** `lambda-input-full` is now the static `lambda-data` target, every test dependency uses that name, and `build-lambda-data` builds it. The former `lambda-process` subset is now the lower static `lambda-lib` archive; `lambda-data` no longer inlines its generic `lib/*.c` sources and test consumers link the concrete lower mixed-language archive. The source-pattern generator now applies correct per-language standards to mixed C/C++ archive patterns. The first static consumer build deliberately exposes the remaining P1 violation: GC-rooted collection growth and heap-backed decimal operations are still pulled from `lambda-data.cpp`/`lambda-decimal.cpp`; they must move to rt rather than be resolved by an upward link.

**Rev 25 P1/P2 checkpoint.** Static input calls now select an explicit Pool/Arena collection provider (`collection_io.cpp`): it never falls back to the active GC heap, preserves arena-tail rebasing, and keeps pooled directory construction explicit. The GC-rooted collection implementation remains the runtime variant; heap-backed Decimal/BigInt result constructors compile only in the runtime variant while input parser constructors remain arena-owned. `make build-test` now links all test executables without an upward runtime dependency from `lambda-data`. The direct formatter call to Radiant's `resolve_symbol` was replaced with the IO-owned registration hook extended to carry an IO-defined emoji/entity kind, so format-only consumers no longer need a Radiant provider. `build-lambda-rt` and `build-radiant` now build the first static archive groups.

**Rev 26 static-executable checkpoint.** `make build-lambda-static` now builds `lambda-lib` → `lambda-data` → `lambda-rt` → `radiant` and links `lambda-static` without dynamic lookup. The generated executable link rule carries the transitive third-party static archives after the module archives, which is required by the macOS linker; it does not force-load those archives. The target also includes its active MIR, NPM, host-object, JS grammar/libuv, and platform-Radiant providers under their owning modules. macOS Objective-C++ Radiant backends compile through Radiant-only wrapper translation units, while the legacy monolithic sweep excludes those wrappers. Both `make build` and `make build-lambda-static` are green; `lambda-static --help` succeeds; and `make test-lambda-baseline` passed 2,105 input tests plus 1,402 Lambda-runtime tests with zero failures. The monolithic build now uses the relocated `runtime/collection_runtime.cpp` algorithms and the explicit IO `array_append` provider; the now-inactive legacy bodies still need deletion as part of the final P1 source cleanup. P5's five enforcing DSOs remain outstanding.

**Rev 27 boundary-enforcement checkpoint.** `make check-module-boundary` now emits all five requested validation DSOs from the shipped source manifests. `lambda-boundary-lib`, `lambda-boundary-core`, `lambda-boundary-io`, and `lambda-boundary-radiant` are fatal-undefined links; `lambda-boundary-rt` is emitted with dynamic lookup solely for the approved Class-F runtime bindings. `utils/check_module_boundary.py` verifies that image has exactly the checked-in 165 deferred imports and rejects any new one. The remaining active Path seam is a registered pool provider owned by `runner`, PDF parsing accepts an explicit IO pool and is wrapped from rt, and network resource release/layout notification is registered by Radiant rather than dereferencing `DocState` from IO. The two Lambda script suites each launch their own nested process batches, so the outer test scheduler now marks them exclusive; `make test-lambda-baseline` passes all 2,105 input tests and all 1,402 Lambda-runtime tests without the prior nested-parallel contention. `make check-module-boundary` and `make lint ARGS='--rule ^module-boundary-link$'` pass; the frozen C2MIR diff remains empty.

P0 is observational. P1a/P1b carried the active header/provider and lifetime risk; moving builder/editor to io removed the former Mark/DOM abstraction risk. P1c still delays optional constructor consolidation until there is a demonstrated semantic duplicate. P2 packages the established groups, P3–P4 clear active upward seams, and P5 locks the boundary images behind them. The only planned migration explicitly deferred by the user is Class F; its current size is a failing ratchet, not an open-ended exception. C2MIR remains frozen throughout.

---

## 16. Open questions

1. **Profile B audit** (§13.1): verify no layout/render/font path hard-requires a live display outside `ui_context` as SM8 relocates files — headless-browser mode must survive the split unchanged. (Replaces the SM10 question, resolved rev 3: Option A.)
2. Counters / `resolve_symbol`: move vs. hook — resolve during P3 once the css_prop split exposes the real shape.
3. Active value-header names and transition: choose the least disruptive new core names and temporary forwarding includes while keeping retired C2MIR files untouched (§9.1/SM14).
4. Does Mark-format input need `parse.c` after the validator moves up (§10)?
5. `network/font_resource_faces.*`: split by the §2.1 rule — the font-file download/cache side stays io; anything holding loaded face objects belongs radiant. Verify which it is at P1.
6. Windows: per-module export macros + DLL config — deferred until a DLL consumer exists (§1 non-goal).
7. Complete the §2.2 runtime-native IO inventory: decide whether `js_fetch.cpp`'s reusable curl worker should call io while its JS promise/Response binding remains rt; keep `js_net`/`js_dns`/`js_tls` rt unless a reusable transport API emerges naturally.
8. Scheduling of §8.1 class F (rt→radiant binding TUs) — deliberately deferred until rt and the layers below are settled; until then the tier-1 rule carries rt→radiant as a ratcheted report-only baseline.
