# Lambda Review 1 — notes from building `lambda.archive`

> **Status:** review, 2026-10-22. Informative: it proposes, it does not rule. Items that would change a ruling need the user's decision and then a revision of the formal spec (`doc/Doc_Convention.md` §3).
> **Basis:** one working session that implemented Phase 1 (M0–M4) of [Lambda_Pkg_Archive.md](Lambda_Pkg_Archive.md): 21 Lambda modules (about 2,100 lines) under `lmd/package/archive/`, the demo `test/demo/archive/` with its model golden and native replay, about 900 lines of tests, and about 1,300 changed lines of C/C++ in `lambda/`, `lib/` and `radiant/` (the enablers and the defects in §5). Every item below was met in that work, not taken from reading.
> **Spec linkage:** S7.6.7v4 (procedure handlers are statements), S12.1.1v3 (`fn`/`pn`), S13.2.1 (addresses), S13.3.1v2 (task ownership), S13.3.2 (cancellation), S16.10.1v2 (reserved names), S17.3.1 (`argv`), S11.4.3/D3.3.2v2 (the implicit `any \ error` return contract, E208).
> **IDs:** RV1-1 … RV1-27 are this review's items; §4 ranks them.

---

## 1. Summary

Lambda carried a substantial application — a downloader with progress, HLS reassembly, a search library, a worker-process pool and a reactive desktop UI — further than most scripting languages would without third-party libraries. The data model, the `fn`/`pn` split, structured concurrency and the IO surface held up well. The friction was concentrated in a few places:

1. **Error values in ordinary expressions** (RV1-1, RV1-2, RV1-3): an error compared with `null` is an error, and catching a procedure's error needs a three-line idiom. These caused the most wrong code that compiled.
2. **Line joining and reserved names** (RV1-8, RV1-9, RV1-10): the most frequent small stops, several with diagnostics that point at the wrong place.
3. **Tier drift** (RV1-19): three miscompiles appeared only under the JIT, behind T0-passing goldens.
4. **Reactive views and Radiant frames** (RV1-20): the demo's viewer frame is still affected.

---

## 2. What works well

**The `fn`/`pn` split (S12.1.1v3).** Planning, classification, the HLS and WebVTT parsers, the URL rewrites and every derived rendering are `fn`; acquisition, packing and the store are `pn`. The pure half was tested without a network or a file, and a stored capture can be re-derived (`refresh`) because derivation is a function of the capture. The split also made the worker-process boundary obvious: what crosses it is data.

**The data model and its formats.** Configuration came from YAML, records went to JSON, snapshots to ZIP, and all of them were ordinary maps, arrays and elements. Building a ZIP was `output(<fs kind:'dir', …>, path, {format:'zip', deterministic:true})`; reading one back was `content(input(zip)^)` and `input(member, 'binary')`. Binary values concatenate with `++` and compare with `==`, which made the byte-for-byte stream tests one line each.

**String patterns.** CSS `url(…)`/`@import` rewriting, HLS attribute lists (`NAME="quoted,with,commas"`), WebVTT tag stripping and slugging were each a short pattern with `find`/`replace`. A `pub type` pattern is reusable across modules (after the fix in §5).

**`io.fetch` as one response map.** `{status, ok, url, headers, body, bytes}`, the same for HTTP and local paths, with `to:` for disk, `max_bytes`, headers and a progress callback. Because local paths go through the same call, the whole media fixture site (HLS master playlists, byte ranges, subtitles, galleries) ran over `file:` URLs with no HTTP server in the tests. The progress callback runs on the task's own stack, rate-limited, as a level rather than a queue, so a fast transfer never queues work behind itself.

**Structured concurrency.** Tasks, mailboxes, `select`, `wait` and `cancel` were enough for the job service as an actor with a worker pool. Cancellation as error 319 at park points propagated correctly through `io.fetch` and through `io.exec` to the child process, which was terminated with no orphan. `let r: map | error = wait(h)` is a clean way to receive a task's outcome.

**`io.exec` without a shell, with line progress.** Running `lambda run worker.ls` as a child, with its step lines as progress, made "CPU-heavy work off the window" (AR-O12) a small module.

**`io.text_search`.** BM25 search with phrases, prefixes, negation and snippets over plain files: the archive's search is about twenty lines and is always current.

**The document stack.** The HTML reader is robust (it even parses JSON-LD into maps); the Readability port dropped straight in; `radiant.export_document` produced PDFs and PNGs headless; `radiant.image_metrics` and `load({thumbnail})`/`save` covered the gallery's size filter and thumbnails without decoding full images.

**Reactive views, and replays that can verify them.** State in a view, handlers as `pn`s, and `dom.dispatch` from a task delivering `on job(evt)` with a Lambda map in `evt.detail` gave the demo live job rows with no polling. The headless event replay with `assert_timeout` retries, `type`/`click`/`switch_frame` steps and surface captures checked the whole UI end to end.

**Diagnostics with code frames.** Most errors show the line, a caret and a suggestion ("write `;` to start a new statement").

**The formal specs.** Numbered rulings (S#/D#), `utils/formal_index.py <ID>` and the implementation marks made it quick to find whether a behaviour was intended. S13.3.1v2's document-owner arm, for example, was ruled before it was built, so the implementation had a target.

**Package layout.** A facade `lmd/package/archive.ls` with submodules under `lmd/package/archive/`, `lambda.<pkg>.<module>` resolution and `pub` visibility gave a clean public surface and private helpers.

---

## 3. Issues

### 3.1 Semantics

**RV1-1 — An error compared with `null` is an error.** With `failure` holding a caught error, `failure != null` evaluates to an error, and `if (failure != null)` then makes the whole procedure return that error. The archive's job service silently returned errors until every such check became `failure is error`. Separately, `e = error("x")` outside a handler raises at the assignment. Both follow from errors propagating through operators, but neither is what the code says.
*Proposal:* make `== null` and `!= null` total for error values (an error is a value that is not null), or warn at compile time when a binding that may hold an error is compared with `null`, suggesting `is error`.

**RV1-2 — Catching a procedure's error is verbose (S7.6.7v4).** A handler on a procedure call is a statement, so `let x = io.fetch(u) ^ { v }` compiles and yields `null` (the concurrency guide's status table says it should be a compile error). The working idiom is three lines, written about 25 times in this package:

```lambda no-run
var response = null
var failure = null
io.fetch(u) ^ { failure = ^ } ~ { response = ~ }
```

`let r: map | error = wait(h)` is much nicer, but it is documented only in the concurrency guide, and I found it late.
*Proposal:* make the value-producing form a compile error now; document `let x: T | error = pn_call()` as the primary idiom wherever handlers are taught; consider an expression form (`let r = try io.fetch(u)`) that yields `T | error`.

**RV1-3 — A guard `if` in a block-bodied `fn` changes its result.** In `pub fn plan(url, cfg) map^ { let u = parse(url); if (bad) { raise error(…) }; …; {…} }` the statement `if` contributes to the block's content, so the body is a list, and the failure surfaced at run time as "type check at function return failed: expected map, got array". The declared return type made the mismatch visible at compile time. The workaround is an expression guard: `let ok = if (bad) raise error(…) else true`.
*Proposal:* a branch that only raises or returns adds no content; independently, check a block body against a declared return contract when compiling.

**RV1-4 — Closures capture `var`s by value.** A progress callback cannot count, collect or remember the last value. Every accumulating callback in the tests became a collector task fed by `send`. It is correct and safe, but heavy for a running total.
*Proposal:* a small mutable cell type (`cell(0)`, `c.value`), or by-reference capture limited to `pn` closures.

**RV1-5 — Task scopes are per block (S13.3.1v2).** A `start` inside a `while` body is joined when that pass of the loop ends, so a service loop that starts a job per request runs its jobs one at a time. The archive service had to start a fixed pool of workers at its top level and route jobs to them through messages.
*Proposal:* document the worker-pool pattern next to S13.3.1v2's examples; consider whether a loop body should share its function's scope.

**RV1-6 — `start` needs a procedure known at compile time.** A `pn` passed as a parameter cannot be started, so the service could not take the job procedure as an argument; the job moved to its own module to avoid an import cycle with the facade.
*Proposal:* accept `pn` values and check them at run time, as calls already do.

**RV1-7 — Cancellation is a number.** The code reads `failure.code == 319`, while S13.3.2 speaks of `'cancelled'`.
*Proposal:* symbolic error identities for the runtime's own errors (`failure.code == 'cancelled'`, or `failure is cancelled`).

### 3.2 Syntax and diagnostics

**RV1-8 — Reserved names take natural API names (S16.10.1v2).** `open`, `list`, `commit`, `put`, `last`, `type`, `range`, `number`, `element` and `at` all collided; the public API became `library`, `snapshots` and `finish`. The diagnostics differ by name:
- `type`, `number`, `range`, `element`, `list`: "'X' is a reserved keyword and cannot be used as a name" (good);
- `at` (the `for` axis word): "expected an expression";
- a module imported as `file`: compiles, then fails at run time with "call target is not a function (type 15)".

*Proposal:* contextual keywords, reserved only in their syntactic positions (`at` only inside `for (…)`, `view`/`state` only in a view declaration); and one diagnostic for every reserved name in a binding position, including import aliases.

**RV1-9 — Line joining needs frequent `;`.** A line starting with `[` or `<` continues the previous expression, so `let x = …` followed by an array literal on the next line needs `;`. A trailing `^` followed by a line opening with `{` is read as a handler. In test scripts I inserted the `;` with a script.
*Proposal:* a line after a complete `let`/`var` statement never continues it; a handler body must open on the `^` line (it already must, per the parser's own message), so a `{` on the next line after a trailing `^` can be a new statement.

**RV1-10 — A handler not directly after its view is misreported.** A `pn` placed between `on job` and `on click` leaves the later handlers unattached, and the error is a line-continuation error at the document's `<html>` element, 60 lines later. Reducing this to a repro took the longest of anything in the demo.
*Proposal:* name the view in the handler (`on <archive_app> click(evt)`), or report "handler `on click` is not attached to a view; handlers must follow their `view`".

**RV1-11 — Small inconsistencies.**
- A declared `pn` cannot use `=> expr`; an `fn` can.
- Element content allows one trailing spread, and `<div <h2 "Jobs">, *rows>` fails with "'<' and '>' are ambiguous with element syntax"; the children must be built as an array first.
- Children are separated by `,`, `;` or newlines under different rules.
- `"x" in s` on strings is not substring membership; `contains(s, "x")` is.

### 3.3 System functions and runtime

**RV1-12 — `sys.*` values need `#`, and `string()` hides the mistake.** `string(sys.proc.self.argv[0])` returned the text `"sys.proc.self.argv.0"`, which became the program name given to `posix_spawnp`.
*Proposal:* `string()` (and other value-taking builtins) resolve a system path, or report that it is a path.

**RV1-13 — `lambda run` takes no script arguments (S17.3.1).** The worker receives its request through an environment variable.
*Proposal:* `lambda run script.ls -- arg…`, the arguments appearing in `argv` after the script path, which S17.3.1 already positions.

**RV1-14 — `io.copy` on a directory reports success and writes an empty file.** Documented in `Lambda_Sys_Func.md`, but it loses data silently; the archive wrote its own `copy_tree`.
*Proposal:* copy the directory recursively, or raise an error.

**RV1-15 — `print` joins with spaces, including before the newline.** `print("x", "\n")` writes `"x \n"`. (It also buffered whole blocks on a pipe; fixed in this work, §5.)
*Proposal:* no separator before a string that starts with `"\n"`, or document `print` with string concatenation as the idiom.

**RV1-16 — Three object models for one page.** The HTML reader's elements (which parse JSON-LD into maps), Readability's node records (which keep only a script's text) and the live `dom.*` API. The media engine reads JSON-LD from the first and everything else from the second.
*Proposal:* one documented conversion between them, or a shared read-only node API.

**RV1-17 — Every process writes `./log.txt`.** A child `lambda run` replaced its parent's log; the workers now run with `--no-log`, so their own failures are seen only through exit codes and `derived.json`.
*Proposal:* per-process log names (`log.<pid>.txt` for children), or appending with a PID prefix.

**RV1-18 — One MIME table serves two purposes.** `*.ts` is TypeScript for the language tooling and MPEG-TS for media; the table now also knows `.m3u8`, `.m4s` and `.vtt`. Harmless today; worth a note before `io.fetch` content types are trusted for more.

### 3.4 Architecture

**RV1-19 — The tiers drift apart.** Three miscompiles appeared only under the JIT, and T0-passing goldens hid them until I ran the JIT by hand: `x == w` with `w` a `var` initialised to `null` and assigned in a loop (lowered as a null test); a statement handler in a `pn … T^` re-raising what it caught; and, on both tiers, an imported pattern read through the importer's type list. All three are fixed (§5).
*Proposal:* the gtest harness runs every procedural and package test on both tiers by default, not only in the separate JIT sweep.

**RV1-20 — Radiant behaviours that reach application authors.**
- A view whose state changes often (every job message) recreates its `<iframe>` on each rebuild, and a frame's load commits only on a full window-loop turn, so a load that completes after the next rebuild is dropped ("navigation-async: could not present"). Not fixed; it limits the archive viewer.
- A repeated SVG background is drawn through ThorVG once per tile: a page with a 64×32 tiled background takes about 12 s to thumbnail.
- Headless replay time is virtual unless something counts as pending, so whether a step waits real time depends on what is running. Background tasks are now counted (§5), but the rule is not visible to a test author.

*Proposal:* keep an element's identity across a rebuild when its output is unchanged (the frame is the sharpest case); rasterize an SVG background once per paint; document the replay's time model next to `assert_timeout`.

**RV1-21 — The end-of-run drain and root waits used one watchdog.** A root task's `sleep(6500)` failed with E502 after 5 s, and a script loaded into a viewer frame drained the window's document-owned tasks for 5 s. Both are fixed (§5); the lesson is that "idle" and "stuck" were one concept and are now two.

### 3.5 Tests, docs and packaging

**RV1-22 — A golden of booleans does not say what failed.** Tests that return `[true, true, false, …]` fail as "index 5 is false"; each failure meant adding prints to find which check it was.
*Proposal:* a `check(label, condition)` helper (or an `expect` builtin) whose output names the failing case and its values.

**RV1-23 — Registration is manual and duplicated.**
- A new test directory goes into a list in `test_lambda_gtest.cpp`.
- A negative test needs a hand-written `TEST_F` with the message, and its `.txt` golden is not read.
- The demo catalog lives in four places: `LAMBDA_DEMOS` in `lambda/main.cpp`, `test/demo/README.md`, `doc/demo.md` and `doc/Lambda_CLI.md`.

*Proposal:* discover test directories and negative cases from the tree; generate the catalog sections from `LAMBDA_DEMOS`.

**RV1-24 — One feature touches many documents.** A runtime feature updates `Lambda_Sys_Func.md`, `Lambda_Concurrency.md`, the formal spec and its index, the vibe design record, `test/README.md` and `Lambda_Packages.md`. That is the right discipline, but the list was assembled by memory.
*Proposal:* a short checklist per change type in `Doc_Convention.md` (a sys func, a runtime behaviour, a package, a demo).

**RV1-25 — `sys.lambda.home` is relative to the working directory.** Package scripts located by `sys.lambda.home# ++ "/package/…"` work only when the process's cwd is the checkout or release root, as the demos require.
*Proposal:* an absolute home, resolved from the executable's location when `LAMBDA_HOME` is unset.

**RV1-26 — Errors carry codes but scripts compare messages.** Tests checked `contains(err.message, "missing.css")` because codes like 401 (file not found) are shared by many causes. A structured `path`/`url` field on IO errors would make such checks exact.

**RV1-27 — The demo needs an environment variable for its test root.** `ARCHIVE_DEMO_ROOT` exists only so the replay can use a fresh library. A replay-level way to pass arguments to a document (or to run it in a scratch working directory) would serve every demo.

---

## 4. Recommendations, ranked

| Rank | Item | Change | Why first |
|---|---|---|---|
| 1 | RV1-1 | `== null` / `!= null` total for errors, or a compile-time warning | Wrong code that compiles; hit repeatedly |
| 2 | RV1-2 | Value-producing procedure handler is a compile error; promote `let x: T \| error = pn()` | Silent `null`; most frequent boilerplate |
| 3 | RV1-19 | Run procedural and package tests on both tiers | Three JIT-only defects hid behind T0 goldens |
| 4 | RV1-8, RV1-10 | Contextual keywords; one reserved-name diagnostic; "handler not attached to a view" | Misleading diagnostics cost the most time per occurrence |
| 5 | RV1-3 | Content-free guard branches; compile-time return-contract check for block bodies | A run-time failure for a compile-time-visible mismatch |
| 6 | RV1-13, RV1-14 | `lambda run … -- args`; recursive or failing `io.copy` on directories | Small changes, real traps |
| 7 | RV1-20 | Keep element identity across unchanged rebuilds | Blocks frame previews in event-heavy views |
| 8 | RV1-22, RV1-23 | `check(label, cond)`; test and catalog discovery | Faster failures, less registration |

RV1-1, RV1-2 (the compile-error half), RV1-3 and RV1-8 touch rulings (S7.6.7v4, S16.10.1v2 and the content rules) and need the user's decision before the formal spec changes.

---

## 5. Defects fixed during the work

Each has a regression test unless noted.

| Defect | Fix | Test |
|---|---|---|
| An imported `pub type` pattern was read through the importer's type list (wrong pattern or "const_pattern: invalid index"), both tiers | `interp.cpp`, `transpile-mir.cpp`: an imported pattern is its declaration-owned `TypePattern` | `test/lambda/import_patterns.ls` |
| JIT: `x == w`, `w` a `var` initialised to `null` and assigned in a loop, lowered as a null test of `x` | `mir_is_null_literal_node`: only a flow-stable binding's static `null` type counts | `test/lambda/proc/var_null_loop_compare.ls` |
| JIT: a statement handler `call() ^ { e = ^ }` in a `pn … T^` re-raised the error it caught | `transpile_proc_side_effect`, `handler_body_consumes_error_value` | `test/lambda/proc/handler_in_raising_pn.ls` |
| A root task's wait failed with E502 after 5 s while a timer, job or child process could still wake it | `scheduler_run_until` re-arms the watchdog while the loop is busy | `test/lambda/conc/root_long_wait.ls` |
| `print` was block-buffered on a pipe, so a Lambda child's lines reached `io.exec` progress only at exit | `pn_print` flushes on newline (about +10 % on a 200,000-line print loop) | covered by the archive worker tests |
| A local `io.fetch` ignored `Range`; HLS playlists came back as binary | single-range reads (206/416, `content-range`); `mpegurl` is text; MIME entries for `.m3u8`, `.m4s`, `.vtt` | `test/lambda/proc/fetch_local.ls` |
| The end-of-run drain waited on document-owned tasks (a viewer frame stalled 5 s) | the drain counts only tasks no document owns | covered by the demo replay |
| Headless replay retries spent no wall time while Lambda tasks were working | the retry path also waits while the Lambda event loop is alive | covered by the demo replay |
| `start(…, {owner: 'document'})` (S13.3.1v2) was ruled but not implemented | front end, both tiers, scheduler, Radiant probe and teardown release | `test/lambda/conc/start_owner_outside_document.ls`, `negative/semantic/start_owner_invalid.ls`, the demo replay |

## 6. Open defects found

- RV1-20: frames dropped across reactive rebuilds; SVG backgrounds drawn per tile.
- RV1-10: the misleading error for a handler not attached to its view.
- RV1-14: `io.copy` on a directory.
- The default (auto) JS tier does not interrupt a `while(true){}` ResizeObserver callback at `LAMBDA_JS_EXEC_TIMEOUT_SECONDS`; explicit `JS_EXEC_BACKEND=interp` and `jit` both do (`RadiantViewTest.NativeLayoutPreservesScriptCpuBudgetAndObserversRemainBounded` times out). Found in the Radiant baseline. It does not appear to come from this work, which does not touch `lambda/js` (disabling this work's watchdog change did not affect it), but it has not been checked against a build without these changes.
