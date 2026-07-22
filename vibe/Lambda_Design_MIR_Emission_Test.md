# MIR Emission Testing — pattern checks, dynamic oracles, and size ratchets

**Status:** REVISED 2026-07-22 — MT1, MT2, MT4, MT5, and MT7 are the active
design. MT7 keeps **0% slack**, with manually lifted, platform-aware thresholds
in `test/mir/mir_budgets.json`. MT3 (the independent structural verifier) is
**DEFERRED** until MIR emission and the Stack API contract are stable and
finalized; its future direction is documented, but it is not an implementation
phase or current acceptance gate. MT6 remains optional behind a coupling spike.
MT8 and OQ3 (Python/Bash/Ruby scope) remain deferred.

**P1, P2 and P3 are IMPLEMENTED (2026-07-22).** The MT2 artifact contract, a real
MIR-Direct `--transpile-only`, the MT1 harness with its baseline drivers, the
MT7 zero-slack ratchet, an 11-fixture MT5 corpus, and the retirement of the
four orphaned `.transpile` files are landed and green inside
`make test-lambda-baseline` (3533/3533), verified against both a debug and a
release host. P3 added the MT4 forced-GC stress sweep, also in baseline
(3551/3551). P4 (the optional MT6 spike) is all that remains. See §8 for the
P1 record, §9 for P2 -- including the ratchet telemetry being invisible in
release builds (§9.2) and a C2MIR-vs-MIR-Direct optimization gap (§9.3) -- and
§10 for P3, which surfaced real pre-existing use-after-free bugs (§10.2).

**Date:** 2026-07-22

**Scope:** unit and regression testing of MIR **emission** (AST → MIR lowering)
for Lambda (`lambda/transpile-mir.cpp`) and LambdaJS (`lambda/js/js_mir_*.cpp`)
over the shared emitter (`lambda/mir_emitter_shared.hpp`), with the Stack API
frame/rooting/scalar-home protocol as the first-class subject. Python, Bash,
and Ruby front-ends inherit the harness mechanics (they already have analogous
dump paths) but are out of initial scope. MIR→native codegen (the vendored MIR
generator) is out of scope except as an execution mode in MT8 differential
lanes. The active design provides curated emission-pattern coverage, dynamic
forced-GC stress, and a size/shape ratchet; exhaustive structural verification
is explicitly future work under MT3.

**Companion documents:**

| Document | Role relative to this design |
|---|---|
| `vibe/Lambda_Design_Stack_API.md` | The contract under test — 27 numbered required invariants (lines 1399–1465); §8.9 verifier reject-list |
| `vibe/Lambda_Design_Stack_Frame.md` | SF1–SF20 side-stack/frame lifetime architecture; SF6/SF17 watermark balance |
| `vibe/Lambda_Design_Stack_Rooting.md` | CR1–CR8 safepoint-current rooting; §5.2 write-through oracle; §10.2 forced-GC stress gate |
| `vibe/Lambda_Stack_JS_MIR.md` | R0–R10 — the shadow-copy rooting regression (58→440 insns on the `frameReview` probe); the motivating evidence for MT7 |
| `vibe/Lambda_Design_Stack_Frame_JS.md` | JO1–JO13 JS residue ledger; JO7/JO8/JO10/JO13 are emission-level and feed MT5 |
| `vibe/editing/Radiant_Editor_Stage4C.md` | Root causes of the five Stage-4C LambdaJS bugs (numeric inference, `this`-capture, module-var write-through) — seed regressions for MT5 |
| `vibe/Lambda_Design_MIR_Cache.md` | Module cache interplay — a cache hit skips emission entirely, so the harness must disable it (§3.1) |

---

## 1. Problem: three bug classes current coverage cannot see

All existing Lambda/JS gates assert **program behavior** (stdout goldens,
test262/Node pass lists). Emission has no first-class coverage, and three
historical bug classes demonstrate three distinct blind spots:

1. **Latent structural violations.** BUG-001 (JIT locals not rooted across
   `array_end`→GC) passed every `.ls` golden because GC rarely fires at the
   vulnerable safepoint. The u64-at-`INT64_MAX` segfault (the retired `push_l`
   sentinel collision) was likewise invisible until the exact boundary value
   flowed through. Behavioral goldens sample the state space; protocol
   violations need to be checked in the emitted instruction stream or under
   forced GC.

2. **Emission drifting from emitter belief.** The emitter has strong
   self-checks — `em_validate_call_abi` (`lambda/mir_emitter_shared.hpp:698`),
   `em_finalize_function_metadata` (`:2492`), `em_finalize_scalar_homes`
   (`:2225`), all `abort()` on inconsistency — but they validate the emitter's
   **own bookkeeping**, not the instructions it emitted. The failure modes that
   actually shipped were exactly "emission disagrees with belief": R7 (two
   rooting emission paths drifting), R8 (root stores preceding register init),
   and the `fn_member` pointer-reconstruction bug now pinned by
   `test/test_item_repr_gtest.cpp`. No check that lives inside the emitter can
   catch this class; only an independent reader of the final MIR can. MT1 pins
   selected historical shapes now. MT3 is the eventual general solution, but
   this coverage gap is accepted until emission and the Stack API are stable
   enough that an independent verifier will not encode transitional forms.

3. **Quality (size/shape) regressions.** The shadow-copy rooting episode
   emitted **correct** MIR that was 7.6× larger (58→440 insns, 84% of the
   growth being root copy/store pairs) and 2× slower on Test262. Every
   behavioral and structural gate stayed green. Only instruction/slot counts
   catch this class.

## 2. Inventory: what already exists (measured 2026-07-22)

### 2.1 Textual MIR dumps

| Front-end | Current trigger | Current path | Current stage | Build gate |
|---|---|---|---|---|
| Lambda | always unless `--no-log` | `temp/mir_dump.txt`; `LAMBDA_MIR_DUMP_PATH` overrides | after emitter/frame finalization, before `MIR_finish_func` | `#ifndef NDEBUG` — `lambda/transpile-mir.cpp:14430` |
| JS | `JS_MIR_DUMP=1` | fixed `temp/js_mir_dump.txt` | after `transpile_js_mir_ast`, which has already called `MIR_finish_func`/`MIR_finish_module` | `#ifndef NDEBUG` — `lambda/js/js_mir_entrypoints_require.cpp:766` |
| TS | `JS_MIR_DUMP=1` | fixed `temp/ts_mir_dump.txt` | likewise post-finish | same file `:275` |
| Python / Bash / Ruby | always in debug | `temp/py_mir_dump.txt` etc. | frontend-specific | `lambda/py/transpile_py_mir.cpp:7820` and siblings |

Facts that shape the design:

- The dump API is **`MIR_output(ctx, FILE*)`** (`include/mir.h:603`) — whole
  context, all modules, `FILE*` only. Lambda currently writes before
  `MIR_finish_func`/`MIR_finish_module`; JS/TS currently write after them.
  Therefore the present dumps are not stage-equivalent, and dump-derived counts
  cannot yet be compared blindly with pre-finish emitter telemetry.
- Every debug run truncates the default path, so concurrent tests **must**
  redirect via `LAMBDA_MIR_DUMP_PATH` (the code comment at
  `transpile-mir.cpp:14434` already warns about this; `test_item_repr_gtest`
  already does it). JS/TS do not currently honor a private path and must gain
  one in MT2; simply removing `#ifndef NDEBUG` is insufficient.
- `--no-log` is the master disable for optional MIR testing instrumentation.
  Lambda's current debug dump already follows
  `log_default_category->enabled`; JS/TS currently key only on `JS_MIR_DUMP`
  and Python/Bash/Ruby currently dump unconditionally in debug. Every dump site
  must be brought under the same master gate in MT2. Environment variables
  never override `--no-log`.
- JS runs must also set `LAMBDA_DISABLE_MIR_CACHE=1` — a module-cache hit
  skips emission and the dump is stale/absent (recipe from
  `Lambda_Stack_JS_MIR.md` §18).
- There is **no MIR-Direct dump-without-execute CLI mode**: `--transpile-only` is parsed
  (`lambda/main.cpp:4812`, `LAMBDA_C2MIR`-gated) but ignored by
  `run_script_file` (`lambda/main.cpp:820–827`). Although the dump currently
  lands before execution, relying on that while accepting a crashing child can
  let a partial dump satisfy substring checks. P1 makes compile-only real for
  Lambda; the harness never accepts nonzero exit codes by default.

### 2.2 Existing MIR-assertion precedent (ad hoc)

`test/test_item_repr_gtest.cpp` `MirMemberAccessKeepsContainerItemUnmodified`
(~lines 383–420): sets a private `LAMBDA_MIR_DUMP_PATH`, runs
`./lambda.exe <script>` via `shell_exec`, scans dump lines for
`call\tfn_member_p, fn_member,`, asserts neighbors contain no `\tand\t`
(pointer reconstruction), and `GTEST_SKIP()`s when no dump appears (release
`lambda.exe`). This is a hand-rolled one-off of exactly the harness MT1
generalizes.

### 2.3 Orphaned fixtures

`test/lambda/*.transpile` (`tail_call`, `unboxed_sys_func`,
`sys_func_native_math`) hold JSON of shape `{"expect": [...], "forbid": [...]}`
describing substrings expected/forbidden in transpiled output. **No consumer
exists** (verified by repo-wide grep) — the format was invented for the C2MIR
`--transpile-dir` inspection path and abandoned. MT1 revives and extends it.

### 2.4 Emitter self-checks (the non-independent oracle)

`em_validate_call_abi` `:698`, `em_require_rep` fail-closed `:684`,
`em_finalize_scalar_homes` unresolved-fixup abort `:2238`,
`em_finalize_function_metadata` `:2492` (public-must-be-checked, scalar-home
ABI consistency, final-pointer-operand, fixup-escaped-prefix), plus the native
non-LIFO frame-restoration error (`lambda/runtime/side_stack.c:256`) and the
CR4 no-GC-scope allocation assert (`lambda/runtime/gc/gc_heap.c:44`). These
stay — they are the first line — but per §1.2 they are not sufficient.

### 2.5 Dynamic oracles (fully wired, currently on-demand)

- `LAMBDA_GC_FORCE_EVERY=N`, `LAMBDA_GC_FORCE_SEED`/`LAMBDA_GC_FORCE_ONE_IN`
  (deterministic random stress), `LAMBDA_GC_POISON_FREED=1` —
  `lambda/runtime/lambda-mem.cpp:318–363`, hooked at every allocation entry in
  `gc_heap.c`.
- `LAMBDA_MIR_ROOT_MODE=write-through` differential oracle
  (`Lambda_Design_Stack_Rooting.md` §5.2).
- `make test-gc-rooting` / `-core` / `-python` (Makefile:1007–1054): runs
  `test/js/regression_side_stack_frame_gc.js` under exact-root forced-GC,
  randomized forced-GC, and `--mir-interp` exact-root collection, plus the static
  `utils/check_gc_effects.py` / `check_gc_root_hazards.py` sweeps.

### 2.6 Telemetry

`LAMBDA_MIR_LOG_FRAME_SLOTS=1` (`mir_emitter_shared.hpp:2543`) reports
per-function roots / root stores / scalar homes / number scratch / classified
`MAY_GC`–`NO_GC` safepoint counts. JS additionally has emitted-insn volume
counters (`js_mir_entrypoints_require.cpp:160–182`) consumed by
`test_js_transpile_timing_gtest` under `JS_TRANSPILE_TIMING`.

### 2.7 Determinism facts (what goldens can and cannot be)

Registers (`em_new_reg` → `"<prefix>_<n>"` sequential counter), labels
(context-monotonic `L<n>`), and proto/import names
(signature-derived or per-site counters) are **deterministic** per compile on
a fresh context. But **12 sites in `lambda/transpile-mir.cpp` bake raw host
pointers into `mov` immediates** — interned string literals (`:1397`), the
module `type_list` (`:2043`), `&LIT_TYPE_*` statics (`:10271–10305`),
`info->func_ptr` (`:11527`), and friends. Under ASLR these differ per process;
`type_list`/interned strings differ even per compile in one process.

**Consequence (design rule):** script-level MIR assertions must match **names
and instruction shapes, never raw immediates**, and full-file goldens of
script dumps are rejected. (Synthetic emissions through the shared emitter
alone bake no pointers — see MT6 — so byte-goldens are allowed only there.)

---

## 3. Design ledger

### MT1 — `mir-check` pattern harness over the dumps. *(DECIDED)*

A shared GTest helper (`test/test_mir_check_helpers.hpp`) providing:

1. **Compile-and-dump:** run `./lambda.exe <snippet.ls>` (or
   `./lambda.exe js <snippet.js>`) as a subprocess with a per-test private dump
   path (JS: plus `JS_MIR_DUMP=1 LAMBDA_DISABLE_MIR_CACHE=1`). Normal fixtures
   require exit code `0`, a non-empty dump, and the expected module/function;
   an expected-failure fixture must declare its allowed exit code explicitly.
   Dedicated MT1 children deliberately do **not** pass `--no-log`; the driver
   rejects a configuration that tries to combine `--no-log` with MIR checks.
   `GTEST_SKIP()` is permitted only during the MT1→MT2 transition and is
   removed before the harness joins baseline.
2. **Canonical stage:** v1 checks the **finalized emitted MIR** — after every
   function and the module have passed `MIR_finish_func`/`MIR_finish_module`,
   before linking/generation. Both frontends finish individual functions while
   lowering, so one whole-context dump cannot truthfully represent every item
   at a common pre-finish point. A raw per-function pre-finish capture would be
   a different future artifact and is not part of MT1/MT7.
3. **Function scoping:** the dump is whole-context; the parser slices it into
   per-item texts keyed by `(module, function, occurrence)`, not function name
   alone. Assertions select a module and function (`main`, user fns, `js_main`)
   and may select an occurrence when names repeat — the CHECK-LABEL analogue.
   An `in_func` selector may contain `#`, which matches a run of one or more
   digits, because both frontends embed a source-offset id in generated symbol
   names (`_accumulate_371`, `_js_helper_752_body`); see §8.2. A selector with
   no `#` is matched exactly. `*` selects the whole artifact.
4. **Assertion forms**, extending the orphaned `.transpile` JSON, one sidecar
   per snippet (OQ1 DECIDED: JSON `.mir-check` sidecar files):
   - required `schema_version`, initially `1`; unknown fields fail closed;
   - optional `description`, `expect_exit_code` (default `0`), and `args`
     (extra child arguments; `--no-log` is rejected there, per item 1);
   - `expect` / `forbid` — unordered substring presence/absence;
   - `expect_seq` — ordered line subsequence (FileCheck `CHECK`-style), each
     entry optionally `{"pattern": "...", "next_line": true}` for a
     `CHECK-NEXT` analogue;
   - `count` — `{"pattern": N}` exact, or bounded with
     `{"pattern": {"min": N}}` / `{"pattern": {"max": N}}`;
   - `in_module` / `in_func` / optional `occurrence` — scope selector per
     assertion group (default is the script's main module and `main`).
   Patterns are plain substrings over the tab-separated dump lines (matching
   the item_repr precedent); no regex engine is needed initially. Relational
   claims such as “the same register reaches both instructions” are not
   represented as substrings; they either receive a small named-capture
   extension later or remain future MT3 work.
5. **Diagnostics:** failures print the sidecar path, selected
   module/function/occurrence, matched line numbers, and the scoped MIR slice.
   Missing/ambiguous scopes and malformed JSON are test failures, not skips.
6. **Drivers:** `test/test_mir_emission_gtest.cpp` auto-discovers
   `test/mir/lambda/*.ls` + sidecars; `test/test_js_mir_emission_gtest.cpp`
   auto-discovers `test/mir/js/*.js` + sidecars. Both registered in
   `build_lambda_config.json` (lambda suite, `category: baseline`,
   `requires_lambda_exe: true`, `dependencies: ["lambda-lib"]`,
   `libraries: ["gtest","gtest_main","rpmalloc"]`), run by `test/test_run.js`
   like every other suite.
7. **Migrations:** port the item_repr MIR test into the harness as a fixture;
   convert the three orphaned `.transpile` fixtures (their `expect` strings
   target C2MIR C output — re-derive equivalent MIR-Direct patterns).

Repository-policy note: test-only C++ harnesses may use `std::` containers and
strings. Code under `lambda/`, including production emitter/runtime changes,
continues to use the project `lib/` equivalents. The matching-`.txt` rule
applies to Lambda regression scripts added under `test/lambda`; the emission
fixtures under `test/mir/lambda` are governed by their `.mir-check` sidecars
and do not require behavioral `.txt` companions.

### MT2 — promote dumps to env-opt-in in all builds. *(DECIDED)*

Establish one explicit artifact contract, available in debug and release:

- `LAMBDA_MIR_DUMP_PATH` set ⇒ write the current frontend's canonical emitted
  MIR to that exact path, even in release, **provided logging is enabled**.
  Lambda, JS, and TS all honor it.
- `--no-log` takes precedence over `LAMBDA_MIR_DUMP_PATH`, `JS_MIR_DUMP`,
  `LAMBDA_MIR_LOG_FRAME_SLOTS`, and every other optional MIR test/dump switch.
  No MIR dump is opened or truncated, no optional emission metadata is
  collected, and no MT1/MT7 child-side check runs. This keeps the many normal
  and performance-oriented test commands that use `--no-log` free of MIR-test
  I/O and accounting overhead.
- With logging enabled and an explicit dump requested, failure to open/write
  the path is a compilation error in test mode, not a warning followed by
  stale-artifact reuse.
- The existing debug default paths remain developer conveniences. Legacy
  `JS_MIR_DUMP=1` may retain `temp/js_mir_dump.txt` / `temp/ts_mir_dump.txt`,
  but the tests never use those shared paths.
- The v1 explicit-path hook is placed at the canonical finalized stage defined
  by MT1 for all three in-scope frontends. Existing developer-only debug dumps
  may retain their earlier diagnostic timing, but tests never consume them.
- Python/Bash/Ruby remain outside the MT1/MT7 private-path and release-dump
  scope, but their existing debug dump paths are updated to obey the universal
  `--no-log` master gate. Their broader artifact contract waits for OQ3.

The master switch applies to **optional emission-test instrumentation**. The
emitter's mandatory fail-closed correctness invariants (`abort()` checks in
§2.4) remain active under `--no-log`; suppressing logs must not turn malformed
MIR into accepted MIR.

MT2 includes suppression coverage for every current frontend dump site. Launch
the applicable Lambda/JS/TS/Python/Bash/Ruby frontend with its dump/telemetry
switches set **and** `--no-log`, then assert from the parent test that no dump
or telemetry artifact was created. Where a guest frontend is not built in the
standard host, its existing guest test target owns the assertion. The parent
is testing the master switch; no child-side MIR check runs.

Rationale: `make build-test` pairs debug test exes with a **release**
`lambda.exe` when `.lambda_release_build` is present, which is exactly why the
item_repr test needs `GTEST_SKIP()`. Emission tests must not silently skip in
the configuration CI actually runs.

Make `--transpile-only` real for Lambda MIR-Direct in P1 — thread the flag
through `run_script_file` to a compile-only MIR-Direct path. JS fixtures still
execute in v1 and therefore must exit successfully; a JS compile-only entry
point can be added later if execution cost or side effects justify it.

The active MT1/MT7 corpus is deliberately single-module. A multi-artifact dump
directory/callback for imported modules and batch processes is deferred with
MT3; one fixed environment path cannot represent many compilations safely.

### MT3 — independent structural verifier for Stack API invariants. *(DEFERRED)*

MT3 is intentionally unscheduled until MIR emission and the Stack API contract
are stable and finalized. Building it now would either pin transitional
instruction shapes or duplicate analysis that is still moving. MT1, MT4, and
MT7 provide targeted coverage in the meantime; they do not claim to close the
general emitter-belief-vs-emission gap described in §1.2.

The direction remains a **test-only binary**; verifier code does not ship in
`lambda.exe`. Its detailed design is re-opened rather than treated as decided
when all of these entry criteria hold:

1. Lambda and JS use the same documented finalized emission stage.
2. Stack frame, rooting, scalar-home, and call-ABI forms are finalized and no
   longer carrying migration residue.
3. A spike proves that the selected artifact can be loaded losslessly with
   `MIR_scan_string`, including reliable error capture. `MIR_scan_string`
   returns `void`, so successful loading and diagnostics must be made explicit.
4. The compiler exposes a compile-only live-context visitor if text rescanning
   is not lossless. The current `runner_init` + `run_script` path is not such a
   fallback because it returns `Input*`, not the `Script`/MIR context.
5. Multi-compilation processes have a real artifact sink — for example a dump
   directory with unique `(frontend, module, pid, sequence)` filenames or an
   emission callback. Reusing one `LAMBDA_MIR_DUMP_PATH` would overwrite every
   earlier script in a test262 or module batch.

Future work is split by actual analysis complexity:

- **Local structural pass:** classify side-stack memory operands via
  `offsetof(Context, ...)`; classify calls from the runtime registry; check
  region bounds, zero initialization, obvious cleanup-order violations,
  zero-root publication, and public-wrapper use.
- **CFG/provenance pass:** build basic blocks and successors, then use
  dominance/reaching-definition or equivalent state propagation to prove
  all-return watermark restoration, scalar-home origin/adopt ordering,
  discard isolation, tail-home forwarding, and capture rehoming. A linear
  instruction scan is not sufficient for these claims because a branch can
  bypass a syntactically earlier restore or adoption.
- **Belief-vs-emission cross-check:** compare verifier-derived counts with a
  machine-readable emitter metadata artifact with explicitly documented
  timing. Frame-shape counts may be recorded by emitter finalizers, while MIR
  instruction counts come from the finalized dump; unlike quantities are not
  compared as if they described the same transformation stage.
  Parsing shared `log.txt` is rejected; any temporary logging during the spike
  uses a process-private `LAMBDA_LOG_FILE`.

The formerly proposed nightly “per-script dump” sweep belongs here and is
therefore also deferred. When MT3 resumes, that lane will compile existing
`.ls`, `.js`, and test262 inputs into unique artifacts and feed them to the
verifier, broadening structural coverage without `.mir-check` sidecars. It is
not part of P1–P4 and no current gate depends on it.

### MT4 — liveness stays with the dynamic oracles, promoted to a gate. *(DECIDED)*

The one genuinely dataflow-based invariant — #7 safepoint currency (with #8
forbidding over-rooting) — is **deliberately excluded from the active static
design**.
Re-implementing CFG liveness independently would duplicate
`em_finalize_semantic_root_write_back` (`mir_emitter_shared.hpp:1444`), the
hardest code in the emitter, and rot alongside it. Instead, exact-root
`FORCE_EVERY=1` + `POISON_FREED=1` strongly stress-tests executed safepoints:
a missing root that becomes otherwise unreachable is collected and its later
use fails deterministically. This is behavioral stress over the selected
corpus, not a proof over every CFG path.

Work items:

- extend `make test-gc-rooting-core` to sweep the MT5 corpus and the Stage-4C
  regression scripts under the three existing exact-root modes (JIT forced,
  deterministic randomized forced, `--mir-interp` forced);
- promote that sweep from on-demand target to baseline-adjacent gate (same
  tier as `test-lambda-baseline`, given runtime cost measured first);
- keep `LAMBDA_MIR_ROOT_MODE=write-through` as the bisection oracle when the
  sweep fails.

Invariant #8 (no redundant/over-root currency) remains a quality property in
the active design. MT7 ratchets root-store counts and catches growth on its
probe set, but it does not prove that every retained store is semantically
necessary.

### MT5 — seed corpus: the Stack API protocol as snippets. *(DECIDED)*

~15 Lambda + ~8 JS micro-scripts under `test/mir/`, each with a `.mir-check`
sidecar (MT1) and swept by MT4. The v1 snippets are single-module and avoid
imports so one private dump path names exactly one compilation. Future MT3 may
reuse the corpus, but MT5 does not wait for it. Initial list:

*Scalar-home protocol (API #15–#21, #25–#27):*

1. fn returning `int64` → caller home donation + `box_int64_value` + adopt
   ordering;
2. nested calls → homes from the current activation, not forwarded;
3. self-recursive tail call → incoming home forwarded (`tail_call.transpile`
   modernized);
4. unused scalar result → discard scratch, no live home (#20);
5. error lane: `raise`/`?` with `CONTEXT_ERROR` scalar → adopt before publish;
6. **u64 at `INT64_MAX`** — the historical sentinel segfault, assert the
   `box_uint64_value` path (#25);
7. subnormal/tiny float → number-stack boxing residue (`push_d` path, per
   double-boxing v3 residue note);
8. datetime construction → `push_k`/heap only, never a number-frame pointer
   (#26).

*Frame shape (#12, SF6/SF17, CR):*

9. pure-int leaf fn → zero-root elision: forbid root-top stores;
10. closure capturing a scalar → `lambda_item_heap_rehome` present (#24);
11. BUG-001 shape: array literal + interleaved `MAY_GC` calls → root store
    precedes `array_end`;
12. deep-let fn → root slots zero-initialized before top publication.

*JS regressions (Stage 4C + JO ledger), each with its documented MIR
observable:*

13. numeric inference: `const x = userFn()` → **forbid** `it2d`/
    `js_profiled_it2d`+`d2i` on the call result (allow for `Math.*`);
14. `this`-capture: class field arrow in an IIFE → **expect** the `_js_this`
    store into the arrow's env `this` slot
    (`test/js/class_field_arrow_nested_this.js` shape);
15. module-var write-through: hoisted inner fn decl → **expect**
    `js_set_module_var` at the hoist site
    (`test/js/class_field_decl_capture_nested_new.js` shape);
16. JO7: generator with 70 `yield`s → resume-label `count` ≥ yield count
    (today silently truncated at the `gen_state_labels[64]` cap,
    `lambda/js/js_mir_context.hpp:460`).

### MT6 — helper-level unit tests of the shared emitter. *(DECIDED — gated on a spike)*

A minimal in-process harness: `MIR_init` → `MIR_new_module` →
`MIR_new_func_arr` → stand up a `MirEmitter` → drive `em_call_with_args`
(`:2685`) / `em_call_direct` (`:2775`) / `em_finalize_frame_prologue`
(`:1008`) / `em_adopt_scalar_item` (`:772`) with synthetic `MirCallOptions`,
then `MIR_output_item` into a per-test file under `./temp/` and compare. A
portable project helper may replace the file later; `open_memstream` is not
used because it is unavailable on Windows.

Why this tier may earn its keep despite MT1 overlap and future MT3:

- it is the true **unit** granularity — one test pins Lambda *and* JS, since
  both route through these exact functions;
- synthetic emissions bake **no host pointers** (§2.7's 12 sites are all in
  `transpile-mir.cpp` AST lowering), so byte-stable full goldens are legitimate
  **within one platform emission profile**. Prefer platform-neutral cases;
  where the shared emitter intentionally differs (for example the Windows
  side-stack prologue), use an explicit platform-specific golden.

Gate: a half-day spike confirming `MirEmitter` stands up outside a full
transpiler (import metadata defaults via `em_ensure_import` may need a stub
registry). If the coupling is too deep, drop this optional tier; no active
acceptance gate depends on MT6, and MT1 still covers the shared paths through
real Lambda/JS snippets.

### MT7 — emission-size ratchet. *(DECIDED — 0% slack)*

**Probe set (decided 2026-07-22): two tiers, both listed in the config.**
(a) Purpose-written probes — the `frameReview` probe from
`Lambda_Stack_JS_MIR.md` plus ~6 MT5 corpus functions per language — tracked
**per-function**: emitted-insn count plus `LAMBDA_MIR_LOG_FRAME_SLOTS` shape
counts (roots, root stores, scalar homes, safepoints). (b) **Picked existing
scripts from `test/lambda/*.ls` and `test/js/*.js`** — a handful of stable,
representative ones covering closure-, array-, generator-, and call-heavy
shapes — tracked as **whole-module insn totals**, so each stays one budget
line. Whole-module totals still intentionally change when a fixture's internal
functions are refactored; the benefit is compact configuration, not immunity
to refactoring churn.
All thresholds are **exact values** in one checked-in config:
**`test/mir/mir_budgets.json`** (entries: probe path, scope = function name
or module total, canonical finalized stage, threshold values).

**Policy (decided 2026-07-22): 0% slack.** Any increase in emitted MIR over a
recorded threshold fails the gate; landing it requires human review of the
emission diff and a **manual threshold lift** in the config — the commit that
grows emission carries the budget edit, so the growth is visible and
reviewable in the same diff. On a decrease, the gate passes and prints the
tightened values; committing them is the manual re-baseline that keeps
budgets pinned to actuals. The test never edits its own config.

Zero slack is applied **within a platform emission profile**, not by assuming
that all platforms emit identical MIR. Windows currently emits additional
side-stack commitment instructions, so the config either records an explicit
Windows threshold alongside the default threshold or marks a narrowly defined
platform-prologue normalization. Silent platform-dependent slack is rejected.
Before enabling the gate, each probe is measured repeatedly in release and
debug test configurations on every supported CI platform; nondeterministic
probes are removed rather than padded.

The Windows difference is an OS virtual-memory requirement, not different
Lambda/JS semantics. On macOS/Linux, an anonymous `mmap(PROT_READ|PROT_WRITE)`
reserves the full side-stack range and the kernel demand-pages it when touched;
generated code only needs the common logical-limit check. Windows
`VirtualAlloc(MEM_RESERVE, PAGE_NOACCESS)` reserves addresses but leaves the
pages inaccessible. Before advancing a side-stack watermark into new pages,
the generated prologue must also compare against `side_*_commit_limit` and, if
needed, call `lambda_side_stack_ensure` to `MEM_COMMIT` pages. Committing the
whole reservation eagerly would consume Windows commit charge for untouched
pages. Those compare/branch/call instructions explain the platform-specific
MIR count; function bodies and value semantics remain the same.

**Gating (decided 2026-07-22): runs inside `test-lambda-baseline`, covering
both the Lambda and JS probes.** Driver: `test/test_mir_ratchet_gtest.cpp` →
`test_mir_ratchet_gtest.exe`, registered in the `lambda` suite with
`category: baseline`, so `make test-lambda-baseline` (`test/test_run.js
--target=lambda --category=baseline`) runs it alongside the behavioral
goldens — an emission-size increase is a baseline failure, not a nightly
curiosity. Two consequences: (1) **MT2 becomes a hard prerequisite** —
baseline pairs test exes with a release `lambda.exe` when
`.lambda_release_build` is present, so the dumps must be env-available in all
  builds; (2) the driver uses a per-probe private `LAMBDA_MIR_DUMP_PATH`, a
  private `LAMBDA_LOG_FILE` for frame telemetry, and
  `LAMBDA_DISABLE_MIR_CACHE=1`, since baseline runs parallel. Instruction
  counts come from the finalized dump. Emitter telemetry supplies roots, root
  stores, scalar homes, scratch, and safepoints; its pre-finish instruction
  field is not cross-compared with the finalized dump count. Dedicated MT7
  children never pass `--no-log`; all other baseline children that do pass it
  remain outside the MIR ratchet and emit no MIR artifacts.

JS volume counters and `JS_TRANSPILE_TIMING` already exist
(`lambda/js/js_mir_entrypoints_require.cpp:160–182`) — this is wiring plus
the config file. This is the only layer that catches §1.3
(correct-but-bloated); at 0% slack inside the baseline gate it turns the
one-off 58→440 forensic measurement into a standing, review-gated ledger of
emission size.

### MT8 — differential lanes. *(DEFERRED — documented only)*

Decided 2026-07-22: worth keeping on record, **not implemented now** — its
value is benchmarking, not gating. Motivating observation: some tests
currently run **faster under `--c2mir` than under MIR-Direct**; explaining
that requires digging in and comparing the MIR the two pipelines emit for the
same script. This section records the recipe for when that investigation
starts:

1. **C2MIR vs MIR-Direct:** run the `test/lambda` corpus under `--c2mir` and
   default MIR-Direct; diff stdout for semantics (CR7 keeps C2MIR alive
   precisely as this oracle), and compare per-function MIR plus timings for the
   performance-parity investigation. A C2MIR-side MIR dump hook is a
   prerequisite; the current MIR-Direct dump contract must not be assumed to
   expose C2MIR's generated MIR.
2. **JIT vs `--mir-interp`:** same emitted MIR, two executions — separates
   lowering bugs (reproduce in both) from MIR-codegen bugs (JIT-only). This
   is the exact bisection recipe used on the Stage-4C bugs
   (`JS_MIR_INTERP=1`).

When picked up: Makefile + small runner work; findings feed MT5 as new
fixtures and MT7 as new probes.

---

## 4. Invariant coverage map

This table distinguishes active regression evidence from future structural
proof. “Pattern” and “stress” mean coverage over the selected corpus, not an
exhaustive proof over all emitted functions or CFG paths.

| Contract | Active coverage | Future MT3 |
|---|---|---|
| API #1–#6 (analysis purity, one call path, one frame owner) | emitter construction-time self-checks (§2.4) | belief-vs-emission cross-check after forms stabilize |
| API #7 safepoint currency | MT4 exact-root forced-GC stress on executed paths | no static liveness pass currently planned |
| API #8 no redundant currency | MT7 root-store ratchet on probes; quality guard, not proof | optional future analysis if justified |
| API #9 cleanup order | selected MT1 patterns only | CFG cleanup-state pass |
| API #10/#11 entry/ABI isolation | existing `em_finalize_function_metadata`; selected MT1 patterns | call-target classification |
| API #12 zero-root elision | MT5 fixture 9 + MT7 counts | local structural pass |
| API #13/#14 lane contracts | emitter self-checks; MT5 fixture 5 exercises the error lane end-to-end | — |
| API #15–#17 adopt ordering, callee restore, all-producer coverage | MT5 fixtures 1–5 patterns + behavior | CFG/provenance pass |
| API #18 liveness-bounded homes | MT7 home-count ratchet + existing `em_finalize_scalar_homes` | — |
| API #19 finalized references | existing abort `:2238`; selected MT1 bounds patterns | provenance/bounds pass |
| API #20 discard isolation | MT5 fixture 4 | provenance pass |
| API #21 tail-home forwarding | MT5 fixture 3 | provenance pass |
| API #22 no scalar/root alias | selected MT1 patterns | region/provenance pass |
| API #23 float discriminator | MT5 fixture 7 pattern | — |
| API #24/#27 no activation escape | MT5 fixture 10 pattern + forced-GC behavior | capture-ownership provenance pass |
| API #25/#26 int64/datetime ownership | MT5 fixtures 6/8 | — |
| CR4 no-GC scopes | existing runtime assert (`gc_heap.c:44`) under MT4 sweeps | — |
| R-class (rooting bloat) | MT7 ratchet | — |
| JO7/JO10/JO13 (JS residue) | MT5 fixture 16 (JO7); JO10/JO13 get fixtures when their fixes land | — |
| Stage-4C bug classes | MT5 fixtures 13–15 | — |

## 5. Rollout

| Phase | Content | Notes |
|---|---|---|
| **P1 — DONE 2026-07-22** | MT2 canonical/private dump contract + real Lambda MIR-Direct compile-only + MT1 harness + first 5 MT5 fixtures | Landed green in `test-lambda-baseline`; see §8. The item_repr *window* assertion stayed in `test_item_repr_gtest.cpp` (§8.3) |
| **P2 — DONE 2026-07-22** | MT7 zero-slack ratchet (10 probes) + revived `.transpile` cases + MT5 grown to 11 fixtures | Green on debug and release hosts; see §9 |
| **P3 — DONE 2026-07-22** | MT4 forced-GC stress sweep over the corpus, cost-measured and promoted to baseline | 7.8s, stable across runs; see §10. Shadow mode remains diagnostic and ungated |
| **P4** | MT6 spike; drop it if coupling is deep or MT1 already supplies sufficient coverage | Optional helper-level unit tier |

MT3 and MT8 are deliberately unscheduled future work. No active phase or
acceptance gate depends on either one.

## 6. Decisions and open questions

- **OQ1 — sidecar format: DECIDED 2026-07-22.** JSON `.mir-check` sidecar
  files (language-agnostic — one harness serves Lambda/JS/Python; fixtures
  diff cleanly). Embedded C++ expectation tables rejected.
- **OQ2 — where a future verifier ships: DIRECTION DECIDED 2026-07-22.** If
  MT3 resumes, it stays a test-only binary and never ships inside
  `lambda.exe`. Artifact routing, scanner/live-context input, CFG scope, and
  nightly sweep mechanics are intentionally re-opened at that time.
- **OQ3 — Python/Bash/Ruby scope:** the harness mechanics carry over via
  their existing dump paths; Rooting CR6 keeps Python in compatibility mode,
  so MT3's precise-rooting checks don't apply there yet. Defer until the
  Python frame port (Track F) completes.

## 7. Non-goals

- Testing the vendored MIR generator (MIR→native). MT8's interp-vs-JIT lane
  *localizes* such bugs but this design does not add MIR-generator unit tests.
- Full-file goldens of script-level dumps (rejected — §2.7).
- Static liveness verification duplicating
  `em_finalize_semantic_root_write_back` (rejected — MT4).
- An independent structural verifier or full-corpus nightly dump sweep in the
  active rollout (deferred — MT3).
- Replacing behavioral goldens; all layers here are additive to
  `test-lambda-baseline` / test262 / Node gates.

---

## 8. Implementation record — P1 (2026-07-22)

### 8.1 What landed

| Area | Change |
|---|---|
| Shared artifact contract | New `lambda/mir_dump.h` / `lambda/mir_dump.cpp`: `mir_dump_instrumentation_enabled()` (the `--no-log` master gate), `mir_dump_explicit_path()`, `mir_dump_write_context()`, `mir_dump_finalized()`. Registered in `lambda-rt` |
| Lambda frontend | `transpile-mir.cpp` emits the canonical artifact **after** `MIR_finish_func`/`MIR_finish_module`. The pre-finish debug snapshot is retained as a developer diagnostic but now writes only the shared default path, never an explicit private one |
| JS/TS frontends | Both dump sites route through `mir_dump_finalized`, honor `LAMBDA_MIR_DUMP_PATH` in every build, and obey `--no-log`. This also removed two copy-pasted NULL-label pre-scans (a third would have been added for Lambda) |
| Guest frontends | Python/Bash/Ruby dumps now obey the `--no-log` master gate (still debug-only, still default-path — OQ3 unchanged) |
| Telemetry | `LAMBDA_MIR_LOG_FRAME_SLOTS` reads are gated on the master switch at both sites in `mir_emitter_shared.hpp` |
| Compile-only | `--transpile-only` moved out of the `LAMBDA_C2MIR` block and threaded through `run_script_file` → `run_script_mir(..., compile_only)`. It compiles the script and its whole import cone, writes the artifact, prints nothing, and exits 0 |
| Harness | `test/test_mir_check_helpers.hpp` — self-contained JSON reader, whole-context dump parser, sidecar validation, evaluator, diagnostics, fixture discovery |
| Drivers | `test/test_mir_emission_gtest.cpp`, `test/test_js_mir_emission_gtest.cpp`, both registered in the `lambda` suite (category `baseline`, `requires_lambda_exe`) |
| Fixtures | `test/mir/lambda/{scalar_home_donation,scalar_home_tail_forward,sized_int_boxing}`, `test/mir/js/{numeric_inference_call,hoisted_modvar_write_through}`, each with a `.mir-check` sidecar |

Result: `make test-lambda-baseline` = **3516/3516**, including Lambda MIR
Emission 4/4 and JS MIR Emission 3/3.

Every fixture pattern was derived from a real dump, never guessed. The
fail-closed paths were verified with a temporary negative-control fixture:
unmatched `expect`, unknown sidecar field, `--no-log` in `args`, wrong
`schema_version`, and a missing sidecar each fail with the sidecar path,
resolved scope, artifact path, and the scoped MIR slice.

### 8.1a Release-build behavior: verified by construction, not yet exercised

`mir_dump.cpp`/`mir_dump.h` contain no `NDEBUG` guards, and in every frontend
only the *default-path* fallback flag is debug-conditional — the
`mir_dump_finalized` call itself is unconditional. So a release build honors
`LAMBDA_MIR_DUMP_PATH` and writes no default artifact, as MT2 requires.
This was confirmed by inspection; P1 was developed and gated against a debug
`lambda.exe`. **The first P2 task is to run the MT1 suites against a release
`lambda.exe` end to end**, since MT7 rides on this contract and baseline can
pair debug test binaries with a release host.

### 8.2 Finding: both frontends embed source-offset ids in symbol names

The design assumed function names were stable scope keys. They are not:
Lambda emits `_accumulate_371` and JS emits `_js_helper_752_body`, where the
number derives from source position — so *editing a fixture's own comments*
renames its functions. Pinning ids would have made every fixture brittle in a
way that looks like an emitter regression.

Resolution: `in_func` accepts `#` for a digit run (§MT1 item 3), so fixtures
scope to `_accumulate_#` / `_js_anon1_#_body`. `#` matches digits only, which
also keeps `_twice_#` from matching the bound-entry variant `_twice_b_371`.

### 8.3 Finding: the item_repr assertion is relational, and stays put

`MirMemberAccessKeepsContainerItemUnmodified` asserts that the 12 lines
*before* each `fn_member` call contain no pointer-reconstruction ops. That is
a windowed/relational claim, and MT1 §item 4 explicitly leaves relational
claims out of the substring language. Measurement confirmed a blanket
`forbid` would be wrong rather than merely weaker: the reconstruction mask
`72057594037927935` legitimately appears twice elsewhere in the same `main`
(ordinary int boxing), so only the *windowed* form is sound.

The test therefore stays in `test_item_repr_gtest.cpp`, and its
`GTEST_SKIP()` was replaced with a hard assertion — MT2 guarantees the
artifact in every build, so a missing dump is now a contract failure rather
than a silent skip. Its JS/Lambda fixture slot was spent on a second
Stage-4C regression (`hoisted_modvar_write_through`) instead.

### 8.4 Finding: zero-root elision has no clean P1 fixture

MT5 #9 (zero-root elision, API #12) was dropped from P1. The obvious probe —
a pure-int leaf `fn square(x: int) int { x * x }` — still reserves two root
slots, because the multiply routes through the `fn_mul` import whose boxed
operands are rootable at that safepoint. That is the emitter behaving
correctly for a function that is *not* zero-root, not an elision failure. A
genuine zero-root shape needs a function with no `MAY_GC` call at all;
constructing one deliberately is P2 work rather than a guessed assertion.

### 8.5 Build-system hazard hit during P1 — FIXED

`make build-test` copies `lambda.exe` to `.lambda_build_backup.exe` before
building tests and `mv`s it back afterwards, but the restore fired on the
backup file merely *existing*. A stale backup from an earlier interrupted run
silently replaced a freshly built `lambda.exe` with an older binary, which
presented as "`--transpile-only` is an unknown option" long after that flag
was implemented — the symptom points at the source, not the build system, so
it costs real debugging time.

Fixed by clearing the backup at the start of `build-test`, which makes "the
backup exists" mean "this invocation created it". The save/restore pair is
otherwise untouched, so the legitimate release-host path still restores.
`clean` already removed the file. Verified by planting a stale backup with no
`.lambda_release_build` marker and confirming `build-test` discards it and
leaves `lambda.exe` intact; the release restore path was checked structurally
rather than by paying for a full release compile.

---

## 9. Implementation record — P2 (2026-07-22)

### 9.1 What landed

| Area | Change |
|---|---|
| MT7 ratchet | `test/mir/mir_budgets.json` (10 probes) + `test/test_mir_ratchet_gtest.cpp`, registered in the `lambda` suite at `category: baseline`. Over budget fails; under budget passes and prints the tightened values; the test never writes its config |
| MT7 profiles | Thresholds resolve `<platform>-<config>` → `<platform>` → `default`. Measured identical on debug and release hosts, so only `default` is populated today |
| Harness reuse | `compile_and_dump` took a `CompileOptions` struct so the ratchet reuses the MT1 machinery instead of duplicating it; added instruction counting and frame-slot telemetry parsing |
| `.transpile` retirement | All four orphaned fixtures (`tail_call`, `unboxed_sys_func`, `sys_func_native_math`, `proc/tail_call_proc`) removed and replaced with MIR-Direct fixtures |
| MT5 corpus | Grown from 5 to 11 fixtures (8 Lambda, 3 JS), adding TCO (functional and procedural), system-function specialization, the BUG-001 array-rooting shape, bound-wrapper scalar rehoming, and JS `this`-capture |

`make test-lambda-baseline` = **3533/3533**. The ratchet's fail/pass behavior
was verified by simulating both directions against the live emitter: an
artificially lowered budget produced `GREW module_insns 130 -> 138 (+8)` and
failed; an inflated one passed with a re-baseline report.

Probe selection followed the doc's rule that nondeterministic probes are
removed rather than padded: every candidate was measured three times before
being recorded, and all ten were stable.

### 9.2 Finding: the ratchet's telemetry did not exist in release builds

MT7 reads frame shape from `LAMBDA_MIR_LOG_FRAME_SLOTS`, which was emitted
with `log_info`. `lib/log.h:147` compiles `log_info` and `log_debug` to no-ops
under `NDEBUG` as a deliberate release optimization, so on a release host the
telemetry file was empty and every probe failed with "reported no functions" —
not a size regression, an unobservable metric.

This matters because MT7 gates `test-lambda-baseline`, which pairs debug test
binaries with whatever `lambda.exe` is present, including a release one. Fixed
by emitting the two telemetry lines with `log_notice`, which survives release.
The telemetry stays opt-in behind its env var, so normal runs are unaffected.

The bigger conclusion from this exercise: **emission is identical on debug and
release hosts.** After the telemetry fix all ten probes matched their
debug-recorded thresholds exactly on a release host, with no "shrank" reports,
so no per-config profile is needed and §8.1a's open question is closed.

### 9.3 Finding: MIR-Direct lacks C2MIR's system-function specializations

Two of the orphaned fixtures asserted optimizations that MIR-Direct does not
perform. `unboxed_sys_func.transpile` expected unboxed variants (`fn_pow_u`,
`fn_min2_u`, `fn_max2_u`, `fn_abs_i`, `fn_sign_i`, `fn_sign_f`) for typed
arguments, and `sys_func_native_math.transpile` expected native C math
(`sin(`, `sqrt(`, ...) in place of runtime calls.

Measured: those symbols exist in `lambda/sys_func_registry.c` and are emitted
by the C transpiler, but `lambda/transpile-mir.cpp` contains **zero**
references to any of them, and emits no native math call. A typed
`x ** y` / `abs(x)` / `math.sin(x)` under MIR-Direct imports the boxed
`fn_pow` / `fn_abs` / `fn_math_sin` entry points instead.

This is a plausible contributor to the MT8 observation that some tests run
faster under `--c2mir` than under MIR-Direct, and is the first concrete lead
for that investigation. No fix is attempted here — this design owns the tests,
not the optimizer. `test/mir/lambda/sys_func_specialization.{ls,mir-check}`
records the current boxed emission and forbids the specialized symbols, so if
the specializations are ported to MIR-Direct the fixture fails and is updated
deliberately.

### 9.4 Note: `.transpile` fixtures were retired, not merely superseded

The four files had no consumer anywhere in the repository and asserted C2MIR C
output that is meaningless for MIR-Direct. Their intent (watching TCO and
system-function specialization decisions) is preserved by
`tail_call_tco`, `tail_call_tco_proc`, and `sys_func_specialization`, so the
dead files were removed rather than left to look like coverage.

---

## 10. Implementation record — P3 (2026-07-22)

### 10.1 What landed

`test/test_mir_gc_stress_gtest.cpp` sweeps every `test/mir` fixture plus six
named rooting/capture regressions (`regression_side_stack_frame_gc`,
`array_callback_gc_roots`, and the four Stage-4C capture/hoist scripts) through
the three exact-root forced-GC modes MT4 specifies: JIT with
`FORCE_EVERY=1`, deterministic randomized `FORCE_ONE_IN=3`, and `--mir-interp`
with `FORCE_EVERY=1`. All modes run with `POISON_FREED=1`.

The oracle is **self-baselining**: each stressed run must exit 0 and produce
byte-identical output to the same script run unstressed. No goldens are
maintained, so adding a corpus fixture extends this gate automatically and a
fixture whose output legitimately changes cannot drift out of coverage.

`make test-gc-rooting-core` now invokes the sweep instead of growing another
dozen near-identical shell blocks, and `make test-mir-gc-stress` is the
standalone entry point. Process launching was factored into
`mir_check::run_lambda_process` so the subcommand rules (`js`, `run`,
`--mir-interp` placement) live in one place shared with the emission harness.

**Cost measured, then promoted.** The sweep runs in **7.8s** (7.7/7.7/7.9 over
three consecutive runs), about 6% of `test-lambda-baseline`, and is stable. It
therefore runs at baseline tier, satisfying the doc's "promote only after
measured cost is acceptable". Baseline is **3551/3551**.

### 10.2 Finding: forced GC exposes real pre-existing use-after-free bugs

`test/js/array_callback_gc_roots.js` passes normally but under every forced-GC
mode fails with `TypeError: Set.prototype.add is not callable` — a builtin
prototype method collected while still reachable from user code.

That is not an isolated case. Sampling twelve `test/js` scripts that are green
in the ordinary baseline, **four diverged under `FORCE_EVERY=1` exact-root collection**:

| script | symptom |
|---|---|
| `array_callback_gc_roots` | `Set.prototype.add is not callable` |
| `arguments_callee_strict` | SIGSEGV at `0x19999a09999b9997` |
| `arguments_rest_default` | SIGSEGV at `0x19999a09999b9997` |
| `async_v14` | `TypeError: is not a function` |

`0x9999...` is the `POISON_FREED` fill, so those two are use-after-free of
collected memory, made deterministic by the poison. These are pre-existing
engine bugs: nothing in P1–P3 touches GC or rooting, and the failures reproduce
from environment variables that predate this work.

The wider lesson is about coverage, not about any one bug.
`make test-gc-rooting-core` forced-GC-tests a handful of hand-picked scripts;
the moment the aperture widens, roughly a third of a random JS sample fails.
The rooting design's S0–S6 "complete" status should be read as *complete for
the scripts currently gated*, not as a property of the engine at large.

### 10.3 How the known failure is carried

`array_callback_gc_roots.js` stays in the sweep on an explicit
`kKnownForcedGcFailures` list. It is still executed in all three modes, and the
test asserts it **still fails**: if it ever survives every mode, the sweep goes
red and tells the reader to retire the entry. Dropping the script instead would
have made the suite green by deleting coverage, which the no-silent-caps rule
forbids. The other three scripts above are outside this design's corpus and are
recorded here as findings rather than added to the sweep.

Promoting them into the gate is a decision for whoever fixes the rooting bugs;
this design deliberately does not expand its corpus to cover the whole JS
suite, which is MT3-era work.
