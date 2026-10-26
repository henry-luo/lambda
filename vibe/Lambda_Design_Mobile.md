# Lambda and Radiant on iOS — Design Proposal

**Date:** 2026-10-26 (rev 6: host paths (MB13) done; the rest of P1.2 awaits Xcode)
**Status:** **PROPOSAL — not ratified.** Nothing here revises an `S#`/`D#` ruling yet. §10 lists the spec changes this design would need; each needs a user ruling before it lands.
**User direction (2026-10-25):** Phase 1 is a **developer-build MVP**: several simple demos running on the user's own iPhone, installed from Xcode. App Store distribution is out of phase 1. External Jube modules are deferred to a later phase. Phase 1 is specified in **§1** and overrides the full-port sections where they differ.
**Design authority:** `doc/Lambda_Formal_Design.md` — D1.1, D1.3v3, D7.1.1, D7.1.4v2 (headless profile C), D7.1.6 (`lambda-cli`), **D7.1.7v6 (`lambda-wasm`, the no-JIT precedent)**, D8.1.1v18 (T0/AUTO tiering), D8.1.3v42 (LambdaJS AST interpreter), D8.5 (MIR cache), D4.5 (Radiant seam).
**Working design:** `vibe/Lambda_Design_Ast_Interpreter.md` (AI1–AI25), `vibe/Lambda_Design_Static_Modules.md` (wasm profile), `vibe/Lambda_Design_JS_Interpreter.md`, `vibe/Memory_Context.md` (reclaim ladder), `doc/dev/Lambda_WASM_Build.md`, `doc/dev/radiant/RAD_00_Overview.md`.
**Scope:** porting the Lambda runtime, LambdaJS and Radiant to iOS and iPadOS (device and simulator, arm64). Android is out of scope, but §4's platform seam is meant to serve it too.
**Ledger:** this doc opens the **`MB#`** series (mobile). Open questions are **`MB-O#`**.

---

## 0. Summary

iOS lets an App Store app execute only code that Apple signed when it installed the app. Writable-and-executable memory, `MAP_JIT` and `dlopen` of downloaded code are all unavailable. Lambda's native tier, MIR Direct, generates machine code at run time, so it cannot ship on iOS. The port therefore runs **T0 only**: the boxed AST interpreter for Lambda (`lambda/runtime/interp.cpp`) and for LambdaJS (`lambda/js/js_interp.cpp`), with MIR compiled out of the binary (**MB1**).

That is less radical than it sounds, because most of the hard part already exists:

- `LAMBDA_NO_MIR` already compiles MIR out of the Lambda engine. In that build T0 is the only tier and promotion is impossible (`interp.cpp:57-78`).
- `lambda-wasm` (D7.1.7v6) ships this exact constraint in production: no MIR, no promotion, no MIR-interpreter fallback.
- LambdaJS has a complete AST interpreter. Its impl record reports Test262 40,261/40,261 under `ast`.

Wasm strips out the OS. iOS keeps most of it: threads, fibers, libuv, files, the network, images, Radiant and JS. The work splits into three parts:

1. **Execution (§3).** Make JS and the JS→Lambda bridge build and run without MIR. Close or explicitly refuse the constructs T0 still rejects. Decide what to do about T0 being the performance ceiling.
2. **Radiant platform (§4).** Replace GLFW, the legacy-GL present path and CVDisplayLink with a UIKit shell. Add the input model desktop never needed: touch, the soft keyboard, safe areas and the app lifecycle.
3. **OS services, build and distribution (§5–§7).** Cover the sandboxed filesystem, no process spawning, statically linked Jube modules, TLS trust, cross-building every dependency, and App Store rules. One App Store rule (2.5.6, on web browsers) decides how Radiant may be positioned (§7.2).

Most of the execution work can be done and qualified **on macOS**, before touching an iOS SDK, by building a desktop no-MIR profile (**MB2**, milestone M0).

**Phase 1 (§1)** narrows all of this to a developer-build MVP. Five simple pure-Lambda demos already pass their UI scripts under strict T0 today. With JS, network, GPU content and external Jube cut, phase 1 needs no new interpreter work. Its real work is a no-MIR + no-JS build, the platform seam, the iOS cross-build and a minimal UIKit shell.

## 1. Phase 1 — developer-build MVP

### 1.1 Goal and demo set (MB20)

**Goal:** a handful of the simple Lambda demos run, playable by touch, on the user's iPhone, from a Release-configuration build that Xcode installs. No App Store, TestFlight or review constraints apply.

**MB20 — the phase-1 demo set.** These demos are pure Lambda (no `.js`), need no network, and use no GPU content:

- **Core:** `tetris` (has on-screen buttons and Auto Play), `sentence-game` (tap and drag tiles), `greetings` (taps, text, photos, fonts).
- **Stretch:** `draw` (pointer gestures) and `superlambda` (Auto Play; audio for its score).

Excluded from phase 1:

- `scene3d`, `cityscape`, `doom`: GPU content (§4.4) or heavy compute.
- `finder`: uses JS.
- `mail`, `readability`, `archive`: network.
- `word`, `note`, `slides`: heavy text editing.
- `photo`, `photos`: heavy image kernels.
- `chart`: an HTML collection, not a Lambda entry.

**Evidence that the execution side is ready (measured 2026-10-25).** Runs used the current desktop `lambda.exe` (a debug build, so the timings are not performance numbers, per AGENTS rule 10), headless, with `LAMBDA_EXEC_BACKEND=interp`, which refuses any MIR fallback:

| Demo | Probe | Result under strict T0 |
|---|---|---|
| tetris | `test/ui/tetris_autoplay.json` (21 events) | PASS, 12/12 assertions. `interp: executed=2 fallback=0 excluded=0` |
| superlambda | `test/ui/superlambda_autoplay.json` (24 events) | PASS, 12/12 |
| greetings | `test/ui/greetings/studio.json` (110 events) | PASS, 34/34 |
| draw | `test/ui/draw/smoke.json`, `gestures.json` | PASS, 3/3 and 19/19 |
| sentence-game | boot + 1.5 s idle + render | PASS, no refusals |
| music, readability, note, photo, slides | boot + 1.5 s idle + render | PASS, no refusals (outside the phase-1 set; recorded for phase 2) |

Every phase-1 demo already runs entirely in T0. Phase 1 therefore needs **no new interpreter work** (MB3 moves to phase 2). The remaining risk is the build and the platform layer.

### 1.2 Scope cuts that make phase 1 small

- **JS runtime stays; only its MIR code generation goes (MB22).** The first P1.0 build tried `LAMBDA_NO_JS` (MB19, now superseded, see Appendix S). The link showed why that cannot work: `lambda/dom` builds DOM objects (events, arrays, prototypes, typed arrays, private fields, error lanes) on the LambdaJS object-model kernels, so Lambda's own `import dom` needs the JS runtime even when no page script runs. Stubbing those ~146 kernels would break the Lambda demos. So the phase-1 build keeps the LambdaJS runtime and its AST interpreter and excludes only MIR, which pulls MB4 (§3.4) into phase 1. Page scripts are still not a phase-1 goal.
- **No external Jube modules.** This is the user's deferral. The **built-in static modules stay**: `import radiant` resolves to the static `radiant` Jube module (`radiant_jube_register_static`, `jube_registry.cpp:2249`; there is no `lmd/package/radiant`), and `dom` registers the same way. Only the `modules/*` set is out. The Jube registry itself must stay: `lambda/dom` resolves interface types through it (`jube_type_has_interface`, `jube_iface_type_slot`). The dynamic loader can stay compiled as well, because `dlopen` exists on iOS and only refuses unsigned code, so a load failure is the ordinary explicit "module unavailable" path. Phase 1 therefore needs no Jube flag at all.
- **No network.** The MB20 demos read only bundled files. If the wasm profile's `LAMBDA_NO_NETWORK` composes with Radiant, phase 1 drops curl, nghttp2 and mbedTLS from the iOS dependency build. If it does not compose cheaply, curl is linked and the TLS hook (§5.4, MB18) lands in phase 1 instead.
- **No GPU content.** WebGL, canvas-GL and scene3d are excluded with diagnostics (§4.4 option 1).
- **No App Store concerns.** §7 does not apply to phase 1. C2 still matters: a build launched from the home screen cannot JIT even though one under the Xcode debugger can. MB1's compile-time exclusion keeps both launches identical.

### 1.3 What phase 1 must build

1. **The `lambda-nomir` desktop twin** (MB2, MB22): exactly `lambda.exe` minus MIR. `make lambda-nomir` builds it from the `lambda-nomir` variant in `build_lambda_config.json`: `LAMBDA_NO_MIR`, no `libmir`, and the four MIR translation units excluded (`transpile-mir.cpp`, `runtime/mir.c`, `mir_dump.cpp`, `js/mvp-lmd/mvp_lmd_mir.cpp`). Progress is recorded in §1.7.
2. **The platform seam** (MB7) and **tick inversion** (MB8), done on desktop first with no behavior change.
3. **The iOS cross-build** (MB16) of only the libraries the phase-1 closure links: mpdecimal, utf8proc, fribidi, libuv, ThorVG (+ patches), Brotli, and the in-tree image codecs and miniaudio. zlib, bzip2 and iconv come from the SDK.
4. **A minimal UIKit shell:**
   - present through IOSurface or CAMetalLayer (MB9);
   - CADisplayLink ticks;
   - touch → pointer events (MB11);
   - safe-area insets;
   - background/foreground handling;
   - text input stage 1, `UIKeyInput` (MB12), for greetings' text;
   - CoreText font discovery (MB10). Greetings names macOS fonts: Noteworthy, Snell Roundhand, Marker Felt, Chalkboard SE, Didot, Futura and others ship on iOS. Luminari, Apple Chancery, Savoye LET and Brush Script MT do not, so their CSS fallbacks must resolve.
5. **Host paths** (MB13): the bundle `lmd/` subset plus the demo folders read-only, and caches/logs under `Library/Caches`.
6. **A demo picker.** `test/demo/index.ls` starts each demo as a separate process, which C4 forbids. Phase 1 uses a native list that opens the chosen demo in the Radiant view, and closing it tears the document down in-process. Desktop `view` runs one document per process today, so any teardown leak this exposes is a real defect to fix, not to paper over by relaunching.

### 1.4 The layout viewport on a phone (MB21)

The demos are designed for a desktop window (the UI tests use 900×1000 CSS px). An iPhone is roughly 390–430 pt wide. **MB21 — a document with no `viewport` meta lays out at a desktop-width layout viewport and is scaled to fit the screen, with pinch-zoom.** This is the legacy-viewport behavior of mobile browsers (980 px wide in Safari), not a special case. Phase 1 uses 1024 CSS px as the default width. A demo that adds `<meta name="viewport" content="width=device-width">` gets device-width layout instead. Touch coordinates map back through the scale before hit-testing. Landscape and iPad come for free.

### 1.5 Device setup (dev build)

- **Signing.** Xcode signs the app with the user's Apple ID team. A free Personal Team provisioning profile expires after **7 days**, and the app must then be reinstalled from Xcode. A paid Apple Developer Program membership gives one-year profiles. Either works for phase 1.
- **Developer Mode** must be enabled on the iPhone (Settings → Privacy & Security), which iOS 16+ requires for any locally signed build.
- **Release configuration** is used for on-device evaluation, so T0 speed is judged on optimized code. The Debug configuration is for stepping only.
- **No special entitlements** are needed. The extended-virtual-addressing entitlement (§3.7) is needed only if the 128 MB interpreter stack reservation fails on the device. Paid teams can enable it; Personal Teams may not be able to *(verify)*.

### 1.6 Phase-1 milestones

| M | Deliverable | Acceptance |
|---|---|---|
| P1.0 | `lambda-nomir` desktop profile (MB2, MB22) with GLFW Radiant | It links. The §1.1 UI scripts pass on it. `test-lambda-baseline` under T0 recorded (failures listed, not fixed, unless a phase-1 demo hits them). |
| P1.1 | Radiant platform seam + tick loop on desktop (MB7, MB8) | `test-radiant-baseline` 100%. The §1.1 UI scripts still pass. |
| P1.2 | iOS cross-build + `Lambda.xcframework` + host paths (MB13, MB16) | `xcrun simctl spawn` runs a Lambda script and `test-lambda-baseline` in the simulator |
| P1.3 | UIKit shell: present, display-link tick, touch, MB21 viewport, lifecycle | tetris plays by touch in the simulator. Background and return keeps the game. |
| P1.4 | Fonts, `UIKeyInput`, safe area, demo picker | sentence-game and greetings run in the simulator. Switching demos 10× shows no growth in resident memory. |
| P1.5 | Release dev build on the user's iPhone | The three core demos play smoothly on the device. Peak memory recorded against jetsam. The stretch demos are attempted, with their status recorded. |

P1.0 and P1.1 need no iOS SDK.

### 1.7 P1.0 result (2026-10-25 – 2026-10-26)

**Lambda engine: links without MIR.** `LAMBDA_NO_MIR` had only ever been compiled by `lambda-wasm`, which also drops the resource cache, tasks and file IO. Keeping those exposed code that had drifted into MIR-only regions. Fixes, each at the boundary and not by stubbing:

- `interp_poll_interrupt` (D8.1.1v17) was inside the `#ifndef LAMBDA_NO_MIR` region of `interp.cpp`, although T0 polls it in every profile. Moved out.
- `lambda_script_cache_request` (the backend-neutral source/AST cache key, D8.5.1v4) and `lambda_finalize_ast_template_for_execution` (T0 activation of a cached AST template) were MIR-guarded in `runner.cpp`. Now only the AUTO fallback tail of the latter is guarded; a T0-only build refuses there like explicit `interp` (D8.1.1v15).
- **The script run entry points lived in `transpile-mir.cpp`.** `run_script_mir`, its large-stack worker, `run_lambda_package_module` and the document-transform table and entries now sit in a new `lambda/runtime/script_run.cpp`; only their whole-module MIR branch is guarded. The wasm manifest does not list the new file, so that profile compiles exactly what it did before.
- Two copies of the post-order import-cone walk and collector (`transpile-mir.cpp`, and `interp.cpp`, whose comment said it existed only because the original was static to the MIR file) became one shared `script_import_cone_postorder` / `collect_import_cone` in `runner.cpp` (AGENTS rule 13). Four hand-copied declarations of `prepare_context_module_state` became one in `transpiler.hpp`, beside the other `mir.c` exports.
- MIR-only call sites are guarded with an explicit diagnostic where a user can reach them:
  - `--mir-interp` logs "excluded from this build";
  - JS importing a Lambda module throws "excluded from this build" (MB5);
  - Lambda namespace registration for JS, satellite-image teardown, `execute_script_and_create_output` and the batch MIR error handler are compiled out with MIR.

**LambdaJS: `mvp_lmd_mir.cpp` split (MB23).** That file was not only the MVP code generator. It also held MIR-free JS runtime used by the AST interpreter and the DOM: private internal names (RegExp iterators, URL records, PerformanceObserver), template-object ops, realm-library ambient state and its activation hook, `MvpLmdRealmScope`, and program/callable accessors. The split, by user ruling 2026-10-26:

- `mvp-lmd/mvp_lmd_internal.h` holds the types both halves share: the include block, the `LmdKind`/`LmdIntrinsic` enums, the compiler-plan structs `MvpLmdProgram` embeds, `MvpLmdCode`/`Program`/`Execution`/`Realm`, and the intrinsic tables. MIR names appear there only as field types; a no-MIR build includes the in-tree MIR headers for those types and never links `libmir`.
- `mvp-lmd/mvp_lmd_program.cpp` (≈450 lines) holds the MIR-free runtime: unit creation, reset, program/realm accessors, template-object ops, `object_create`, the realm-library thread-locals and ambient hook, internal names, `MvpLmdRealmScope`, function source/name, the realm-intrinsic predicates, and the dynamic-global entries.
- `mvp_lmd_function_prepare` split along its own ABI gate. The shared half returns interpreter functions unchanged and rejects non-MVP ABIs, as before. MVP-callable materialization moved to `mvp_lmd_function_materialize` in the generator file.
- Four entries that compile or plan through the generator stay in `mvp_lmd_mir.cpp`: `create_realm`, `prepare_module_realm`, `dynamic_import` and `dynamic_class`. They are reachable only from an MVP program, which a no-MIR build never creates (MVP library state is owned by a program, and only MIR instantiation creates one). In that build one guarded block in `mvp_lmd_program.cpp` defines them as explicit "MIR code generation is excluded" failures.
- The JS selector forces `ast` and disables AUTO at compile time under `LAMBDA_NO_MIR` (`js_interp.hpp`, MB4). The MVP fallbacks and preamble images in `js_execution.cpp` and the MVP batch in `main.cpp` fail explicitly. The P2 promotion region of `js_interp.cpp` (admission scan, satellites, loop handoff) compiles to "never promotes" definitions, the JS counterpart of `INTERP_DISABLED_MIR_HOOK`.

**Result.** `make lambda-nomir` links `lambda-nomir.exe` (27 MB debug) with **zero MIR symbols and no `pthread_jit_write_protect_np` reference**; `lambda.exe` has 143 MIR symbols. Every phase-1 demo script passes on it, headless, with `interp: fallback=0 excluded=0`:

| Demo | Script | Result | Peak RSS |
|---|---|---|---|
| tetris | `test/ui/tetris_autoplay.json` | 12/12 | 148 MB |
| superlambda | `test/ui/superlambda_autoplay.json` | 12/12 | 408 MB |
| greetings | `test/ui/greetings/studio.json` | 34/34 | 206 MB |
| draw | `test/ui/draw/smoke.json`, `gestures.json` | 3/3, 19/19 | 113 MB, 139 MB |
| sentence-game | boot + 1.5 s + render | pass | 78 MB |

Super Lambda's autoplay peak is the first input to the iOS memory budget (MB15).

**Build gotcha found on the way.** Make does not track preprocessor defines. Changing a variant's `defines` in `build_lambda_config.json` leaves objects in `build/obj/<variant>` compiled under the old ones, and the link mixes them silently. Here `name_pool.cpp` kept its JS name catalog compiled out from an early `LAMBDA_NO_JS` attempt, so JS symbol keys failed to resolve at run time ("invalid virtual accessor descriptor") in a binary that linked cleanly. After editing a variant's defines, delete `build/obj/<variant>` before building.

**Normal build unaffected.** `make build` passes. `make test-lambda-baseline` passes 8231/8232. The one miss, `JsFileTest.Run/dom_media_playback`, is the tracked `test/media` link not resolving inside a worktree; it passes with the link retargeted. A smoke run of the 40 import-using `test/lambda` goldens on `interp`, `auto` and `jit` gives 105/117 matches. The 12 misses are environmental (`binary_js_bridge`: no `node-core` dylib in the worktree) or the known T0 image-package gap (`lmd/package/image.ls` `INDEX_EXPR`; `image_paint`, `image_stroke_step` and `image_raster_equality` are missing from `interp_excluded.txt`). The `lambda-wasm` profile was not rebuilt (no Emscripten on this machine). After the JS split, `make test-lambda-baseline` again passes 8231/8232 with the same environmental miss, including the 847 JS MVP tests. The full Test262 corpus was not rerun.

**T0 parity.** All 1694 `test/lambda` scripts with a golden were run on `lambda-nomir.exe` and on `lambda.exe` with `LAMBDA_EXEC_BACKEND=interp` (`temp/ios_mvp/parity.py`). **Every output is identical between the two (0 differences)**, so removing MIR changes no T0 behavior. Both binaries match the golden on the same 1001 scripts. The rest are this simple sweep's limits, not defects of either binary: procedural scripts need `run` mode, `.mac.txt` overrides are not applied, and the known T0 refusals (§3.3) apply.

**Next.** P1.1 (the Radiant platform seam and tick loop on desktop) needs no iOS SDK. Phase 2 continues with the full-port milestones in §8, starting from the JS→Lambda T0 export path (MB5) and the T0 gap closure (MB3).


### 1.8 P1.1 result (2026-10-26)

**The platform seam (MB7) is `radiant/platform.hpp`.** Radiant sources name only `radiant_platform_*` functions and the opaque `RadiantWindow`/`RadiantCursor` handles. The groups are:

- library lifetime, event pump and clock;
- windows (create, show, focus, title, icon, sizes, content scale, focus/hover, native handle);
- the `RadiantFullscreenBackend` operations;
- host attachment, with input gated separately so a replayed event file owns the pointer;
- pointer and cursors;
- the plain-text clipboard;
- presentation of the CPU surface.

The backend reports translated `RdtEvent`s and window notifications through seven `radiant_host_*` functions defined by `window.cpp`. Host policy stays there: history keys, Escape-to-close, the close guard, click series, redraw.

**One backend per build, chosen at link time.** No vtable is needed, because a desktop binary in headless mode is still the desktop backend: it opens a hidden window for webview/native-graphics tests.

- `platform_glfw.cpp` is the desktop backend: GLFW windows and callbacks, the legacy-GL texture-blit present path (moved verbatim from `repaint_window`), GLFW cursors and clipboard, and the GLFW fullscreen read/apply.
- `platform_null.cpp` replaces `headless.cpp` for `LAMBDA_NO_GUI`. It has the same "uninitialized library" contract, now behind Radiant's own API instead of stub definitions of GLFW and GL symbols.
- A UIKit backend implements the same header in P1.3.

**What changed outside the backend.**

- **Key codes.** They are Radiant's own: `RdtKeyCode` (now the full key set, GLFW numbering) and `RdtMouseButton`. `static_assert`s in the GLFW backend keep translation the identity. `RDT_KEY_UNKNOWN = 0` stays Radiant's "no key" value; GLFW's `-1` is `RDT_KEY_UNRECOGNIZED`. `KeyEvent` gained `repeat`, so press-only host policy (Escape, history keys) no longer needs GLFW's action codes.
- **Headers and state.** `view.hpp` no longer includes OpenGL or GLFW. `UiContext::window` and `MouseState::sys_cursor` use the seam's handles.
- **Desktop natives.** The macOS/Windows IME shims, the child webviews and the app icon reach the native window through `radiant_ui_get_native_window` / `radiant_platform_native_window`.
- **Clipboard.** `state_store.cpp`'s four copies of the non-Apple OS clipboard mirror became one helper pair on the seam.
- **GLFW users left.** Outside `platform_glfw.cpp` there are three: `gl_core.cpp` (the self-contained desktop GPU-content backend, already off in no-GUI builds; MB-O2), the never-enabled `RADIANT_CLIPBOARD_GLFW` block in `clipboard.cpp`, and one scene3d unit test that observes `gl_core`'s context.

**The tick (MB8).** The windowed viewer's loop body is now `radiant_window_tick(RadiantWindowLoop*)`, declared in `radiant.hpp`. Each turn it pumps platform events, JS timers and animation frames, replays simulated input, ticks animations, renders when dirty and reclaims retired storage, then returns how long the host may wait. The desktop loop is now "tick, then wait on the platform". A display-link host calls the same function. Splitting the viewer's setup and teardown into open/close entries an iOS shell can call is left to P1.3, where its first consumer arrives.

**Pitfalls met.**

- `glfw3native.h` pulls the Carbon/Win32 SDK headers into a `view.hpp` unit (Carbon's `Rect` collides with Radiant's). The backend declares GLFW's three native-access functions by their pointer-sized ABI instead.
- `gl_core.cpp` had used `GL_ALIASED_POINT_SIZE_RANGE` from the legacy `gl.h` that `view.hpp` used to include; it now defines the constant itself.
- The old `headless.cpp` also stubbed two macOS native companions (`radiant_ime_mac_attach`, `radiant_app_icon_mac_set`); they now sit in the null backend's native-companion block.

**Verification.**

| Check | Result |
|---|---|
| `make build` (desktop, GLFW backend) | links |
| `make build-headless` (null backend) | links; **zero GLFW and OpenGL symbols** in `lambda-headless.exe`. It renders `tetris.ls` to a PNG byte-identical to `lambda.exe`'s. |
| `make lambda-nomir` | links |
| Phase-1 demo scripts, headless | all pass, as before |
| Phase-1 demo scripts in a real window (event simulation drives the windowed loop) | identical to the pre-change baseline taken in the same worktree: Greetings 34/34, Tetris and Super Lambda exit 0, Draw gestures 17/19. The same two Draw assertions fail before and after: a range value 460 for 450 and a zoom label 75% for 83%, apparently real-window sizing differences. Recorded, not part of P1.1. |
| `make test-radiant-baseline` | 6177 passed, 1 failed: `CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline` (CSS live bytes 383,690 against a 373,940 budget). An A/B in this worktree, with the P1 changes set aside and the test rebuilt at the base commit (origin/master 5658e5d55), fails with **the same byte counts**. It is pre-existing on origin/master: the baseline file was last updated 2026-10-22, before later CSS-touching commits. |
| `make lint ARGS='--rule ^no-int-cast-radiant$'` | no violations |

Not verified: the Linux and Windows builds. The X11/Win32 native-window branches and the webview/IME edits for those platforms are written but uncompiled here.


### 1.9 P1.2, SDK-independent part: host paths (2026-10-26)

P1.2's cross-build needs Xcode, which this machine does not have yet. The host-paths half of P1.2 (MB13) needs no SDK and is done.

**`lib/host_paths.h`** holds five base directories:

| Kind | Default (desktop, unchanged) | Override | Holds |
|---|---|---|---|
| `HOST_PATH_HOME` | `./lmd` | `LAMBDA_HOME` | the read-only package tree |
| `HOST_PATH_CACHE` | `./temp` | `LAMBDA_CACHE_DIR` | `cache/` (HTTP and Radiant state), `radiant_cache/` |
| `HOST_PATH_TEMP` | `./temp` | `LAMBDA_TEMP_DIR` | `io` temp files and directories, downloads, developer dumps (MIR dump, profiles, event/state logs, `--mem-dump`) |
| `HOST_PATH_LOG` | `.` | `LAMBDA_LOG_DIR` | `log.conf` and its relative outputs (`log.txt`) |
| `HOST_PATH_DOCUMENTS` | `.` | `LAMBDA_DOCUMENTS_DIR` | user documents (first consumer: the iOS demo picker, P1.4) |

The API has five calls:

- `host_paths_set` is for an embedding host, called before start-up;
- `host_path` returns a base;
- `host_path_join` returns a caller-owned `<base>/<rel>`;
- `host_path_dir` returns a stable `<base>/<rel>` for long-lived directory strings;
- `host_path_ensure` creates a base.

What changed around it:

- **Package home.** It replaces `g_lambda_home`/`lambda_home_path` outright: `lambda_home_init` now just applies the environment overrides.
- **Logging.** `lib/log.c` gained `log_set_output_dir`, which resolves relative config outputs against the log directory, and `log_reload_config_in_dir`. The latter replaces two copied "empty `log.txt`, parse `log.conf`" blocks in Radiant. `log.c` itself takes no new dependency: the 19 test targets that hand-list it are untouched.
- **Call sites.** About 35 sites moved: cache roots (14), temp files and developer dumps, and the log configuration.
- **Left alone on purpose:**
  - `sys.home`/`sys.temp`, which report the OS directories (iOS maps both into the sandbox);
  - the `temp:` path scheme and other `"temp"` names;
  - the event-file harness's rule that fixtures write only under the repo's `temp/`;
  - `test_output/` dumps;
  - the npm installer's project-local `.lambda/` (deferred with Jube);
  - the unbuilt `font_config.c`.
- **Wasm.** The manifest gained `lib/host_paths.c`, with environment lookups and directory creation guarded by `LAMBDA_NO_AMBIENT_PROVIDERS` / `LAMBDA_NO_FILE_IO`, following the existing convention. It was not rebuilt; there is no Emscripten here.

**Pitfall found by the gates: process-lifetime strings must not use the tracked heap.** The first version kept the bases and memoized `host_path_dir` strings in `mem_*` allocations. Two things went wrong:

- At exit they showed up as live bytes, failing every `[MEMTRACK_LIVE] bytes=0` check (the GC-stress sweep and the Radiant view tests).
- Freeing them in a teardown hook crashed (SIGTRAP in `free`). `memtrack` keys tracking on a thread-local flag, so a string first allocated on an untracked network thread is a raw `malloc` without a header. Freed on the tracked main thread, `mem_free` reads a header that isn't there.

The fix is static storage for the bases and the memo table: there is no heap, no teardown, and it is the same whichever thread asks first. Only `host_path_join` allocates, and its caller frees on the same thread.

**Verification.**

- **From an empty foreign working directory with all five bases redirected:**
  - three package-importing goldens match;
  - the Tetris UI script passes 12/12;
  - **nothing is written to the working directory**;
  - `log.txt`, the MIR dump, the phase profile, the memory snapshot and Radiant's state database land in the redirected directories.
- **The pre-change binary, same setup:** it writes `temp/` (MIR dump, cache database, profile, snapshot) into the working directory and never finds the log configuration.
- **Gates.** `make test-lambda-baseline` passes 8232/8232. `make test-radiant-baseline` passes 6177; its one failure is the pre-existing `CssCascadeMemory` budget, with byte counts identical to the base commit.

**Next.** The rest of P1.2 needs Xcode and an iOS simulator runtime:

- cross-built dependencies;
- the iOS platform block and `Lambda.xcframework`;
- `simctl spawn` of the Lambda baseline.

## 2. iOS platform constraints

These facts drive every decision below. Items marked *(verify)* must be confirmed on a device before the dependent milestone.

| # | Constraint | Consequence for Lambda/Radiant |
|---|---|---|
| C1 | **No run-time code generation.** App Store apps lack the `dynamic-codesigning` entitlement. `MAP_JIT` pages cannot become executable. The one exception is BrowserEngineKit, which is EU-only, browser-only and needs its own entitlement. | MIR Direct, MIR-interp (§3.6) and any thunk generation are out. |
| C2 | **A debugger hides C1.** Under Xcode with `get-task-allow`, JIT can appear to work. TestFlight and App Store builds then crash. | A no-JIT build is enforced at **compile time** (`LAMBDA_NO_MIR`), never at run time (MB1). |
| C3 | **No `dlopen` of code outside the signed bundle.** Embedded, signed frameworks are allowed. | Dynamic Jube modules (`.dylib` under `modules/`) must be linked statically (§5.3). |
| C4 | **No `fork`/`posix_spawn`.** The sandbox denies child processes. | `cmd` (`sys_func_registry.c:1257`), `lib/shell.c` and Node `child_process` must fail explicitly. |
| C5 | **Sandboxed filesystem.** The bundle is read-only. Writable space is `Documents/`, `Library/` (`Caches/`, `Application Support/`) and `tmp/`. The cwd is meaningless. | `./lmd`, `./temp`, `./log.txt` and `./temp/cache` all become explicit base directories (§5.1). |
| C6 | **No swap, jetsam limits, graded memory warnings.** | Memory budgets and the reclaim ladder (`vibe/Memory_Context.md` stage 2) become load-bearing (§5.6). |
| C7 | **The OS owns the run loop and the lifecycle.** UIKit's `UIApplicationMain` owns the main thread. Apps are suspended in the background and must stop GPU work there. | Radiant's blocking `while(!glfwWindowShouldClose)` loop must invert into per-frame ticks (§4.2). |
| C8 | **Desktop OpenGL does not exist. OpenGL ES 3.0 is deprecated (since iOS 12) but present.** Metal is the supported GPU API. | The present path and WebGL/canvas/scene3d need new backends (§4.3, §4.4). |
| C9 | **Touch-first, soft keyboard, safe areas, rotation, iPad multitasking.** | New input and viewport model (§4.6–§4.8). |
| C10 | **Small default thread stacks** (secondary threads 512 KB) and a limited virtual address space *(verify current limits)*. | The 128 MB interpreter worker stack and the 2 MB activation stacks need budgeting (§3.7). |
| C11 | **App Store Guideline 2.5.2:** an app may not download code that changes its features. **Guideline 2.5.6:** apps that browse the web must use WebKit (outside the EU). | This decides what the app may run and how Radiant is positioned (§7). |

## 3. Execution: T0 only

### 3.1 The profile (MB1, MB2)

**MB1 — the iOS build is a `LAMBDA_NO_MIR` profile.** `transpile-mir.cpp`, `runtime/mir.c`, `libmir` and every MIR-only LambdaJS translation unit are left out of the source manifest. `lambda_tier_set` refuses any tier but `INTERP` (`interp.cpp:70-78`). Like D7.1.7v6: no promotion workers, no MIR caches, no MIR-interpreter fallback. Unlike wasm, the profile **keeps** tasks and fibers, libuv, threads, file and network IO, image processing, LambdaJS/TS, the DOM and Radiant. It is a sibling of `lambda-wasm`, not a subset of it.

**MB2 — a desktop twin, `lambda-nomir`, is built first.** It is the same `LAMBDA_NO_MIR` manifest built for macOS with the ordinary GLFW Radiant. It runs every Lambda, JS and Radiant gate under T0 on a developer machine, with no iOS SDK, simulator or signing. Every execution gap in §3.2–§3.5 is found and closed here. The iOS build then differs from it only in the platform layer (§4) and services (§5). This also gives the project a permanent "does it still work without MIR" CI gate, which `lambda-wasm` cannot give for JS or Radiant.

### 3.2 What already works

- **Tier selection.** `LAMBDA_NO_MIR` defaults the tier to `INTERP` (`interp.cpp:57-61`). Under explicit `interp`, an unsupported script fails with E501 before execution, and whole-module MIR fallback is not allowed (D8.1.1v15; `runner.cpp:1548-1564`).
- **MIR guards across the engine.** `interp.cpp` (`INTERP_DISABLED_MIR_HOOK`), `runner.cpp`, `transpiler.hpp:90,250`, `module_registry.cpp:396`, `sys_func_registry.c:1500` and `lambda-mem.cpp:453`. Built and tested by `make lambda-wasm`.
- **REPL.** It runs through `interp_repl_session_eval` (`runner.cpp:2715`). `main-repl.cpp` makes no MIR calls.
- **Async.** T0 runs suspending definitions on their task activations (D8.1.1v16, AI24v2).
- **`that` predicates and const folding.** They run in T0's `EvalMode::PREDICATE`/`CONST` (AI P3), so `validate` needs no JIT.
- **Dynamic JS source** (`eval`, `Function`, string timers). It is always interpreted, whatever the tier (D8.1.3v20).

### 3.3 Lambda constructs T0 still refuses

Under AUTO these go to MIR. Under MB1 they become hard E501 errors. The support scan is `interp_kind_supported` (`interp_plan.cpp:141-245`) plus `interp_scan_visit` (`interp_plan.cpp:465-829`). Today it rejects:

- generator functions (`interp_plan.cpp:621-627`);
- named arguments on dynamic calls or to JS imports (500-513, 566-585);
- `var` parameters passed through dynamic or non-identifier arguments (586-617);
- object-method calls without a direct root receiver (535-546);
- several `a[i] = v` / `a.f = v` shapes: imported roots, N-D row writes, typed non-array/map roots (629-724);
- N-D indexing with non-literal coordinates (754-764);
- system functions that are variadic (except `print`/`select`), take more than 5 arguments or non-`Item` arguments, or use sized-int math lanes (771-828);
- namespace/default imports, non-JS cross-language (Jube) imports, and dependencies with no runnable tier (289-315, 727-740).

`test/lambda/interp_excluded.txt` lists 24 corpus scripts in this class, mostly image `INDEX_EXPR` and COW `INDEX_ASSIGN`/`MEMBER_ASSIGN`.

**MB3 — on iOS, a refused construct is a defect to close in T0, not a feature to drop.** Wasm can live with a refusal list because it is an evaluation sandbox. An iOS app that runs `test/demo/*` cannot. The work is ranked by what the shipped packages (`lmd/package/*`) and the target demos actually hit, which M0 measures. Closing a gap follows the existing T0 rule: simplest design, fewest lines, reuse the MIR lowering's helpers rather than writing parallel ones (AI3; AGENTS rule 13). Sized-int lanes and N-D indexing route through the existing boxed runtime helpers that the MIR boxed lane already calls (SI3/D3.3.1: erasing inferred types and running boxed must give identical results).

**Doc drift to fix along the way:** the §4.9 table of `Lambda_Design_Ast_Interpreter.md` still says modules with views stay eager-MIR. The code admits views that have a body (`interp_plan.cpp:220-225`).

### 3.4 LambdaJS without MIR

The interpreter exists (`js_interp.cpp`, 8,305 lines). Its support scan (`js_interp_node_is_supported`, `js_interp.cpp:5847-5940`) admits classes, async/await, generators, destructuring, try, modules and regex. **But LambdaJS cannot be built without MIR today:**

- There are no `LAMBDA_NO_MIR` guards anywhere under `lambda/js/`.
- `lambda/js/mvp-lmd/mvp_lmd_mir.cpp` (15,121 lines) and the MIR context and `MIR_finish` in `js_execution.cpp:42,763-809` are compiled unconditionally.
- Under AUTO, unsupported units fall back to whole-module MIR (`js_execution.cpp:667-692`).
- Promotion compiles through `mvp_lmd_compile_function_satellite` / `_loop_satellite` (`js_interp.cpp:6477,6774`).
- 17 tests are pinned to MIR (`test/js/mir_list.txt`).

**MB4 — LambdaJS gets the same MIR partition Lambda has.** The JS front end, AST, interpreter, runtime kernels and DOM bindings are MIR-free. The MVP lowering, satellite compiler and whole-module MIR path sit behind `LAMBDA_NO_MIR`. In that build `JS_EXEC_BACKEND` is forced to `ast`. An unsupported unit fails with the same explicit diagnostic as Lambda's E501 instead of falling back. The partition follows D1.3v3: the host substrate is shared, and the MIR lifecycle is one removable component of it.

The 17 MIR-pinned tests are classified: either they test MIR itself (excluded from the no-MIR gate) or they expose an interpreter gap (fixed, per AGENTS rule 18). Test262 runs under `ast` in `lambda-nomir` as part of M1.

**Doc drift:** `vibe/Lambda_Design_JS_Interpreter.md:41` still says "Unset remains MIR". D8.1.3v19 made unset select AUTO.

### 3.5 The JS→Lambda bridge

A JS module that imports a Lambda module goes through `load_script_mir_direct` (`runner.cpp:2398-2415`, called from `js_source_module.cpp:45`). That function **pins `LAMBDA_TIER_JIT`** for the load: the JS membrane reaches a Lambda export through a native function pointer, and `module_build_lambda_namespace` exists only with MIR (`module_registry.cpp:396`). Under `LAMBDA_NO_MIR` the whole function compiles out, so JS cannot import Lambda at all.

**MB5 — Lambda exports cross to JS as ordinary interpreted `Function` values.** T0 already publishes interpreted definitions as `Function` objects with the `FN_ENTRY_ABI_LAMBDA_INTERPRETED` entry ABI (`lambda.h:1631`, `interp.hpp:454`). The namespace builder gets a T0 path that exports those values, and the JS membrane calls them through the same dispatcher. This also fixes the desktop: AUTO stops forcing a JIT compile of every Lambda module imported from JS, which is the cold-load cost the interpreter was built to remove (`Lambda_Design_Ast_Interpreter.md` §1, motivation 1). It matters for Radiant because pages mix JS with Lambda DOM behavior templates (`lmd/package/dom`).

### 3.6 Rejected: MIR's own interpreter as a fallback

`--mir-interp` looks like a no-JIT option. It is not. The MIR interpreter generates machine-code thunks: `_MIR_get_ff_call` for foreign calls (`lambda/mir/mir-interp.c:1778`) and `_MIR_get_interp_shim`, redirected via `_MIR_redirect_thunk` (`mir-interp.c:2058`). Both need executable pages, which C1 forbids. MIR is vendored (AGENTS rule 16), so patching thunk generation out is not on the table either. D7.1.7v6 already excludes "MIR-interpreter fallback" for the same reason.

### 3.7 Stacks, fibers, faults

- **Fibers.** `lib/fiber.c` switches stacks in hand-written assembly. Its arm64 AAPCS64 path (`fiber.c:137-185`) never touches `x18`, which Apple reserves as the platform register, so it is ABI-clean for iOS. Apps run as `arm64`, not `arm64e`, so pointer authentication does not affect saved return addresses *(verify that the build does not opt into arm64e)*. Stacks are `mmap` RW with a `PROT_NONE` guard page (`fiber.c:50-57`), which iOS allows.
- **Large interpreter stack.** `interp_run_on_large_stack` creates a worker thread with a **128 MB** stack (`interp.cpp:7633`). Each activation reserves 2 MB of native stack (`activation.cpp:19`). Both are reserved virtually and committed on touch, which is fine on iOS *if* the address-space limit allows it *(verify; `com.apple.developer.kernel.extended-virtual-addressing` is the escape hatch)*. **MB6:** the stack sizes become profile parameters, sized from measured T0 recursion depth on the demo corpus, not copied from desktop.
- **Stack-overflow recovery** uses `sigaltstack` plus SIGSEGV/SIGBUS handlers with `pthread_get_stackaddr_np` (`lambda-stack.cpp:73-75,243-273`), and `sigsetjmp`/`siglongjmp` on Apple (`recovery_frame.h:42-71`). Both are available on iOS. Two interactions must be checked: Xcode's debugger catches `EXC_BAD_ACCESS` as a Mach exception before the signal is delivered, and crash reporters chain signal handlers *(verify both)*.
- **No other code generation exists.** Outside `lambda/mir/`, the tree has no `PROT_EXEC`, `MAP_JIT`, `pthread_jit_write_protect_np` or cache flushes. There is no libffi, PCRE-JIT or sljit. RE2 does not JIT. JS regex is the backtracking `js_bt_regex.cpp`. Other `mmap` calls are RW or `PROT_NONE` (`lib/mem_vm.c:53`, `lib/fiber.c:50`, `side_stack.c:53`, `pack.cpp:161`).
- **SIMD.** `lib/str_simd.c` and `lib/str_teddy.c` already have NEON paths (`str_simd.c:18-21`, `str_teddy.c:23-33`). They are kept, unlike wasm, which drops them.

### 3.8 Performance: T0 becomes the ceiling (MB-O1)

The interpreter was designed **boxed-only, with performance explicitly out of scope**, because hot code promotes to MIR (AI3; the user-ranked T0 priorities are simplicity, then correctness, then nothing for speed). On iOS nothing promotes. Interactive document UIs are dominated by runtime helpers, not by glue: on the JS batch corpus, generated code is 0.6% of CPU and helpers are 43.5% (`vibe/jube/JS_Profiling_Helpers.md` §2). So most Radiant pages should feel the same. Compute-bound demos will not: `physics`, `doom`, `scene3d`, `tetris` and chart and image kernels written in Lambda.

This proposal does **not** decide the remedy. It lists the options for a user ruling, with M8's measurements as input:

- **(a) Accept T0 speed.** Move hot numeric kernels into C runtime helpers (the existing `fn_*` library), which every tier calls anyway. This is consistent with AI3.
- **(b) Allow targeted T0 optimizations** such as cached dispatch or unboxed numeric locals. This contradicts the standing T0 priority ruling, so it needs an explicit user decision.
- **(c) Ahead-of-time native code (deferred, not proposed).** Use MIR at *build* time on the Mac to emit object code for bundled packages, linked into the signed app. This is legal on iOS (C1 forbids only run-time generation), but it is a new back-end mode outside the "no MIR on iOS" scope of this proposal. It is recorded only so it is not rediscovered as new.

## 4. Radiant: the platform layer

### 4.1 Inventory of what is desktop-bound

| Area | Today | iOS |
|---|---|---|
| Window / app shell | GLFW 3.4, one global `UiContext` (`window.cpp:434`, `view.hpp:4520`). `view.hpp:606-611` includes `<OpenGL/gl.h>` and `<GLFW/glfw3.h>` for every TU. GLFW calls: `window.cpp` 77, `ui_context.cpp` 17, `gl_core.cpp` 16, `event.cpp` 5, `state_store.cpp` 4, `clipboard.cpp` 3. | UIKit scene + view controller |
| Main loop | Blocking `while(!glfwWindowShouldClose)`: poll, libuv/JS pump, render, `glfwWaitEventsTimeout` (`window.cpp:2146-2287`); wake via `glfwPostEmptyEvent` | Run-loop-driven tick (§4.2) |
| Present | CPU raster → GL texture with damage-rect `glTexSubImage2D`, drawn with **fixed-function GL** (`glBegin(GL_QUADS)`, `glOrtho`) then `glfwSwapBuffers` and `glFinish` (`window.cpp:1153-1240,1408`) | CAMetalLayer / IOSurface (§4.3) |
| Frame clock | CVDisplayLink + mach_time (`frame_clock.cpp:12-14,127,243-250`) | CADisplayLink |
| GPU content | Hidden GL 4.1 core context for WebGL/canvas/scene3d, read back with `ReadPixels` (`gl_core.cpp:276-293,724`). GLSL ES 3.00 rewritten to `#version 330 core` (`gl_shader.cpp:42`) | §4.4 |
| Vector raster | ThorVG `sw_engine` only (`build_deps_config.json`) | Unchanged — CPU, portable |
| Fonts | Own shaper (`lib/font`), CoreText rasterizer (`font_rasterize_ct.c`), CoreText fallback (`font_fallback.c:406-555`), **directory scan** of `/System/Library/Fonts` and `~/Library/Fonts` (`font_platform.c:107-156`) | §4.5 |
| Input | Mouse, keyboard and scroll only (`event.hpp:135-153`). Pointer/touch exist only as `event_sim` test injection (`event.cpp:9523-9613`). `input_capabilities` hard-coded to fine pointer + hover (`ui_context.cpp:241-242`) | §4.6 |
| IME | `ime_mac.mm` isa-swaps GLFW's NSView to implement NSTextInputClient (`ime_mac.mm:238-269`), feeding the RAD_15 §6 composition bridge | UITextInput (§4.7) |
| Clipboard | NSPasteboard (`clipboard_mac.mm`) | UIPasteboard |
| Cursors | `glfwCreateStandardCursor` (`event.cpp:13873-13900`) | No-op on iPhone; `UIPointerInteraction` on iPad later |
| Video / speech | AVFoundation (`rdt_video_avf.mm`, `rdt_speech_avf.mm`) | Portable: AVFoundation and AVSpeechSynthesizer exist on iOS |
| Audio | miniaudio (`audio_bus.cpp`), already has `TARGET_OS_IPHONE` paths | Add an `AVAudioSession` category |
| Webview | WKWebView parented into the NSWindow (`webview_child_mac.mm:161-230`) or snapshotted (`webview_layer_mac.mm`) | WKWebView under a UIView |
| Misc mac | Dock icon (`app_icon_mac.mm`), Accelerate vImage (`render_filter.cpp`), CF text breaks and hyphenation (`text_breaks.cpp`, `hyphenation.cpp`) | Drop the icon. The rest is iOS-safe. |

No iOS work exists anywhere in the tree today. `TARGET_OS_IPHONE` appears only in vendored miniaudio.

### 4.2 The platform seam (MB7) and the inverted loop (MB8)

**MB7 — introduce a Radiant platform backend interface, and move GLFW behind it as the desktop implementation.** The backend owns:

- surface creation and resize, content scale, and safe-area insets;
- present (upload damage rects, then display);
- the frame clock and wakeups;
- the native event source;
- text input sessions;
- the clipboard;
- cursors;
- the webview host;
- lifecycle notifications.

`view.hpp` stops including GL and GLFW headers. Only backend TUs see them.

The alternative is to keep calling GLFW and implement a GLFW-shaped shim on UIKit, the way `radiant/headless.cpp` stubs GLFW for `LAMBDA_NO_GUI`. That is rejected for iOS. The null shim works because a headless run *has* no window semantics. iOS has richer semantics than GLFW can express: touches with identifiers and phases, safe areas, keyboard frames, scene lifecycle, memory warnings. A fake GLFW would either drop them or smuggle them through invented key codes, which is a workaround (AGENTS rule 1). The seam keeps D7.1.4v2's principle: every profile compiles the **same Radiant sources** against a different windowing backend, with no `#ifdef` stripping of display code. The headless null backend becomes one more implementation of the interface instead of a GLFW stub.

**MB8 — the shell drives Radiant by ticks; Radiant never owns the loop.** The body of `window.cpp:2146-2287` becomes `radiant_tick(budget)`: drain the event queue, pump libuv with `UV_RUN_NOWAIT`, run due timers and frame callbacks, lay out, paint, present. The desktop backend calls it from its own `while` loop. iOS calls it from a CADisplayLink callback while frames are wanted, and from the libuv backend file descriptor (`uv_backend_fd`, watched by a `dispatch_source` on the main queue) while idle. Then network completions and timers still wake an app that is not animating. The frame clock (RAD_16 §2) already only wakes the loop, so it maps directly onto CADisplayLink. ProMotion uses `preferredFrameRateRange` plus `CADisableMinimumFrameDurationOnPhone` in Info.plist.

**Lifecycle.** On `sceneDidEnterBackground` the backend pauses the display link, flushes persistent state (`state_store`), releases GPU resources and lets libuv idle. On foreground it restores the surface and schedules a full repaint.

### 4.3 Present path (MB9)

The renderer is already CPU: the tile pool rasterizes into an image surface and only the damage rects change. So iOS needs no GPU renderer, only a presenter. **MB9 — present through a `CAMetalLayer`.** Keep one persistent `MTLTexture` per drawable size. Upload damage rects with `replaceRegion`, then blit to the drawable. The fixed-function GL quad in `window.cpp:1205-1240` is deleted in favor of this per-backend present on both platforms. Desktop GL keeps a core-profile textured quad, or also moves to Metal on macOS, which removes the deprecated `<OpenGL/gl.h>` path entirely.

A simpler first step is an IOSurface-backed `CALayer.contents`. It is acceptable for M5 if it measures adequately, but it gives no path to compositing GPU content (§4.4) in the same layer.

**Retina cost.** A 3x iPhone surface is about 1290×2796 RGBA, roughly 14 MB per full frame. Damage-rect raster and upload keep steady state cheap. Full repaints (rotation, scale change) are the budget case M8 measures. Tile-pool width should be capped for thermal reasons on phones, using the efficiency cores at default QoS.

### 4.4 GPU content: WebGL, canvas, scene3d (MB-O2)

`gl_core.cpp` creates a desktop GL 4.1 core context and reads pixels back. iOS has no desktop GL. Options:

1. **Phase 1: compile the GPU paths out** of the iOS profile with the explicit "excluded from this build" diagnostic (the D7.1.6 pattern), so canvas falls back to its CPU path where one exists. Nothing degrades silently.
2. **OpenGL ES 3.0** through EAGL. WebGL's GLSL ES 3.00 runs nearly as is (the `#version 330 core` rewrite becomes unnecessary). But the API has been deprecated since iOS 12 and may be removed.
3. **ANGLE on Metal**, the path WebKit uses. It keeps the GLES API surface on a supported GPU API, at the cost of a large new vendored dependency (AGENTS rule 16 applies).
4. **A native Metal backend** for scene3d and a WebGL translation layer. This is the most work and the long-term option.

Recommendation: (1) for the first shipping milestone, then decide between (3) and (4) once scene3d and WebGL demand on iOS is known. (2) is not recommended as a destination.

### 4.5 Fonts (MB10)

Text shaping is portable (own GPOS/glyf/COLR/CBDT/WOFF2 code), and rasterization already uses CoreText, which iOS has. Discovery is the problem: `font_platform.c` scans hard-coded macOS directories with `opendir`. On iOS system font files are not reliably enumerable from the sandbox, and their locations are private.

**MB10 — on Apple platforms, discover fonts through CoreText.** Use `CTFontCollectionCreateFromAvailableFonts` / `CTFontManagerCopyAvailableFontFamilyNames`. Get each face's file through `kCTFontURLAttribute` when it is readable *(verify on device)*. Otherwise rasterize and measure through the `CTFontRef` without opening the file. Fonts shipped in the app bundle register through `CTFontManagerRegisterFontURLs`. Then:

- `system-ui` maps to `CTFontCreateUIFontForLanguage` (the RAD_07 branch at `font.cpp:64-73`);
- `Apple Color Emoji` stays first in the emoji fallback list (`font_context.c:73-76`);
- the `__builtin_available(macOS 13.0, *)` guard at `font_platform.c:97` gains an `iOS` clause.

Dynamic Type (user text size) is a later milestone and maps onto the root font size.

### 4.6 Touch and pointer input (MB11)

The DOM layer already dispatches pointer and touch events. Only a native source is missing.

**MB11 — the iOS backend emits native pointer events, and the DOM's existing dispatch produces the web-compatible sequence.** Each `UITouch` becomes a pointer event: `pointerType: "touch"`, a stable `pointerId`, `pressure` from `force`, and `width`/`height` from `majorRadius`. The DOM layer then derives Touch Events and the compatibility mouse events and `click` per the Pointer Events and Touch Events specs, the same derivation `event_sim` drives in tests. `input_capabilities` becomes `CSS_POINTER_COARSE` with no hover on iPhone, so `@media (pointer: coarse)` and `(hover: none)` match. On iPad with a trackpad, `UIPointerInteraction`/hover events add fine pointer and hover.

**Gestures Radiant must own**, because it renders the whole document into one layer:

- **pan scrolling with momentum and rubber-banding.** This is Radiant's own scroller physics, tuned to UIKit's deceleration rate. A hidden `UIScrollView` proxy is the alternative if hand-rolled physics feels wrong.
- **pinch-zoom** under the `viewport` meta rules;
- **long press**, which opens the existing custom-drawn context menu (`context_menu.cpp`);
- **text-selection handles and the loupe** (§4.7);
- **double-tap zoom** (optional).

The scroll and gesture recognizers yield to `touch-action` and to `preventDefault` from page listeners.

### 4.7 Text input (MB12)

**MB12 — a hidden `UIView` adopting `UITextInput` hosts every editable focus.** It feeds the same composition bridge `ime_mac.mm` feeds on macOS (RAD_15 §6): `setMarkedText` maps to composition update, `insertText` to commit, `unmarkText` to composition end. Focus on a DOM editable shows the keyboard, using `inputmode` and `enterkeyhint` for the keyboard type and return key. Blur hides it. Keyboard frame changes resize the visual viewport (`visualViewport`, `interactive-widget` semantics) rather than the layout viewport.

Staging:

1. `UIKeyInput` only: insert, delete, return. This is enough for forms.
2. Full `UITextInput`: marked text for CJK input, autocorrect, dictation, selection rects.
3. `UITextInteraction`: native selection handles, loupe and edit menu.

Hardware keyboards arrive as `UIPress`/`UIKeyCommand` and map to the existing `KEY_DOWN`/`KEY_UP` path. The editor stack (Stage 4B, the JS editor at the `beforeinput` seam) gets `beforeinput` from the same bridge, so iOS needs no editor-specific work beyond stage 2.

### 4.8 Viewport and scenes

- **Safe areas** feed `env(safe-area-inset-*)`, and `viewport-fit=cover` opts a page into drawing under the notch.
- **Rotation and iPad split view** are ordinary resizes plus a content-scale check.
- **One scene for v1.** The global `UiContext` already blocks multi-window (RAD_20 §8 item 4). iPad multi-scene waits for that refactor, which MB7's per-backend surface ownership is a step toward.

### 4.9 Media and webview

- **Video** (`rdt_video_avf.mm`) ports with little change: AVPlayer plus `CVPixelBuffer` polling exist on iOS.
- **Audio** needs an `AVAudioSession` category, plus interruption and route-change handling, so a call or a headphone unplug pauses playback correctly.
- **Speech** (`rdt_speech_avf.mm`) works as is.
- **The webview** keeps both modes: child mode parents a `WKWebView` under the Radiant view, and layer mode uses `takeSnapshot`. The webview functions take `struct GLFWwindow*` today (`radiant.hpp:237-284`), so they move behind MB7's webview host. Webview is also the escape hatch for general web browsing (§7.2).

## 5. OS services

### 5.1 Filesystem (MB13)

**MB13 — every implicit relative path becomes a host-supplied base directory.**

| Today | iOS |
|---|---|
| `g_lambda_home = "./lmd"` (`runner.cpp:339`), overridable by `LAMBDA_HOME` | `<bundle>/lmd`, read-only, set by the host before `lambda_home_init` |
| `./temp/`, `./temp/cache` (network `EnhancedFileCache`) | `Library/Caches/lambda/` (the OS may purge it) |
| `./log.txt`, `log.conf` | `Library/Caches/lambda/log.txt`, plus a bundled `log.conf`. Release builds log at error level only. |
| Script/AST caches (`InputScriptCache`) | `Library/Caches/lambda/` |
| User documents (finder, word and photo demos) | `Documents/`, plus `UIDocumentPickerViewController` with security-scoped URLs for files outside the container |

The runtime gets an explicit "host paths" initialisation (home, cache, temp, documents) instead of reading the cwd and environment variables. On desktop it defaults to today's values. Desktop-only path assumptions (`$HOME`, `/tmp`, absolute `/System` paths) are found by grep in M4 and routed through it. `io.file_search`, `io.grep` and directory enumeration work inside the sandbox unchanged.

### 5.2 Processes, terminal, CLI

- **Processes.** `cmd` (`SYSPROC_CMD`, `sys_func_registry.c:1257`), `lib/shell.c` `posix_spawn` and Node `child_process` fail with an explicit "not available on this platform" error (the D7.1.6/D7.1.7 "fail explicitly" rule). They are not stubbed to return empty output.
- **Terminal.** `lib/terminal_device.c`, the CLI entry (`main.cpp`) and the line-edited REPL are excluded. The app embeds the runtime through an entry API shaped like `wasm_embed.cpp`: init, load package, run, evaluate in a REPL session, shutdown. An in-app console view can host a REPL.
- **Environment knobs** (`LAMBDA_EXEC_BACKEND`, `LAMBDA_HOME`, `LAMBDA_LOG_LEVEL`, …). They are set through that entry API's config struct. `shell_getenv` stays as the desktop override.

### 5.3 Jube modules: static only (MB14) — deferred

**Deferred by the user (2026-10-25)** to a phase after phase 1. Phase 1 keeps only the built-in static modules (`radiant`, `dom`; §1.2). The rest of this section records the intended direction so the deferral loses nothing.

C3 forbids `dlopen` of the `modules/node-*` and `rdb-drivers` dylibs. Static registration already exists: `radiant`, `dom` and `hostobj_demo` register through `*_jube_register_static` (`jube_registry.cpp:2249-2251`).

**MB14 — the iOS profile links its Jube modules statically** and registers them with the same static entry. The dynamic loader (`jube_registry.cpp:2994-3021,4040-4056`) is compiled out. The module set is chosen per app (`modules/module-set-reduced.json` is the model). Within modules:

- `node_crypto` lazily `dlopen`s system OpenSSL (`node_crypto_module.cpp:4418-4453`), which does not exist on iOS. It moves onto mbedTLS, which is already linked, or CommonCrypto.
- `node_net` server sockets and `serve` (the HTTP server) are excluded in v1: background suspension and the local-network permission prompt make a server a poor fit.
- `rdb` keeps SQLite (built with `SQLITE_OMIT_LOAD_EXTENSION`) and drops the libpq/mariadb drivers.

This collides with **D1.1** ("bundles differ only by the Jube modules beside it; host binaries are byte-identical") and **D7.1.1** ("dynamic is reserved for Jube modules"). See §10.

### 5.4 Network and TLS trust (MB18)

curl 8.10.1 with mbedTLS and nghttp2 cross-builds statically for iOS. Its own thread (`curl_multi_backend.cpp`) and the network thread pool are fine. Phase 1 needs no network (§1.2). This section is the design for when network lands.

**How trust works today.** HTTPS already trusts the host's one certificate store rather than curl's compiled-in CA path. `curl_use_host_trust_store` installs `curl_trust_ssl_ctx` as `CURLOPT_SSL_CTX_FUNCTION` (`lambda/network/curl_trust.h`). That hands mbedTLS the root chain from `lib/trust_store.c`, which the RDB TLS bridge shares (`vibe/Lambda_IO_RDB.md` §13.9). On macOS, `trust_load_platform` *enumerates* the OS roots with `SecTrustSettingsCopyCertificates` over the system, admin and user domains, honoring each certificate's SSL trust settings (`trust_store.c:36-102`). On Linux it reads the distro bundle files (`trust_store.c:150-157`).

**Why that model cannot carry over.** iOS has **no API to enumerate the trusted roots**: the `SecTrustSettings*` family is macOS-only, and the system root store is not readable from the sandbox. An "export the OS roots into mbedTLS" design has nothing to export on iOS.

**Options:**

1. **Bundle a CA set** (Mozilla's, as `cacert.pem`), loaded through the existing `trust_add_pem_file`. It is the least code. But it goes stale between app updates and silently disagrees with the OS: it ignores roots the user installs through a configuration profile (corporate and dev proxies), MDM-managed roots, and Apple's revocations and distrust decisions.
2. **Ask the OS to evaluate each server chain.** In the mbedTLS verify callback, build a `SecTrustRef` from the chain the server presented and call `SecTrustEvaluateWithError`. This is exactly what the OS's own networking does, so trust is always current and matches Safari's.

**MB18 — recommendation: option 2, OS chain evaluation on iOS.** It keeps the design's existing principle, "trust the host's one certificate store", and option 2 is the only way to honor that principle on iOS. Option 1 would make Lambda the one app on the phone that trusts a different set of roots than the OS. Design:

- **One hook point.** `curl_trust_ssl_ctx` (and the RDB bridge, through the same `trust_store` API) keeps being the only place trust is configured. On iOS it installs an OS-backed trust hook instead of a CA chain. curl's mbedTLS backend uses one `mbedtls_ssl_config` per connection, so the hook is per-connection. `trust_store.h` gains the iOS entry point. `trust_store_roots()` returns NULL there, since there is no root list.
- **Which mbedTLS hook *(verify against mbedTLS 3.6 and curl 8.10)*.** There are two candidates:
  - **`mbedtls_ssl_conf_ca_cb`:** mbedTLS asks the hook for trusted CAs for the presented chain. The hook runs `SecTrustEvaluateWithError` and returns the anchor the OS chose (`SecTrustCopyCertificateChain`), so mbedTLS then runs its full ordinary verification against that one root. This needs `MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK` in the iOS mbedTLS config. That is a build option, not a vendor patch.
  - **`mbedtls_ssl_conf_verify`:** the per-certificate callback clears `MBEDTLS_X509_BADCERT_NOT_TRUSTED` only when the OS accepts the chain. This risks mbedTLS short-circuiting before the callback when no CA chain is configured.

  The ca-callback form is preferred if it works, because no mbedTLS verdict is ever edited by hand.
- **Split of duties.** The OS chooses *which root* the chain trusts, using the server-auth policy `SecPolicyCreateSSL(true, NULL)`. mbedTLS keeps hostname checking (curl sets the SNI/hostname) and validity periods. Both must pass. Neither can loosen the other.
- **Threading.** `SecTrustEvaluateWithError` may block on revocation or fetching missing intermediates. It runs on curl's network thread, never the UI thread, so that is acceptable. The OS fetching missing intermediates (AIA) makes iOS more tolerant of misconfigured servers than mbedTLS alone, which matches browser behavior.
- **Tests.** Cover a valid public site, an expired cert, a wrong host, a self-signed cert, and a root installed through a configuration profile. The last one is the case option 1 gets wrong.
- **macOS stays as is.** Moving macOS to the same evaluate-per-chain model would remove the root enumeration code and honor distrust decisions more exactly. It is a reasonable later unification, but not part of this proposal.

**ATS.** App Transport Security governs `URLSession`, not raw sockets, so curl is not technically subject to it. An HTTPS-only default with explicit `http:` opt-in is still the right policy for when distribution matters.

OAuth flows (`io_mail`, `vibe/Lambda_Demo_Mail.md`) use `ASWebAuthenticationSession` instead of launching a desktop browser. `io.secret` already uses `SecItem*` (`lib/secret_store.c`), which works on iOS with a keychain access group.

### 5.5 Threads

Thread pools (tile pool, ThorVG, network scheduler, interpreter workers) keep their pthread implementation. Pool sizes read `hw.perflevel0.logicalcpu`, not total cores, so phones do not schedule raster onto efficiency cores at UI priority. Threads get QoS classes: user-interactive for UI-tick work, utility for prefetch. The script watchdog (`script_runner.cpp:198`) is kept: with no JIT, a runaway loop is more likely to be noticed, not less.

### 5.6 Memory (MB15)

**MB15 — `didReceiveMemoryWarning` drives the Memory_Context reclaim ladder.** That design already names the iOS signal as prior art (`vibe/Memory_Context.md:797,1035`). Its levels drop, in order:

1. raster tiles and the image decode cache;
2. font glyph caches;
3. AST and script caches of unloaded modules;
4. a full GC.

Budgets for the shipped demos are measured in M8 against device jetsam limits. GC heap growth policy reads the available-memory figure (`os_proc_available_memory`) instead of assuming desktop headroom.

## 6. Build and toolchain

### 6.1 Targets (MB16)

**MB16 — iOS is a new platform block in `build_lambda_config.json`**, generated like every other profile (AGENTS rule 7: never hand-edit the Lua). It produces static archives for `arm64-apple-ios` and `arm64-apple-ios-simulator` from the same layered libraries: `lib` → `lambda-data` → `lambda-rt` → `radiant` (D7.1.1). They are packaged as one `Lambda.xcframework`. A thin Xcode project under a new `platform/ios/` directory holds the UIKit shell (scene delegate, Metal view, text-input view), Info.plist, entitlements and asset bundling, and links the xcframework. Swift or Objective-C++ in the shell is a free choice. The engine stays C+.

The deployment target is iOS 17 (proposal, MB-O4): it covers current devices and gets modern `UITextInput`/`UIPointerInteraction` behavior.

### 6.2 Dependencies

Every Homebrew `.a` must be rebuilt from source for both iOS slices, by a new `setup-ios-deps.sh` beside `setup-mac-deps.sh`:

- **Built from source:** mpdecimal, mbedtls 3.6, libuv, nghttp2, openjpeg, xz, zstd, fribidi, utf8proc, curl, ThorVG (with the existing `patches/thorvg-*.patch`), Brotli.
- **From the iOS SDK:** zlib and bzip2 (`libz.tbd`, `libbz2.tbd`) and iconv.
- **Dropped:** GLFW.
- **Already in-tree and portable:** the image codecs (libpng, libjpeg-turbo, libwebp, libavif/libaom, libtiff, giflib, libnsbmp), miniaudio, SQLite, RE2, the tree-sitter runtime.
- **Off:** libaom's assembly build options must stay off for the simulator slice *(verify)*.

### 6.3 Assets

`lmd/` is 22 MB, mostly `package/doc` (9.0 MB) and `package/music` (7.9 MB). The bundle step copies only the packages an app's import closure reaches, computed by the same import discovery the static-module prebuild uses (`module_ast_prebuild`, D8.5.1v4). AST prebuild output for bundled packages can ship as a cache seeded into `Library/Caches` on first launch. That is a derived cache (D1.7), never source.

### 6.4 Testing on iOS

- **Most qualification runs on macOS** in `lambda-nomir` (MB2): Lambda baseline, JS (Test262 `ast`), Radiant baseline and UI tests.
- **Simulator.** Command-line test executables built for the simulator slice run in the simulator via `xcrun simctl spawn <device> <exe>`, which needs no app wrapper. This runs `test-lambda-baseline` and the gtest suites against the real iOS libc, sandbox and filesystem layout.
- **Device.** Tests run in-app, through an XCTest target that calls the embed API over a corpus. Device runs are the only place C1/C2 are enforced for real, so the CI device job must use a Release, non-debuggable build.

## 7. App model and distribution

*Not in phase 1, which is a developer build installed from Xcode (§1.5). Recorded for when distribution becomes a goal.*

### 7.1 What ships

Two deliverables share the xcframework:

1. **A Lambda app host.** It runs bundled Lambda/Radiant apps: the demo suite (slides, word, photos, chart, finder) is the first acceptance target. Per Guideline 2.5.2, all executable script ships in the bundle. Running user-authored scripts in a playground (the Pythonista and Swift Playgrounds model) is acceptable. Downloading scripts that change the app's features is not.
2. **An embedding SDK.** Other apps embed Lambda for data processing (headless, profile-C-style, no UIKit shell) or embed a Radiant view for document rendering.

### 7.2 Radiant is a UI and document engine on iOS, not a browser (MB17)

Guideline 2.5.6 requires apps that browse the web to use WebKit, except BrowserEngineKit in the EU. Radiant plus LambdaJS *is* a browser engine. Rendering an app's own HTML/CSS UI and documents is fine: that is a UI framework, like Flutter or React Native rendering their own views. Loading arbitrary third-party web pages in Radiant is not.

**MB17 — on iOS, Radiant renders bundled and app-owned documents. Navigation to arbitrary `http(s)` origins goes to the WKWebView host (§4.9) or `SFSafariViewController`.** A navigation policy hook in the shell enforces this. Fetching data and images over the network for app-owned documents is unaffected. A future EU BrowserEngineKit build is a separate product decision, not part of this design.

## 8. Milestones (phase 2 onward)

Phase 1 has its own milestones (§1.6). The table below is the full port that follows it. Each milestone has explicit acceptance cases, per the Radiant workflow (`radiant/README.md`). Focused checks run during the work. Aggregate gates run at milestone boundaries.

| M | Deliverable | Acceptance |
|---|---|---|
| M0 | `lambda-nomir` desktop profile (MB1, MB2) for the Lambda engine only (JS off) | It builds. `test-lambda-baseline` runs under T0, with the refusal set (§3.3) measured and recorded against `lmd/package` and `test/demo` |
| M1 | LambdaJS MIR partition (MB4) and the T0 JS→Lambda bridge (MB5) | `lambda-nomir` builds with JS. Test262 under `ast` holds its desktop result. JS→Lambda import tests pass under `interp`. The 17 MIR-pinned tests are classified. |
| M2 | Close the T0 gaps the target packages hit (MB3) | Zero E501 refusals across the target demo closure. Differential T0 vs JIT stays at 0 mismatches. |
| M3 | Radiant platform seam (MB7, MB8, MB9 desktop half) | Desktop behavior unchanged: `test-radiant-baseline` 100%. `view.hpp` free of GL/GLFW. The headless backend is an interface implementation, not a GLFW stub. |
| M4 | iOS cross-build: deps, `Lambda.xcframework`, host paths (MB13, MB16; MB14 when Jube is undeferred) | `simctl spawn` runs `test-lambda-baseline` green in the simulator. No dylib loading in the image. |
| M5 | UIKit shell: Metal present, CADisplayLink tick, lifecycle, touch → pointer (MB8, MB9, MB11) | The slides demo runs and scrolls on simulator **and** a Release device build. Background/foreground keeps state. |
| M6 | Text input stages 1–2, clipboard, fonts, safe areas, media (MB10, MB12) | Forms and the word demo editable with CJK marked text. `system-ui` and emoji render. Video and audio play. |
| M7 | GPU-content decision (MB-O2), TLS trust (MB18), navigation policy (MB17) | Decided and implemented, or explicitly excluded with diagnostics |
| M8 | Device qualification: release build, memory and performance (MB6, MB15, MB-O1) | Demo budgets met under jetsam. Memory-warning ladder exercised. T0 performance numbers recorded as input to the MB-O1 ruling. |

M0–M3 need no iOS SDK and can proceed in parallel with dependency cross-building.

## 9. Ledger

### 9.1 Proposed decisions (`MB#`)

| ID | Decision | § |
|---|---|---|
| MB1 | iOS is a `LAMBDA_NO_MIR` profile: T0 only, no promotion, no MIR-interp, enforced at compile time | 3.1 |
| MB2 | A desktop `lambda-nomir` twin is built first and stays as a CI gate | 3.1 |
| MB3 | T0 refusals are defects to close for the iOS target set, not features to drop | 3.3 |
| MB4 | LambdaJS gets a MIR partition; `JS_EXEC_BACKEND` is forced to `ast` under no-MIR, with no fallback | 3.4 |
| MB5 | Lambda exports cross to JS as interpreted `Function` values; JS→Lambda import stops pinning JIT | 3.5 |
| MB6 | Interpreter and activation stack sizes become profile parameters sized from measured depth | 3.7 |
| MB7 | A Radiant platform backend interface; GLFW and headless become implementations; no GLFW shim on iOS | 4.2 |
| MB8 | Radiant runs by ticks; the platform owns the run loop; libuv is pumped `NOWAIT` and woken by its backend fd | 4.2 |
| MB9 | Present through CAMetalLayer with damage-rect texture upload; the fixed-function GL quad is retired | 4.3 |
| MB10 | Apple font discovery goes through CoreText collections, not directory scans | 4.5 |
| MB11 | Native touch → pointer events; the DOM derives touch, compat-mouse and click; coarse pointer, no hover | 4.6 |
| MB12 | A hidden `UITextInput` view feeds the existing composition bridge, staged in three steps | 4.7 |
| MB13 | Host-supplied base directories replace cwd-relative `./lmd`, `./temp` and `./log.txt` | 5.1 |
| MB14 | Jube modules link statically on iOS; the dynamic loader is compiled out (**deferred** by the user, 2026-10-25) | 5.3 |
| MB15 | Memory warnings drive the Memory_Context reclaim ladder | 5.6 |
| MB16 | An iOS platform block in `build_lambda_config.json` emits a `Lambda.xcframework`; a thin Xcode shell lives in `platform/ios/` | 6.1 |
| MB17 | Radiant renders app-owned documents; arbitrary web navigation goes to WebKit | 7.2 |
| MB18 | TLS on iOS: the OS evaluates each presented chain (`SecTrustEvaluateWithError`) behind the existing `curl_trust_ssl_ctx` hook; no bundled CA set | 5.4 |
| ~~MB19~~ | ~~Phase 1 builds Radiant with `LAMBDA_NO_JS`~~ — superseded by MB22 (Appendix S) | 1.2 |
| MB23 | `mvp_lmd_mir.cpp` is split: shared types in `mvp_lmd_internal.h`, MIR-free program/realm runtime in `mvp_lmd_program.cpp`; the no-MIR build drops the generator file whole (user ruling 2026-10-26) | 1.7 |
| MB22 | The phase-1 build is `lambda.exe` minus MIR: the LambdaJS runtime and AST interpreter stay, because the DOM is built on the JS object model; only MIR code generation is excluded | 1.2 |
| MB20 | Phase-1 demo set: tetris, sentence-game, greetings (core); draw, superlambda (stretch) | 1.1 |
| MB21 | A document without a `viewport` meta lays out at a 1024 CSS px layout viewport scaled to fit, with pinch-zoom | 1.4 |

### 9.2 Open questions (`MB-O#`)

| ID | Question | Needed by |
|---|---|---|
| MB-O1 | T0 is the performance ceiling on iOS: accept it with C helpers, allow targeted T0 optimizations (contradicts the T0 priority ruling), or later pursue build-time AOT? | M8 |
| MB-O2 | GPU content (WebGL, canvas, scene3d): exclude, ANGLE-on-Metal, or a native Metal backend? | M7 |
| MB-O4 | Minimum iOS version (proposed 17) and iPad multi-scene timing | M4 |
| MB-O5 | Which Jube modules ship in the iOS host, and is the node-compat surface wanted on iOS at all? (deferred with Jube) | after phase 2 |
| MB-O6 | Is the macOS presenter also moved to Metal (removing deprecated GL entirely), or kept on a core-profile GL quad? | M3 |

## 10. Spec impact (for ratification — not yet proposed as rulings)

If this design is accepted, these formal-spec changes would follow. **None is made by this document.** Each needs the user's ruling (`doc/Doc_Convention.md` §2).

- **New D7.1.8 — `lambda-ios` profile.** It is defined against D7.1.7v6 the way D7.1.7 is defined against D7.1.6: MIR and all JIT machinery excluded, evaluation stays in AST (Lambda and JS), and the retained and excluded capability lists come from §3.1, §5.2 and §5.3. A companion note states the `lambda-nomir` desktop twin.
- **D1.1 and D7.1.1 (deferred with Jube).** Both assume Jube modules are dynamic libraries beside a byte-identical host. MB14 needs either an amendment ("on platforms that forbid loading unsigned code, Jube modules link statically into the host; the module contract is unchanged") or a ruling that the iOS host is a separate product outside D1.1.
- **D7.1.4v2.** Generalize "null windowing backend in place of GLFW" to "a windowing backend" (MB7), with the null backend one implementation among GLFW and UIKit.
- **D8.1.1 and D8.1.3.** Note that under a no-MIR profile, AUTO's fallback paths do not exist and refusal is the only outcome (already true for Lambda per D8.1.1v15; new for JS via MB4). Record MB5's T0 export path for JS→Lambda imports.
- **D4.5 (Radiant seam).** If MB8's tick inversion changes ownership of the event loop between rt and radiant, D4.5 records it.

## Appendix S — Superseded rulings

- ~~**MB19** (rev 2, 2026-10-25): phase 1 builds Radiant with `LAMBDA_NO_JS`, stubbing JS entry points through an extended `no_js_stubs.cpp`.~~ Superseded the same day by **MB22**. The first P1.0 link showed `lambda/dom` is built on the LambdaJS object model, so the JS runtime cannot leave a build that keeps the DOM (§1.2, §1.7).

## Appendix A — Rejected alternatives

- **MIR-interp as the no-JIT tier.** It generates machine-code thunks (§3.6).
- **A JIT entitlement or the debugger trick.** Neither is available to App Store apps. The trick hides the crash until TestFlight (C1, C2).
- **A GLFW-shaped shim on UIKit.** It loses touch, keyboard, safe-area and lifecycle semantics or smuggles them through fake codes (§4.2).
- **Running `lambda-wasm` inside a WKWebView.** JavaScriptCore would JIT the wasm, but Radiant would not render, and JS, files and network would all be lost. It is a demo path at best, not a port.
- **OpenGL ES as the long-term GPU API.** Deprecated since iOS 12 (§4.4).
- **A bundled CA set for TLS on iOS.** It goes stale and disagrees with the OS's trust decisions and user-installed roots (§5.4, MB18).
- **Relaunching the app to switch demos.** It would hide in-process teardown leaks instead of fixing them (§1.3).
- **Build-time AOT through MIR.** Legal, but out of this proposal's "no MIR on iOS" scope. It is recorded under MB-O1(c) so it is not rediscovered as new.

## Appendix B — Implementation anchors

| Concern | Anchor |
|---|---|
| Tier default and refusal under no-MIR | `lambda/runtime/interp.cpp:57-78` |
| Backend env selection | `lambda/main.cpp:2408-2437` |
| T0 support scan | `lambda/runtime/interp_plan.cpp:141-245, 465-829` |
| Whole-module MIR fallback (AUTO) | `lambda/runtime/runner.cpp:1871-1896` |
| Strict-interp refusal | `lambda/runtime/runner.cpp:1548-1564` |
| JS→Lambda JIT pin | `lambda/runtime/runner.cpp:2398-2415`, `lambda/js/js_source_module.cpp:45`, `lambda/runtime/module_registry.cpp:396` |
| JS interpreter and its scan | `lambda/js/js_interp.cpp:5847-5940`; satellites at 6477, 6774 |
| Unconditional JS MIR | `lambda/js/js_execution.cpp:42, 667-692, 763-809`; `lambda/js/mvp-lmd/mvp_lmd_mir.cpp` |
| MIR-interp thunks | `lambda/mir/mir-interp.c:1778, 2058` |
| Executable-memory allocator (MIR only) | `lambda/mir/mir-code-alloc-default.c:21-63` |
| Fiber assembly | `lib/fiber.c:50-57, 137-185` |
| Large interpreter stack | `lambda/runtime/interp.cpp:7633` |
| Activation stacks | `lambda/runtime/activation.cpp:19` |
| Stack-overflow recovery | `lambda/runtime/lambda-stack.cpp:73-75, 243-273`; `lambda/runtime/recovery_frame.h:42-71` |
| Wasm no-JIT profile | `build_lambda_config.json` (`lambda-wasm`, `LAMBDA_NO_MIR` at 2396); `lambda/runtime/wasm_embed.cpp`; `doc/dev/Lambda_WASM_Build.md` |
| Headless profile | `build_lambda_config.json:1252-1286`; `radiant/headless.cpp` |
| GLFW window and loop | `radiant/ui_context.cpp:204-300`; `radiant/window.cpp:1153-1240, 1408, 1690-1706, 2146-2287` |
| Frame clock | `radiant/frame_clock.cpp:12-14, 127, 243-250` |
| GPU context | `radiant/gl_core.cpp:276-293, 724`; `radiant/gl_shader.cpp:42` |
| Font discovery | `lib/font/font_platform.c:97, 107-156, 173, 212`; `lib/font/font_fallback.c:406-555` |
| Input model | `radiant/event.hpp:135-185`; `radiant/window.cpp:888-1126`; `radiant/ui_context.cpp:241-242` |
| IME | `radiant/ime_mac.mm:238-269` |
| Webview | `radiant/radiant.hpp:237-284`; `radiant/webview_child_mac.mm:161-230` |
| Jube static/dynamic | `lambda/jube/jube_registry.cpp:2249-2251, 2994-3021, 4040-4056` |
| Lambda home | `lambda/runtime/runner.cpp:339-343` |
| Process spawn | `lib/shell.c:751-763`; `lambda/runtime/sys_func_registry.c:1257` |
| Secret store | `lib/secret_store.c` (`SecItem*`) |
| TLS trust hook | `lambda/network/curl_trust.h` (`curl_trust_ssl_ctx`); `lib/trust_store.c:36-102` (macOS enumeration), `150-157` (Linux bundles) |
| Static Jube built-ins | `lambda/jube/jube_registry.cpp:2249-2251` (`radiant`, `dom`) |
| Radiant TUs including JS headers | `radiant/{cmd_layout,event,event_sim,event_sim_render,layout_block,scene3d_physics,speech,script_runner,ui_context,window}.cpp` |
| Phase-1 T0 probes | `test/ui/tetris_autoplay.json`, `superlambda_autoplay.json`, `greetings/studio.json`, `draw/{smoke,gestures}.json` |
