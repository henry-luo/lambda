# Slow Online-Page Loading: YouTube Iframes and GitHub Brotli

**Status:** Investigation record — open; no implementation change  
**Date:** 2026-09-18  
**Scope:** Retained `make test-radiant-online` logs from 2026-09-17, with
focus on the `youtube.com/embed/...` iframe cases and
`https://github.com/google/brotli`.  
**Spec linkage:** None. This record makes no language or architectural ruling;
no `S#` or `D#` point applies.

---

## 1. Executive finding

The slow YouTube pages are **not network-bound** after the iframe document has
arrived, and their dominant cost is **LambdaJS AST execution**, including
serial script/lifecycle work. AST parsing and building are material, but they
do not explain most of the 116–122 second blocks. Browser documents now use
the AST backend unconditionally, eliminating the prior parse-and-discard
backend-selection probe.

GitHub's roughly 6.4 GiB peak is **not a 6.4 GiB script cache**. It is a
process physical-footprint peak composed chiefly of two full CSS cascades and
their style allocations, with a substantial transient JavaScript module/JIT
wave. The post-script CSS recascade alone adds about 2.0 GiB after only two
DOM mutations.

The retained logs establish the phase-level causes. They do not have the
per-script execution or per-style-epoch allocation counters necessary to
divide those phases into exact byte or millisecond ownership.

## 2. Evidence and measurement limits

### 2.1 Artifacts

The evidence is the child-process log emitted by the online test harness:

- `temp/test_radiant_online_view_reason_docs_lambda.log`
- `temp/test_radiant_online_view_pnpm_docs_lambda.log`
- `temp/test_radiant_online_view_nginx_unit_docs_lambda.log`
- `temp/test_radiant_online_view_wireshark_docs_lambda.log`
- `temp/test_radiant_online_view_brotli_repo_lambda.log`

The GTest parent measures the wall time of the whole `lambda.exe view` child.
The child `[TIMING] load: total` lines are internal document-pipeline timing;
when a page contains an iframe, the final such line can describe the child
document rather than the top-level page. The logs below therefore use the
explicit `SLOW BLOCK iframe` and `MEMSTAGE` records, not a final-load-line
shortcut.

### 2.2 Meaning of memory values

`MEMSTAGE` reports macOS `TASK_VM_INFO.phys_footprint`; the final
`[PEAK_FOOTPRINT]` uses `ledger_phys_footprint_peak`. These are physical
process-footprint metrics, not virtual-address size and not only allocations
known to `memtrack`. See `lib/log.c:log_mem_stage` and
`lambda/main.cpp:main`.

### 2.3 Timing precision

The saved trace timestamps have one-second resolution. The debug-only
`RADIANT_JS_TASK_TIMING` breakdown was not enabled, so a boundary between two
timestamps proves that work occurred in that interval but does not apportion
it precisely between parse, AST construction, execution, DOM bridging,
microtasks, and global-state synchronization.

## 3. The slow YouTube iframe cases

### 3.1 A common signature

| Top-level case | Embedded player block | Player source / AST evidence |
|---|---:|---|
| `reason_docs` | 122,428 ms | YouTube player document; script #10: 190,267 AST nodes |
| `pnpm_docs` | 116,772 ms | YouTube-nocookie player document; script #10: 190,267 AST nodes |
| `nginx_unit_docs` | 116,769 ms | YouTube player document; script #10: 190,267 AST nodes |
| `wireshark_docs` | 116,745 ms | YouTube player document; script #10: 190,267 AST nodes |

Each player document carries about 2.56 MiB of JavaScript. The `reason_docs`
iframe's HTTP document fetch completed in about 276 ms; the 122-second wait
began only after the iframe had reached its script phase. The other three
cases exhibit the same player source, backend choice, and approximately
two-minute script block.

### 3.2 Backend selection: AST, not MIR

At the time of the recorded run, `MIR_RADIANT_AST_NODE_THRESHOLD` was 25,000
nodes in `lambda/runtime/mir_policy.hpp:MIR_RADIANT_AST_NODE_THRESHOLD`; the
runner used a parse-only probe to select a document-wide AST realm. The
YouTube player logs explicitly record:

```
script_runner: document selects AST backend; script #10 has 190267 AST nodes
js-mir: document AST (...) uses AST executor
```

Thus, despite the historical `js-mir:` log prefix, the heavy player-document
scripts run through the tree-walking AST executor in
`lambda/js/js_interp.cpp:js_interp_execute_script`, not through generated MIR
code. Small supporting work, including Lambda DOM-package code and some
module paths on other pages, may still use MIR; it is not the dominant player
document path.

As of 2026-09-18, `execute_document_scripts` in
`radiant/script_runner.cpp` sets `Runtime::js_ast_backend` unconditionally.
`cmd_layout` also no longer creates a document MIR lease session. The runner
therefore no longer parses a source solely to select a backend, and browser
documents cannot enter the incompatible cached-MIR path.

### 3.3 `reason_docs` timeline

The following stages are reconstructed from
`test_radiant_online_view_reason_docs_lambda.log`. Durations are approximate
because timestamps are whole seconds.

| Interval | Approx. duration | Recorded boundary |
|---|---:|---|
| 19:53:53–19:54:07 | 14 s | Player scripts parsed/built; player `base.js` reaches a 677,531-node AST and is admitted to the AST cache |
| 19:54:07–19:54:22 | 15 s | AST execution/global synchronization after the large player AST is already available |
| 19:54:22–19:54:54 | 32 s | Subsequent inline player script completes |
| 19:54:54–19:55:09 | 15 s | Next inline player script completes |
| 19:55:09–19:55:24 | 15 s | Next inline player script completes; document becomes interactive |
| 19:55:24–19:55:44 | 20 s | Lifecycle/page-show work |
| 19:55:44–19:55:54 | 10 s | Final lifecycle work; `after_scripts` |

At least about 107 seconds lie after the large player AST has been admitted.
That makes AST execution and lifecycle-triggered JS work the direct bottleneck.
AST build contributes approximately the first 14 seconds and increased
footprint from 457 MB to 784 MB across the player script stages, but cannot
account for the remaining long intervals.

### 3.4 Why the wait is serialized

`execute_script_task_queue` in `radiant/script_runner.cpp` iterates one ready
task at a time, then the document runner executes its lifecycle phases in
order: post-DOM scripts, `interactive`, deferred scripts,
`DOMContentLoaded`, async-ready scripts, `load`, and `pageshow`. No second
task runs concurrently with a current task.

Further, `layout_iframe` in `radiant/layout_block.cpp:layout_iframe` loads and
lays out the embedded document synchronously. The outer page cannot finish
that layout operation until the inner document's serialized script runner has
completed.

### 3.5 Classification

| Candidate | Finding |
|---|---|
| Network | Not the post-document bottleneck. The observed HTTP fetch is sub-second; the long interval begins in the script runner. |
| AST parsing/building | Material; roughly the early ~14 seconds in the representative trace. The current runner no longer adds a selection-only parse. |
| AST interpretation | Dominant. More than 100 seconds follow AST availability in sequential script and lifecycle work. |
| MIR | Not the player-document execution tier. The log explicitly selects and uses the AST executor. |
| Sequential loading | Yes. Sequential script/lifecycle queues make each interval additive; synchronous iframe layout propagates the delay to the parent. |

## 4. GitHub Brotli: the 6.4 GiB footprint

### 4.1 Stage breakdown

`test_radiant_online_view_brotli_repo_lambda.log` reports the following
physical footprint progression.

| Stage | Footprint | Change from previous stage |
|---|---:|---:|
| Process start | 88 MB | — |
| CSS parsed | 269 MB | +181 MB |
| Initial CSS cascade complete | 1,913 MB | +1,644 MB |
| JavaScript begins | 1,917 MB | +4 MB |
| JavaScript phase complete | 4,441 MB | +2,524 MB |
| Temporary script-phase peak | 6,394 MB | — |
| Post-script full CSS cascade complete | 6,460 MB | +2,019 MB |
| Layout/render complete | 6,578 MB | +118 MB |
| Normal document cleanup | 4,656 MB | -1,922 MB |
| Mempool cleanup | 1,097 MB | -3,559 MB |

The final `[PEAK_FOOTPRINT]` is 6,897,966,808 bytes, or 6.42 GiB. The absolute
peak occurs after post-script recascade/layout, while the script phase itself
already reaches a temporary 6,394 MB peak.

### 4.2 It is not the script cache

The shutdown cache summary is only 5,087,218 bytes total:

| Retained cache component | Bytes |
|---|---:|
| Source | 4,215,011 |
| AST | 23,904 |
| MIR | 848,303 |
| Total peak | 5,087,218 |

The script cache is therefore conclusively not the multi-gigabyte owner.
Likewise, `memtrack` reports a clean shutdown and about 310 MB peak tracked
usage. That does not contradict the physical footprint: the tracker does not
cover all allocator pools, JIT/runtime allocations, or platform accounting.

### 4.3 JavaScript/module contribution

The GitHub document first selects the AST backend for a 65,062-node script.
It subsequently loads a broad module graph, including GitHub Elements and
Primer/React-related modules. The log records module ASTs up to 154,729 nodes,
many module-cache admissions, and MIR JIT optimization for module paths.

This explains the +2.52 GiB script-phase growth and the 6.394 GB transient
peak as a mixed execution phase: JavaScript ASTs, runtime objects, imported
module compilation/JIT artifacts, and work that JavaScript causes in the DOM
and layout pipeline. It does **not** show that one 1 MiB source file expands
to 6.4 GiB by itself.

One `layout_html_doc` operation also takes 69.1 seconds during the script
phase. The saved log proves that it occurs while scripts run; it does not
provide enough nested timing to state which specific JS operation triggered
it.

### 4.4 CSS cascade and style-epoch retention

The largest repeatable allocation signature is CSS:

1. Parsing stylesheets reaches 269 MB.
2. The initial full cascade adds 1.64 GB in about 2.35 seconds.
3. JavaScript reports only two DOM mutations.
4. The load path clears cascaded styles across the whole DOM and performs a
   second full cascade, adding another 2.02 GB.

The post-script path is `load_lambda_html_doc_profiled` in
`radiant/cmd_layout.cpp`: when `DomDocument::js.mutation_count > 0`, it calls
`clear_load_stylesheet_cascade_visitor` for the full tree and then
`apply_load_css_cascade`. This is a full re-resolution rather than an
incremental change.

`apply_stylesheet_to_tree` in `radiant/css_cascade.cpp` visits every element
and loops through every rule in each stylesheet. The resulting cost therefore
scales with both document size and stylesheet rule count.

The style epoch mechanism retains canonical style trees and their recipe
entries in the current epoch pool:

- `style_epoch_create_entry` clones or creates a `StyleTree`, applies the
  recipe, and stores both the tree and recipe in `style.canonical.epoch.pool`.
- `style_epoch_try_release` can destroy an epoch pool only once that epoch is
  no longer current and has no bound references.

Consequently, unique canonical style recipes from a cascade remain allocated
for the current document epoch. This is a concrete retention mechanism that
can amplify the footprint of a very large page and a full recascade. The
large drop at cleanup strongly confirms that pooled allocations own much of
the peak.

The existing trace does **not** record `StyleEpochStats` or per-pool reserved
bytes. It therefore supports the conclusion that CSS cascade/style pools are
a major owner, but does not prove how much of the 3.66 GB across the two
cascades belongs to canonical trees versus other CSS/DOM allocations.

### 4.5 Classification

| Candidate | Finding |
|---|---|
| Retained script cache | Excluded: about 5.1 MB total. |
| JavaScript/module graph | Major contributor: +2.52 GB through an AST/MIR mixed module phase, with a 6.394 GB transient peak. |
| Initial CSS cascade | Major contributor: +1.64 GB. |
| Two-mutation recascade | Major contributor: +2.02 GB because it is whole-document work. |
| Layout and render after recascade | Small incremental contribution: +118 MB. |
| Leak proven by this run | No. Cleanup releases roughly 5.48 GB, and `memtrack` finds no tracked leak. A long-lived pool-retention or allocation-efficiency defect remains possible and requires allocation-class evidence. |

## 5. Next evidence required

No behavior change should be made from this record alone. A targeted,
release-build reproduction should add measurement only:

1. Per document-script task timings for selection parse, executable parse,
   AST build, MIR/JIT, execution, microtask flush, and global synchronization.
   The current `RADIANT_JS_TASK_TIMING` facility is debug-only and was absent
   from these artifacts.
2. `StyleEpochStats` plus reserved/used bytes for every CSS memory pool at
   the pre-script and post-script cascade boundaries.
3. Per-memory-role accounting for JS heap/runtime, JIT code/data, DOM, CSS,
   and layout pools in each `MEMSTAGE` record.
4. A nested timing trace around the 69.1-second GitHub layout operation to
   associate it with a concrete JS call or DOM/CSS action.

These measurements will distinguish a high-but-expected page workload from
specific retained style recipes, module/JIT objects, or DOM structures that
should be released earlier.
