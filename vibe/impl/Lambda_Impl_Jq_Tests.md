# Lambda Impl — jq workloads for the `text` benchmark suite (proposal)

**Status:** v3, 2026-10-05, with the user's decisions recorded in §8. Done on
branch `jq-bench`: P0, P1, P2, P4, P5 and the LambdaJS crash fix (§4.3). V1
(P3) is written and verified but blocked at full size (§1.5). All four
filters below were prototyped under `./temp/jaq/` (uncommitted scratch) and
cross-checked on jq 1.7.1, gojq 0.12.19, jqjs (git `f2894f6`) and purejq 0.3.1.
**Scope:** add jq-language workloads to `test/benchmark/text/`. Most engine
columns run a **jq interpreter** over the same `.jq` filter text: an existing
port for Node, Go and Python, and a new interpreter written for C2MIR. A
typed Lambda port of that interpreter (the "Lambda VM") is reported inside
the C2MIR cell, with no column of its own. The Lambda columns (MIR-T,
MIR-U) run each filter translated by hand into a Lambda query over the same
JSON.
**Goal (from the request):** show that jq's features can be built in Lambda
without strain, and stress that implementation under load. The goal is *not*
full jq conformance.

---

## 1. Summary

| Row | Kind | Filter source | What it exercises | Size | jq 1.7.1 user time |
|---|---|---|---|---|---|
| `jq_mix` | breadth | jaq `examples/benches/*.jq` (27 filters) | generators, `def` and recursion, `reduce`/`foreach`, `limit`/`repeat`/`recurse`/`nth`/`last`, `try`/`error`, sort/group/min/max/unique, object build/update, `tojson`/`fromjson`, string ×/slice/`explode`/`implode`, `contains`, `flatten`, `paths`, `|=`/`+=` | 7 rounds | **1.9 s** |
| `jq_records` | breadth (data) | new, written for this suite | JSON fixture processing: `select`/`map`/`group_by`/`sort_by([..])`/`unique`/`first`, `//`, `//=`, `del`, `with_entries`/`to_entries`/`from_entries`, string interpolation, `join`/`split`/`ascii_upcase`, `paths(f)`, `..|objects|select(..)` `|=`, slices, `try`/`tonumber`, filter-argument `def`s | 4,000 orders × 15 rounds | **1.8 s** |
| `jq_bf` | load | jaq `examples/bf.jq` (originally itchyny/brainfuck, by the gojq author) over `fib.bf` | an interpreter written in jq: `until`, `last(recurse(..))`, nested `def`s with filter args, path updates (`.memory |= assign(..)`, `.[i] |= f`), string slicing, `implode`, `limit`/`repeat`, `error` | 10 passes | **6.3 s** |
| `jq_tree` | load | jaq `tree-update` + `tree-paths` + `tree-flatten` | path machinery at scale: `(.. | scalars) |= f` over 2^17 leaves, `[paths]`, `flatten`, `nth(recurse(..))` | depth 17 × 7 rounds | **6.4 s** |

Two breadth rows and two load rows, four in total. The two load rows meet the
"jq version runs ≥ 5 s" requirement. All timings are jq *user* time on this
machine while it was heavily loaded (load average 19–26), so Phase P0 (§7)
recalibrates them on a quiet machine.

| Column | Implementation | Status |
|---|---|---|
| Node.js | **jqjs** (Michael Homer, MIT), vendored from git master `f2894f6`. The npm 1.6.0 release is too old (§4.1) | runs all 4 rows; needs the portable subset (§3) |
| Go | **gojq** 0.12.19 (itchyny, MIT) as a library | runs all 4 rows unchanged |
| Python | **purejq** 0.3.1 (MIT, zero dependencies, pure Python: it compiles to closures and does not wrap C) | runs all 4 rows; `jq_bf` needs a higher recursion limit (§4.2) |
| C2MIR | **new**: a reduced jq VM in C, modelled on jq's own `src/execute.c`/`compile.c`/`jv.c` | DONE (§1.2) |
| Lambda VM (V1), **shown in the C2MIR cell** | **new**: typed Lambda port of the C2MIR VM, the like-for-like comparison with C2MIR | written and verified (§1.5); all four rows blocked at full size by LR03-37/38 |
| Lambda **MIR-T** (V2) | **new**: each filter translated by hand into typed Lambda that works on the JSON directly (no VM) | DONE (§1.4) |
| Lambda **MIR-U** (V3) | **new**: V2 without type annotations | DONE (§1.3) |
| LambdaJS | runs the Node driver | crash FIXED (§4.3); correct results, but `jq_mix` exceeds the 600 s row timeout even in release (§4.3, P6) |

---

### 1.1 Implemented state (P0 + P1, 2026-10-05)

The final sizes were recalibrated on a quiet machine (load average about 3).
The earlier numbers were taken at load average 19–26, when jq ran about 2×
slower. The goldens are jq 1.7.1 output, and gojq agrees on all four.

| Row | Golden | jq 1.7.1 | gojq (Go) | jqjs + patches (Node 22) | purejq (CPython 3.14) |
|---|---|---|---|---|---|
| `jq_mix` | 98172625 | 1.88 s | 1.05 s | 3.87 s | 10.3 s |
| `jq_records` | 878885883 | 1.78 s | 1.13 s | 2.00 s | 6.5 s |
| `jq_bf` | 478890292 | 6.31 s | 2.60 s | 2.96 s | 26.5 s |
| `jq_tree` | 313746104 | 6.35 s | 4.37 s | 5.79 s | 37.5 s |

The reference columns time evaluation only (`__TIMING__`); the jq column is
the process's user time. Go 1.24 is now required: `go.mod` moved from
`go 1.22` to `go 1.24.0` because gojq 0.12.19 needs it.

### 1.2 P2: the C2MIR jq-core VM (2026-10-05)

`test/benchmark/text/c2mir/jq_value.h`, `jq_parse.h`, `jq_vm.h` and
`jq_core.h` (about 2,400 lines) implement §5, plus four thin drivers
`jq_*.c`. Values are NaN-boxed and immutable, collected by a mark-and-sweep GC
at the VM's safe point. The data stack is forkable, and fork records cover
EACH, RANGE, try/label and the try-exit marker. Path tracking and heap frames
with closures follow jq, and tail calls reuse frames. The prelude is jq 1.7.1's
`builtin.jq` definitions. `add`, `join`, `flatten` and `from_entries` are
native, because jq's own definitions rely on in-place mutation.

**Verification.**
- 109 differential filters against jq 1.7.1, compiled natively with clang
  under ASan + UBSan and also under `c2m`. The only differences are a
  compile-error case and `splits`, which is outside the subset.
- GC stress: `-DJQ_GC_MIN_THRESHOLD=65536` under ASan, all four workloads.
- All four rows match the goldens under `run_c2mir_benchmarks.py --suite text`.

**MIR bug found and fixed** (user-approved, rule 16):
`patches/mir-conventional-ssa-self-loop.patch`, recorded in
`lambda/mir/VENDOR.md`. At `-O2`/`-O3`, `make_conventional_ssa` placed the
back-edge phi move before a single-block loop's terminating branch, which
still read the phi result. As a result, `while (hops-- > 0) f = f->env;` ran one
iteration short, so the VM's `frame_hop` broke under `c2m`. After the patch,
`make verify-mir-patches` passes and `make test-lambda-baseline` gives
6216/6217; the one failure is the worktree-only
`pdf_svg_page_resources`, whose ignored PDF is missing.

C2MIR times on a quiet machine (`c2m -O2`, measured with the patched scratch
driver before vendoring): mix 1.79 s, records 1.58 s, bf 3.26 s, tree 7.53 s.

### 1.3 V3: the untyped Lambda translations (2026-10-05)

`test/benchmark/text/jq_{mix,records,bf,tree}.ls` plus the shared
`test/benchmark/jq_query_common.ls`. All four rows match their goldens on JIT
and AUTO. Debug-build times are not yet benchmark numbers: mix 5.5 s, records
1.1 s, bf 0.17 s, tree 2.5 s. `jq_records` uses a constant-key `m["k"] = v`
that T0 cannot execute (LR12-36), so it runs on JIT and AUTO only.

Writing them turned up Lambda defects.

**Fixed:**
- **GC:** a full data compaction released the old tenured zone before sweep,
  while dead ArrayNum views' finalizers still read their shape side tables
  there (`15d9fc9bb`).
- **T0:** a compact numeric literal was built around a widened `var`
  (`eb43c4dad`, partial).
- **`sort()`, `sort(v, key)` and `order by`** were O(n²) insertion and bubble
  sorts. A reversed 65,536-element sort took 28 s (`94af5e2bc`).

**Ledgered:**
- LR03-36: stale lanes in types derived before a `var` widens.
- LR07-43: the JIT truncates an `any` value into an int-inferred `var`.
- LR09-32: JSON `compact: true` is ignored.
- LR10-18: a value-bound handler on a `pn ... T^` call yields `null`.
- LR12-36: T0 cannot execute a constant-key index assignment.

**Translation choices this forced:**
- Float sums start at `0.0`. jq numbers are doubles, so this is faithful, and
  it also avoids LR07-43.
- A `pn` call's error is propagated with `^`, because its `^ { … }` handler is
  statement-only.
- Recursive helpers use statement-form loops, so they collect no values.
- `jq_contains` short-circuits as jq's `jv_contains` does.

### 1.4 V2: the typed Lambda translations (2026-10-05)

`test/benchmark/text/jq_{mix,records,bf,tree}2.ls` plus
`test/benchmark/jq_query_typed.ls`. They use the same algorithms as V3, with
annotations where jq's values are uniform: `int`, `float`, `string`, `int[]`,
`string[]`, a `BfState` record type for `bf.jq`'s state, and `map`/`array`
for JSON values. All four match their goldens on JIT and AUTO. Debug-build
times: mix 5.1 s, records 1.3 s, bf 0.10 s, tree 2.4 s.

Typing the records row exposed three checked-map-setter defects, all fixed in
`5ad5ef235`:
- `var m: map = {}; m["a"] = 1` segfaulted.
- An open member was re-appended on every write, so a key held earlier was
  stored again each time.
- A `map`-typed write cloned the whole map per store. The counting loop took
  39 s.

### 1.5 V1: the typed Lambda jq VM (2026-10-05, blocked at full size)

`test/benchmark/jq_vm.ls` (about 2,700 lines) ports the C2MIR VM (§1.2)
structure for structure:
- the same opcode, fork-record and native numbering;
- the same parser and compiler, with jq 1.7.1's prelude;
- a forkable value stack and fork records as typed `int[]`/`array` fields of
  one `Vm` record;
- path tracking, `label`/`break` and the try-exit marker.

Two things differ, because Lambda values are immutable containers rather
than heap cells:
- Frames live in an arena of parallel arrays (`fr_*`, `locals`, `pfn`,
  `penv`). They are reclaimed by a mark-compact pass (`vm_compact_frames`)
  when 50,000 frames are live, instead of being individually collected.
- jq values are Lambda values: objects are maps with computed keys, and the
  runtime's GC replaces the C port's.

`jq_vm_benchmark(name, input_kind, input_path, expected)` runs one row with
the same `.jq` file and input as the C2MIR driver. Each row's entry is an
eight-line `text/jq_<row>_vm.ls` that imports `~~.jq_vm` and calls it. The
runner already times any `<bench>_vm.ls` it finds as `c2mir_lambda_vm`, and
`gen_overall_result.py` appends ` / λ-VM <time>` to the C2MIR cell (§5.5). The
entries are not committed yet (see Blocked below).

**Verification.** The same 109 differential filters as P2, run through
`jq_vm_eval` and compared with jq 1.7.1. The only differences are the same
two as C2MIR's (a compile-error case and `splits`). The 27 `jq_mix` filters
run one at a time at full size and all match jq.

**Runtime defects found, fixed on this branch:**
- An N-D array index write `m[0] = [9, 2]` widened the flat leaves, giving
  `[[9, 2], 2, 1, 2]`. S1.6 makes the N-D representation invisible, so it now
  replaces the row (`6745b2443`).
- A packed array widened in place to a generic `Array` kept its `ArrayNum`
  GC tag. The collector traced it as raw numbers and freed the containers it
  held, which corrupted the VM's trees under forced collection.
  `convert_specialized_to_generic` and the new N-D widening now retag the
  header (`heap_retag_container`) in the same step (D4.3.1). Pinned by
  `proc/ndim_row_assign.ls` in the forced-collection sweep.

**Blocked: V1 cannot finish any row at full size.** `jq_bf` and `jq_records`
passed 4 GB of RSS within about 10 s and were stopped, and `jq_mix` and
`jq_tree` were killed by the OS. Three runtime defects, ledgered for a ruling
or a separate fix:
- [LR03-37](../Lambda_Issue_Ledger.md#lr03-37): a write that changes the kind
  of value in a typed record's `any` field rebuilds the whole shape into the
  never-reclaimed pool. The `Vm` record's `vat`/`err` change kind on every
  path step, so `jq_tree` and the path filters passed 10 GB and were killed.
- [LR03-38](../Lambda_Issue_Ledger.md#lr03-38): each new key on a runtime-grown
  map copies its whole shape, O(n²) pool memory per n-key object.
  `b_kv_update` (2,048 keys) passed 11 GB. Small objects leak too: every
  object update rebuilds the map, so `jq_bf`'s five-key state leaks fresh
  shapes on each step.
- [LR07-44](../Lambda_Issue_Ledger.md#lr07-44): every call that passes the
  `Vm` record to a `var vm: Vm` parameter, and every nested write through it,
  re-validates all 39 fields. That is the largest CPU cost once memory is
  bounded (`b_try_catch` 8,192: 5.7 s).

Per-filter release times (one process each, `jq_mix` sizes) show the shape:
`b_reverse` 0.16 s, `b_sort` 0.71 s and `b_reduce` 0.67 s on 65,536 elements
are in range. `b_ack` takes 10.8 s and `b_try_catch` 14.8 s. `b_tree_update`
(35.8 s), `b_tree_paths` (24.2 s) and `b_kv_update` (did not finish) are
dominated by LR03-37 and LR03-38.

## 2. Workloads

Every row follows the suite's existing conventions (`test/benchmark/text/README.md`):

- The fixture and the filter are loaded and the filter is compiled outside the
  timed region. Only evaluation is timed.
- The program prints `<row>: CHECKSUM:<n>` and `__TIMING__:<ms>`.
- The checksum is checked against a golden `.txt`.

**Checksum design.** Each filter reduces its result to **one integer**
(`% 1000000007`, folded across rounds as `. * 31 + digest + $r`). The engines
differ in output formatting: gojq sorts object keys, and the engines print
non-integral floats differently. Because the checksum is an integer, it never
depends on object key order or on float text. Prices are turned into integer
cents before they are summed or interpolated.

**Sizing lives in the filter.** Rounds and depth are written into each `.jq`
file as `def rounds: N;` and `def depth: N;`. They are not passed as `--arg`,
so one filter file is the single source of truth for every engine. The drafts
in the Appendix still use `$rounds`/`$depth` for calibration. The final files
replace them.

Cross-engine agreement measured with 1 round: mix `148015043`, bf `2046`,
tree (depth 12) `12286`. With 2 rounds: records `215194764`. All four reference
engines gave identical values.

### 2.1 `jq_mix` (breadth, from jaq)

Twenty-seven of jaq's bench filters, each wrapped as `def b_<name>:` and reduced
to a digest. The `n` values are cut down from jaq's (which go up to 1,048,576)
so that a single pass costs about 0.7 s in jq. They are also capped wherever a
reference engine hits a pathology (§4):

- Recursion-shaped filters are capped at 512–1024: `upto`, `pyramid`,
  `ex-implode`, `repeat` and `from`. jqjs overflows Node's default stack above
  roughly 512 levels of jq recursion, and purejq's nested generators are
  quadratic.
- Array and object `add` are capped at 8192 and 2048. jqjs's `add` is quadratic.

One deviation from upstream jaq, still valid jq with an unchanged result:
`sort` uses `0 - .` in place of `-.` (J-5).

The prototype also rewrote `ack($m; $n)` as `[$m, $n] | ack`, because
pristine jqjs computed it wrongly (J-2). The jqjs patch (§4.1) fixes J-2, so
the committed filter keeps jaq's original `ack`. Patched jqjs returns 253 for
`ack(3; 5)`, matching jq.

### 2.2 `jq_records` (breadth, data processing)

This row exercises the jq-as-ETL style that matches Lambda's own domain. It
covers several things the jaq micro-filters do not: JSON input, field-shorthand
object construction, `//=`, `del`, `with_entries`, string interpolation,
`paths(f)`, and an update through `..|objects|select(has(..))`.

The fixture is `orders.json`: 4,000 orders, about 860 KB, ASCII only. It is
deterministic. The Appendix gives the jq expression that generates it; a
`generate_jq_fixture.js` will port that expression, following the
`generate_*_fixture.js` precedent.

Not every jq feature is used, because the reference engines can't all run them:

- `as [$a, $b]` and `reduce … as {k: $v}` destructuring are left out because
  jqjs can't parse them (J-4).
- `label`/`break` is left out because jqjs doesn't have it.
- Regex is left out because the C2MIR port would need its own regex engine.
  Lambda's string patterns could cover regex in a later row.

### 2.3 `jq_bf` (load: an interpreter written in jq)

This is jaq's `bf.jq`, unchanged except for two things:

- A one-line prelude `def repeat(g): def _repeat: g | (., _repeat); _repeat;`.
  This is jq 1.7's own definition. jqjs lacks the builtin (J-1). The `g`
  parameter name was a J-2 workaround that the patch makes unnecessary, but
  it is harmless.
- `.output += [.memory[.pointer]]` becomes
  `.memory[.pointer] as $v | .output += [$v]`. jqjs evaluates the right side of
  `+=` against the path value instead of `.` (J-3).

The program is wrapped as `def bf:` and run over `fib.bf` (299 bytes, from jaq)
for `rounds` passes. The checksum is the code-point sum of the program output.

This row is the strongest single test of "can Lambda implement jq". It needs
deep generator backtracking (`last(recurse(..))`, `until`), filter-argument
closures, and nested path updates, all in a hot loop of roughly 10^5 steps
per pass.

### 2.4 `jq_tree` (load: path machinery)

```jq
def tree($d): nth($d; 0 | recurse([., .]));
tree(17) as $t
| reduce range(3) as $r (0;
    ($t | (.. | scalars) |= . + 1 + $r) as $u
    | (. * 31 + ($u | [paths] | length) + ($u | flatten | add)) % 1000000007)
```

Each round updates every one of the 131,072 leaves through a path expression,
lists 262,142 paths, and flattens the tree. Path tracking (jq's
`PATH_BEGIN`/`PATH_END`, `getpath`/`setpath`) is the hardest part of jq to get
right and fast, and this row isolates it.

---

## 3. Portable subset and fairness

**One filter text for every engine.** Every rewrite in §2 is ordinary jq. jq
and gojq produce the same values before and after the rewrites, so no engine
gets an easier filter.

**Not an equal-algorithm comparison.** The other text rows "preserve the
workload shape" with hand ports. Here, three columns run third-party
interpreters with different designs:

- jqjs: a tree-walking interpreter built on JS generators.
- gojq: compiles to a bytecode VM.
- purejq: compiles to Python closures.

The two values in the C2MIR cell, C2MIR and the Lambda VM, run the *same* VM
design (§5), so they are the like-for-like comparison. The other columns measure each
ecosystem's real jq. The README should say this.

**Projected per-row times on the other engines.** These are scaled from the
measured per-pass ratios to jq.

| Row | gojq | jqjs (Node 22) | purejq (CPython 3.14) |
|---|---|---|---|
| `jq_mix` ×3 | ≈1.3 s | ≈8.6 s | ≈7.5 s |
| `jq_records` ×6 | ≈1.2 s | ≈9 s | ≈20 s |
| `jq_bf` ×5 | ≈3.2 s | **≈250–320 s** (50.6–63.6 s per pass) | ≈80 s (16.0 s per pass) |
| `jq_tree` ×3 | ≈4.3 s | ≈12.6 s | **≈130 s** (43.5 s per round) |

The two bold cells exceed the runner's default 120 s timeout (open question Q1).

---

## 4. Defects found in the reference engines

These are vendored third-party code, so CLAUDE.md rule 16 applies: no patching.
Each defect is avoided in the filter text instead, and is worth reporting
upstream.

### 4.1 jqjs (git `f2894f6`; npm 1.6.0 is worse)

npm 1.6.0 has no `try … catch`. `.a += 1` on a missing key throws
`undefined + number`, and `//=` on a missing key is a no-op. Git master fixes
all three, so **vendor master, not npm.**

| ID | Defect on master | Workaround |
|---|---|---|
| J-1 | no `repeat/1` builtin | prelude `def repeat(g)` (jq 1.7's definition) |
| J-2 | filter arguments are evaluated in the callee's scope (dynamic scoping). `def repeat(f)` clobbers the caller's `f` while a generator is suspended, and `def ack($m; $n)` recursion returns wrong values or overflows | **fixed by the local patch** (below) |
| J-3 | `lhs += rhs` evaluates `rhs` against the lhs path value, not `.` (jq: `rhs as $x | lhs |= . + $x`) | bind the value first: `… as $v | lhs += $v` |
| J-4 | no destructuring (`as [$a,$b]`, `as {k: $v}`) | index explicitly |
| J-5 | unary minus on a path (`-.total`) does not parse | `0 - .total` |
| J-6 | `sort_by(f, g)` uses only the first output of the comma | `sort_by([f, g])` |
| J-7 | about 512 levels of jq recursion overflow Node's default stack; array and object `add` are quadratic | cap `n` in `jq_mix` |

**Why jqjs is slow.** A Node `--cpu-prof` of `jq_bf` (one pass over `fib.bf`)
shows two causes:

1. **79% self time is in `FunctionCall.apply`** (`jq.js:2129`). Every call to
   a jq function runs `conf.userFuncArgs = Object.create(origArgs)` in
   `makeUserFuncFromNode` (`jq.js:235`), so the argument scope is a JS
   prototype chain that grows one link per active call. Each argument lookup
   `conf.userFuncArgs[this.name]` walks that chain, and V8 cannot cache
   lookups on such long, ever-changing chains. Deep recursion (`until`,
   `recurse`, `last(recurse(..))`) therefore costs O(depth) per lookup.
   The same design causes J-2: `conf` is one shared mutable object, so a
   suspended generator that resumes later sees whatever scope is current at
   that moment.
2. **9.5% is in `UpdateAssignment.apply`** (`jq.js:1926`), which starts every
   `|=` with `input = JSON.parse(JSON.stringify(input))`. That deep-copies the
   whole input on each update. In `jq_bf` the input is the whole interpreter
   state, including the program text, copied on every step.

3. **Every `add` over arrays or objects is quadratic**, because it re-copies
   the running sum for each element.

**Local patch (user decision Q5(b), 2026-10-05):**
`patches/jqjs-lexical-scope-cow-add.patch`, already applied to
`test/benchmark/text/jq/vendor/jqjs.js` (see `test/benchmark/text/jq/VENDOR.md`).

- **Lexical environments.** `conf` is never mutated. A call or a binding
  (`as`, `reduce`, `foreach`) derives a new `conf`. A call's argument frame
  chains to the frame of the lexically enclosing `def`, which the parser
  records, rather than to the caller's frame. Each argument is a closure over
  the caller's `conf`. Lookups then cost the `def` nesting depth instead of the
  call depth, and J-2 is fixed. (`until`/`recurse` were already native loops
  upstream.)
- **Copy-on-write assignment.** `|=` and `=` copy only the containers on each
  updated path, each container at most once per assignment. They read paths
  from the original input, which is jq's own definition of `|=` as a reduce
  over `path(f)`. Holes left by deletions are compacted only in the copied
  arrays. This replaces the whole-input deep copy and the `normaliseDenseJson`
  re-walk.
- **Linear `add/0`.** Arrays and objects accumulate into one fresh container.

**Verification.**
- jq 1.7.1 `tests/jq.test` via jqjs's `run-test.js`: 339 passed / 94 failed
  before, 341 / 92 after. No case fails that passed before. The two new passes
  are jq's lexical-closure test and `(.a as $x | .b) = "b"`.
- Pristine f2894f6 plus the patch reproduces the vendored file byte for byte.

**Speed** (Node 22, one round, quiet machine, identical checksums):

| Row | pristine | patched | speed-up |
|---|---|---|---|
| `jq_bf` (1 pass) | 7,005 ms | 554 ms | 12.6× |
| `jq_mix` | 1,988 ms | 965 ms | 2.1× |
| `jq_records` | 361 ms | 282 ms | 1.3× |
| `jq_tree` (depth 16) | 716 ms | 688 ms | 1.0× |

**Unchanged by the patch**, and still avoided in the filter text: J-1, J-3,
J-4, J-5, J-6, and J-7's stack depth. The README labels the Node and LambdaJS
columns **jqjs + patches**.

### 4.2 purejq 0.3.1

| ID | Defect | Workaround |
|---|---|---|
| P-1 | `until`/`recurse` recurse in Python: `jq_bf` raises `RecursionError` | the driver calls `sys.setrecursionlimit(2_000_000)` and runs on a thread with a 512 MB stack. This is runtime configuration, not a patch |
| P-2 | generator chains are nested Python generators, so `limit(n; recurse(..))` and `repeat` cost O(n²): 0.11 / 0.27 / 1.19 s at n = 1k / 2k / 4k | cap `n` in `jq_mix` |

### 4.3 LambdaJS crash loading jqjs — FIXED (worktree `jq-bench`)

**Symptom.** `./lambda.exe js` exited with SIGSEGV (139) and no output while
loading the unmodified jqjs source (the 3,820-line file with its two `export`
lines stripped), before any filter ran.

**Root cause.** It is not specific to jqjs.
- jqjs's top-level parser setup calls some functions often enough for D8.1.3
  promotion (`js_interp_promote_function_if_hot`) to compile them as P2
  satellites.
- `js_mir_lower_function_satellite` refuses a definition whose body names a
  top-level `class` binding (`MCONST_CLASS`), with "module binding form". It
  also refuses a definition without a closed MIR plan, and one whose analysis
  fails.
- All of those refusal paths returned `false` with the satellite's MIR module
  still open. The caller's cleanup (`jit_cleanup_mode` → `MIR_finish`) then
  hit MIR's "finish when module %s is not finished" error. MIR raises that
  error after `string_finish` has already freed the module name.
- The result was `default_error` calling `exit(1)`, or a SIGSEGV in `strlen`
  when the freed name was unreadable.
- The minimal trigger is five calls to `function make(v) { return new Box(v); }`.
  The Oct-4 main build exits 1 on it, silently.

**Fix.** `js_mir_refuse_function_satellite` in
`lambda/js/js_mir_module_batch_lowering.cpp` closes the module on every refusal
path. This is the satellite counterpart of the existing early-analysis guard in
the full-module plan. The refused definition stays pinned to the interpreter,
which is the intended outcome.

**Test.** `test/js/regression_p2_satellite_refusal.js` (+ `.txt`) fails on the
old binary and passes on the fixed one.

**Worktree setup note.** A fresh worktree also needs the Node module dylibs
(`make build-node-{core,fs,net,crypto,zlib}`). Without them `require("fs")`
fails with "Cannot find module".

**Remaining LambdaJS issue: speed.** After the fix, jqjs gives correct results
under LambdaJS, but the debug build is very slow on it:
- `b_upto` (512) takes 4.2 s.
- `b_reduce_update` (2048) takes 28.8 s, against 0.23 s on Node.
- `b_sort` (65536) takes more than 60 s.
- `ack(3;5)` takes 56 s, against 0.39 s on Node.
- With the patched jqjs, `ack(3;5)` still takes 65 s (debug). The patch's
  fixes do not touch this cost, so it is on the LambdaJS side.

Results are correct in every case. Debug timings are not valid evidence
(CLAUDE.md rule 10). Re-measure on a release build before any analysis (P6).
A release build was not possible at HEAD `f722d537f`: `make release` failed on
`radiant/view_pool.cpp:648`, where `view_slot_value` is unused and release
builds use `-Werror`. It is fixed on this branch (`0041b60ff`, the helper is
debug-only).

**Release measurement (2026-10-05, one run).** `jq_mix` under LambdaJS
exceeded the 600 s row timeout, and its AUTO run was past 8 minutes when the
runner was stopped. Node runs the same driver in 3.9 s, so the gap is more
than 150×, not a debug artifact. The other three rows were not reached.
Analysing it is P6.

---

## 5. The C2MIR and Lambda implementations

### 5.1 Why a VM after jq's own design

The request says the C2MIR port should use jq's C implementation as its
reference. jq does not evaluate a tree. It compiles to bytecode and runs a
**backtracking VM**:

- `FORK` pushes a resume point.
- `BACKTRACK` pops one, and that is how one expression produces many outputs.
- `SUBEXP_BEGIN`/`SUBEXP_END` wrap a sub-evaluation.
- `PATH_BEGIN`/`PATH_END` track paths.
- `CALL_JQ`/`TAIL_CALL_JQ`/`RET` handle calls, with closures for filter arguments.
- Values are reference-counted `jv` that are mutated in place when the count is 1.

jq-core ports a reduced form of this design. The Lambda port is then a
line-for-line translation of the C port, so the two columns run the same
algorithm.

**Alternative considered: a generator-as-fold (CPS) evaluator in Lambda.** It
was rejected for two reasons:

- Lambda closures are immutable snapshots (**S9.1.4**), so every output would
  have to thread its state through return values.
- jq's deep generator chains (`until`, `last(recurse(..))`, about 10^5 steps
  per `jq_bf` pass) would become native recursion depth.

The VM keeps its fork stack and value stack as explicit arrays in a `pn` with
`var` state, which the Lambda runtime handles well. Because `fn` never calls
`pn` (**S12.1.1v2**), the interpreter is `pn` throughout. Only pure helpers
such as the comparator and digest can be `fn`.

### 5.2 Feature set: the union the four filters need

- **Syntax:**
  - `|`, `,`, parentheses, literals, string interpolation `"\(..)"`
  - `.`, `..`, `.k`, `.[e]`, `.[a:b]` (arrays and strings, code-point indexed), `.[]`
  - `[..]`, `{k: v, (e): v, k}` object construction with shorthand
  - `//`, operators `+ - * / %`, `== != < <= > >=`, `and`, `or`
  - assignment `=`, `|=`, `+=`, `//=`
  - `if/elif/else/end` (optional `else`, as in jq 1.7)
  - `try … catch`
  - `reduce`, `foreach` (2- and 3-clause), `E as $x | …`
  - `def` with filter parameters and `$` parameters, nested and recursive
- **Native builtins:**
  - `length`, `type`, `keys`, `has`, `contains`
  - `tostring`, `tonumber`, `tojson`, `fromjson`
  - `sort`, `_sort_by_impl`, `_group_by_impl`, `min`, `max`, `unique`
  - `explode`, `implode`, `split/1`, `floor`, `ceil`, `error`, `empty`, `not`
  - `range/1,2,3`, `path(f)`, `getpath`, `setpath`, `delpaths`
- **Builtins copied from jq 1.7 `src/builtin.jq` (MIT):**
  - `map`, `select`, `recurse`, `to_entries`, `from_entries`, `with_entries`
  - `paths`, `add`, `join`, `ascii_upcase`, `first`, `last`, `nth`, `limit`
  - `until`, `repeat`, `del`, `flatten`, `group_by`, `sort_by`, `unique`
  - `scalars`, `objects`, `numbers`
- **Internal `label`/`break`:** jq 1.7's `limit`, `first(f)` and others are
  defined with it. The VM implements it so those builtins can be copied
  verbatim. User filters still avoid it because jqjs lacks it.

Not in scope: modules, regex, `@formats`, dates, streaming, SQL-style builtins,
`$__loc__`, `input`/`inputs`, `--arg`, `getpath` on invalid paths beyond what
the filters hit, and Unicode beyond code-point indexing of ASCII data.

### 5.3 Semantics checklist (sources of silent checksum drift)

- **Numbers** are IEEE doubles. `/` always produces a float, and `%` truncates
  both operands to integers. The ports may keep integral values with
  |x| < 2^53 in an int lane as a fast path, but equality, ordering, object keys
  and `tostring` must behave as for doubles (`1.0 == 1`, `tostring` gives `"1"`).
- **Total order** for `sort`/`unique`/`group_by`/`min`/`max`:
  null < false < true < numbers < strings < arrays < objects. Objects compare
  by sorted key arrays first, then values key by key.
- **`null` arithmetic:** `null + x = x`, so `.[k] += v` works on an absent key
  and `(. // 0)` behaves as in jq.
- **`paths` and `..`** iterate objects in insertion order. The checksums do not
  depend on that order (gojq uses sorted order), but `[paths] | length` must
  match.
- **Errors** are values: `try error catch .` yields the payload, and
  `try (f | tonumber) catch 0` swallows parse errors.

### 5.4 C2MIR specifics

The core goes in `test/benchmark/text/c2mir/jq_core.h`, included by four thin
drivers `jq_mix.c`, `jq_records.c`, `jq_bf.c` and `jq_tree.c`. This follows the
`hyphen.c` + `hyphen_patterns_data.h` precedent: C2MIR resolves local includes,
and libc is declared through `extern` as in the other ports.

Values are a reduced **refcounted `jv`**:

- copy-on-write when the refcount is above 1
- in-place updates otherwise
- arrays and strings with capacity
- objects as small open-addressing tables that keep insertion order

Refcounting is required, not optional. `jq_bf` allocates a fresh state object
on every interpreted step (hundreds of thousands per run), so a free-never arena would grow to
gigabytes.

The core also needs its own JSON parser and writer (for the fixture, `tojson`
and `fromjson`). Integers print exactly. Non-integral output is never needed
because of the integer checksums.

Rough size: 3.5–4.5k lines. For scale, jq's `execute.c` + `compile.c` + `jv.c`
+ parser come to about 7k lines, and the subset drops most of that.

### 5.5 Lambda V1: typed VM (reported in the C2MIR cell, like-for-like with C2MIR)

The core goes in `test/benchmark/jq_vm.ls`, alongside `hyphen_core.ls`. Four
entry scripts `text/jq_*_vm.ls` (`import ~~.jq_vm`, `pn main()`) follow the
`hyphen.ls` pattern. Each entry reads the same `.jq` file as the C2MIR,
Node, Go and Python columns, then compiles and runs it.

V1 is **typed throughout** and mirrors the C port structure for structure:
- bytecode as `int[]`
- value, fork and path stacks as typed `var` arrays
- opcode dispatch as one `pn` loop

That way, the only difference between the two values in the C2MIR cell is
the language and runtime.

**Reporting (user decision 2026-10-05).** V1 gets **no column**. It is one
special comparison, so a column for every row is not justified. Its time
is printed inside the jq rows' C2MIR cell, for example
`412.3 / λ-VM 1.38s`. The first value is C2MIR, the second the typed Lambda
VM. It is stored in the results JSON under the non-engine key
`c2mir_lambda_vm`. Because that key is not in `ALL_ENGINES`, it never forms a
column and never enters a ratio or geometric mean. The README's jq note
explains the second value.

- **jq values are native Lambda values.** A jq object is a Lambda map with
  computed string keys (`{*:m, [k]: v}`, **S16.8.9**; this is not a VMap). An
  array is a Lambda array. Copy-on-write follows Lambda's COW rules (**S9.2**
  and D4.4). Whether a map with thousands of dynamic keys (`jq_mix` `kv`) and
  repeated single-key updates stays linear is exactly what this row will show.
  If it doesn't, the cause is a runtime finding to fix, not something to code
  around (CLAUDE.md rule 1).
- **Code and stacks.** Bytecode is an `int[]` with a constants array. The VM's
  value stack, fork stack and path stack are `var` arrays.
- **I/O.** The fixture is read with `input(path, 'json')`. `fromjson` uses
  `parse(str, 'json')` and `tojson` uses `format(v, 'json')`. This leans on
  Lambda's built-in JSON, which is a fair demonstration of "readily
  implementable". The C port has to hand-write the same thing; note that
  asymmetry in the README.
Rough size: 2.5–3.5k lines.

### 5.6 Lambda V2/V3: the queries written in Lambda (MIR-T and MIR-U columns)

V2 and V3 contain no jq interpreter. Each `.jq` filter is **translated by hand
into the equivalent Lambda query**, and the translated script works on the same
JSON input. It must produce the same checksum through the same computation, in
a different query language. This answers "how does the same query read and run
in Lambda".
- **V2** (`text/jq_*2.ls`, the MIR-T column) is fully typed.
- **V3** (`text/jq_*.ls`, the MIR-U column) is the same text without type
  annotations. This matches the suite's existing `x.ls` / `x2.ls` convention,
  so `mir_script_variants` picks both up with no runner change.

**Translation rules** (what "equivalent semantic query" means here):

1. **Same algorithm, same work.** A translation follows the filter's evaluation
   order and cost. It may not precompute, memoise or shortcut anything jq
   recomputes. For example, `jq_bf` keeps `bf.jq`'s character-by-character
   `skip_loop`/`backward_loop` scanning rather than a precomputed bracket-jump
   table. `jq_tree` rebuilds the tree through the path update on every round.
   Each translated block cites the jq line it implements.
2. **Generators become collections or loops.**
   - A multi-output jq expression becomes a for-expression or spread when it is
     collected (`[f]`).
   - `reduce`/`foreach` become folds, or `pn` loops with `var` accumulators.
   - `limit(n; repeat(x))`, `last(recurse(f))` and `until` become bounded
     loops.
   - Infinite jq generators become loops that stop at the same point jq stops.
3. **jq semantics are kept, not Lambda's defaults.** Where the two languages
   differ, the translation spells out jq's behaviour through a small shared
   helper module `test/benchmark/jq_query_common.ls`. These are the
   differences that matter:
   - `unique`/`group_by` sort with jq's total order, while Lambda's `unique`
     preserves order (Lambda_Sys_Func).
   - jq `/` always produces a float.
   - `null + x = x`.
   - String slices and `length` count code points.
   - Objects are maps with computed keys (**S16.8.9**).
4. **Path operations are written as explicit recursive walkers.** Lambda has
   no built-in equivalent of jq's `(.. | scalars) |= f`, `paths` and `flatten`.
   A rebuild-and-replace walker is the honest translation, and the checksum
   catches any drift.
5. **Effects follow S12.1.1v2.** Pure helpers are `fn`. Anything that mutates
   `var` state is `pn`.

Illustrative shape (`jq_records`, by-region block; exact syntax is settled at
implementation):

```lambda
// jq: group_by(.user.region) | map({region, n: length, revenue: (map(.total)|add), ...})
let groups = jq_group_by(orders, (o) => o.user.region)   // jq total order on keys
let by_region = sum(groups |> region_digest(~))
```

**Comparison value.** MIR-U vs MIR-T measures what type annotations buy on a
real query. MIR-T vs the Lambda VM measures direct Lambda against Lambda
interpreting jq. The two values in the C2MIR cell measure the Lambda runtime
against C on identical interpreter code.

---

## 6. Files and runner integration

```
test/benchmark/text/
  jq/
    mix.jq  records.jq  bf.jq  tree.jq       # single source of truth (sizes inside)
    fib.bf  orders.json                      # fixtures (fib.bf from jaq, MIT)
    vendor/jqjs.js  vendor/LICENSE-jqjs      # pristine jqjs f2894f6
    jqjs_script.js                           # generated: export lines -> module.exports/globalThis
    VENDOR.md                                # versions, commits, licences, why not npm
  generate_jq_fixture.js                     # orders.json + jqjs_script.js
  jq_mix.js  jq_records.js  jq_bf.js  jq_tree.js        # Node drivers (// @benchmark-include jq/jqjs_script.js)
  jq_{mix,records,bf,tree}.ls       # V3: untyped Lambda query (MIR-U)
  jq_{mix,records,bf,tree}2.ls      # V2: typed Lambda query (MIR-T)
  jq_{mix,records,bf,tree}_vm.ls    # V1: typed jq VM entry (shown in the C2MIR cell)
  jq_*.txt, jq_*2.txt, jq_*_vm.txt  # goldens, all "jq_x: CHECKSUM:n" with the same n
  c2mir/jq_core.h  c2mir/jq_{mix,records,bf,tree}.c
  python/jq_{mix,records,bf,tree}.py  python/vendor/purejq/   # pristine purejq 0.3.1
test/benchmark/jq_vm.ls             # V1 typed VM core
test/benchmark/jq_query_common.ls   # V2/V3 jq-semantics helpers (total order, unique, digest)
test/benchmark/go/cmd/text/jq_{mix,records,bf,tree}/main.go
test/benchmark/go/internal/bench/text_jq.go             # gojq library calls
```

Runner edits:

- `run_benchmarks.py`:
  - four `TEXT` tuples;
  - an optional **per-row timeout override** (a sixth tuple field, used for
    the jq rows, default 600 s), decided by the user;
  - **no new column**. When a row has a `<name>_vm.ls` sibling, the step
    that times the C2MIR engine also times that script with `lambda.exe`
    (pinned JIT, like MIR-T) and records it as `c2mir_lambda_vm` in the
    row. Only the four jq rows have one.
- `gen_overall_result.py`: `display_ms` appends ` / λ-VM <time>` to the
  C2MIR cell when the row has `c2mir_lambda_vm`. Ratios and geometric means
  are unchanged.
- `run_go_benchmarks.py`: `SUITES["text"]`.
- `run_c2mir_benchmarks.py`: expected lines.
- `text/README.md`: workload notes, the fairness note from §3, the meaning of
  the second value in the jq rows' C2MIR cell, and the regeneration
  command.

**Go:** gojq is pinned in `go.mod`/`go.sum` (user decision), at
`github.com/itchyny/gojq v0.12.19`. It brings the module's first third-party
dependencies (`itchyny/timefmt-go`, `clipperhouse/uax29/v2`,
`clipperhouse/stringish`), which are fetched into the runner's isolated build
directory. Golden values are produced by jq 1.7.1 and confirmed by gojq.

---

## 7. Phases

| Phase | Work | Exit check |
|---|---|---|
| P0 | **DONE.** Commit the filters with sizes inside, the fixtures, the generator and `VENDOR.md`. Recalibrate sizes on a quiet machine (`hyperfine --warmup 1 'jq -n -f …'`, aiming for ≥ 5 s on `jq_bf`/`jq_tree`). Write the goldens | jq and gojq agree on all 4 |
| P1 | **DONE.** Node (jqjs), Go (gojq) and Python (purejq) drivers. Runner registration | `run_benchmarks.py -s text -b jq` green on node/go/python, within the Q1 policy |
| P2 | **DONE.** C2MIR jq-core VM, debugged against jq. Start with `jq_tree` (paths), then `jq_bf`, `jq_mix`, `jq_records` | the four C2MIR checksums match |
| P3 | **WRITTEN, BLOCKED (§1.5).** V1: typed Lambda VM (`jq_vm.ls` + `jq_*_vm.ls`), a translation of P2; recorded in the C2MIR cell (`c2mir_lambda_vm`). Differential tests match; the full-size rows wait on LR03-37 and LR03-38 (and LR07-44 for speed). The four `jq_*_vm.ls` entries are held back until then, because the runner would start processes that exhaust memory | checksums match on both tiers (`LAMBDA_EXEC_BACKEND=jit` and default) |
| P4 | **DONE.** V2: typed hand translations `jq_*2.ls` + `jq_query_typed.ls` | checksums match on both tiers |
| P5 | **DONE (written before V2).** V3: `jq_*.ls`, the untyped translations | checksums match on both tiers |
| P6 | LambdaJS: crash fixed (§4.3, done). Re-measure jqjs under LambdaJS on a **release** build and analyse the slowdown | the LambdaJS column runs within the row timeout |

---

## 8. Decisions (user, 2026-10-05)

| # | Question | Decision |
|---|---|---|
| Q1 | Rows that exceed 120 s on slow engines | **per-row timeout override** |
| Q2 | LambdaJS column | **fix the crash**: done (§4.3); the speed analysis is P6 |
| Q3 | gojq dependency | **pin in `go.mod`** |
| Q4 | `jq_records` | **keep** as the data-processing row |
| — | Lambda versions | **three**: V1 typed VM (§5.5), V2 typed query (§5.6), V3 untyped query (§5.6) |
| — | Lambda VM reporting | **no new column**: V1's time goes in the jq rows' C2MIR cell as a second value (§5.5) |
| Q5 | jqjs optimization | **(b) local recorded patch**: done (§4.1), `patches/jqjs-lexical-scope-cow-add.patch` |

---

## Appendix A — draft filters (as prototyped in `./temp/jaq/`)

The final versions replace `$rounds`/`$depth` with `def rounds: N;` and
`def depth: N;`, with sizes from §1.

### A.1 `mix.jq`

```jq
# jq_mix: breadth workload: jaq's examples/benches filters (MIT), each reduced to an integer digest.
def repeat(g): def _repeat: g | (., _repeat); _repeat;
def digest: if type == "array" or type == "object" or type == "string" then length
  elif type == "number" then . elif type == "boolean" then (if . then 1 else 0 end) else 0 end;
def b_upto: def upto($max): if . < $max then ., (.+1 | upto($max)) end; . as $max | 0 | [upto($max)];
def b_reduce_update: reduce range(.) as $x ([[]]; .[0] += [$x]);
def b_reverse: [range(.)] | reverse;
def b_sort: [range(.) | 0 - .] | sort;
def b_group_by: [range(0; .)] | group_by(. % 2);
def b_min_max: [range(.)] | [min, max] | add;
def b_add: [range(.) | [.]] | add;
def b_kv: [range(.) | {(tostring): .}] | add;
def b_kv_update: [range(.) | {(tostring): .}] | add | .[] += 1;
def b_kv_entries: [range(.) | {(tostring): .}] | add | with_entries(.value += 1);
def b_ex_implode: [limit(.; repeat("a"))] | add | explode | implode;
def b_reduce: reduce range(.) as $x (0; . + $x);
def b_try_catch: [range(.) | try error catch .];
def b_repeat: [limit(.; repeat(1))];
def b_from: [limit(.; 0 | recurse(.+1))];
def b_last: last(range(.));
def b_pyramid: def pyramid($max): def rec: if . < $max then ., (.+1 | rec), . end; rec; . as $max | 0 | [pyramid($max)] | length;
def b_tree_contains: nth(.; 0 | recurse([., .])) | [contains(.)];
def b_tree_flatten: nth(.; 0 | recurse([., .])) | flatten;
def b_tree_update: nth(.; 0 | recurse([., .])) | (.. | scalars) |= .+1;
def b_tree_paths: nth(.; 0 | recurse([., .])) | [paths];
def b_to_fromjson: [range(.) | tojson] | join(",") | "[" + . + "]" | fromjson;
def b_ack: def ack: .[0] as $m | .[1] as $n | if $m == 0 then $n + 1 elif $n == 0 then [$m-1, 1] | ack else [$m-1, ([$m, $n-1] | ack)] | ack end; [3, .] | ack;
def b_range_prop: [{ from: 1, upto: range(-.; .), by: range(-.; .) | select(. != 0) } | ([range(.from; .upto; .by)] | length) == ([(.upto - .from) / .by | ceil, 0] | max)] | map(select(.)) | length;
def b_cumsum: [foreach range(.) as $x (0; . + $x)];
def b_cumsum_xy: [foreach range(.) as $x (0; . + $x; $x, .)];
def b_str_slice: "a" * . | [range(length) as $x | .[$x:], .[:-$x]];
reduce range($rounds) as $r (0; (. * 31 + ([ (512 | b_upto), (2048 | b_reduce_update), (65536 | b_reverse), (65536 | b_sort),
      (65536 | b_group_by), (65536 | b_min_max), (8192 | b_add), (2048 | b_kv),
      (2048 | b_kv_update), (2048 | b_kv_entries), (1024 | b_ex_implode), (65536 | b_reduce),
      (65536 | b_try_catch), (1024 | b_repeat), (1024 | b_from), (65536 | b_last),
      (512 | b_pyramid), (12 | b_tree_contains), (12 | b_tree_flatten), (12 | b_tree_update),
      (12 | b_tree_paths), (4096 | b_to_fromjson), (5 | b_ack), (24 | b_range_prop),
      (65536 | b_cumsum), (65536 | b_cumsum_xy), (1024 | b_str_slice) ] | map(digest) | add) + $r) % 1000000007)
```

### A.2 `records.jq`

```jq
# jq_records: order-processing breadth workload over orders.json (integer checksum only).
def total: reduce .items[] as $it (0; . + $it.qty * $it.price);
def safe_num(f): try (f | tonumber) catch 0;
def cents: . * 100 | floor;
def bump(f): f |= (. // 0) + 1;
def round_digest($r):
  ( map(select(.status != "pending")
        | .user.region //= "unknown"
        | .total = (total | cents)
        | .skus = ([.items[].sku] | unique)
        | del(.note)
        | with_entries(select(.value != null))) ) as $orders
  | ( $orders | group_by(.user.region)
      | map({ region: .[0].user.region, n: length, revenue: (map(.total) | add),
              top: (sort_by([0 - .total, .id]) | first | .id),
              paid: (map(select(.status == "paid")) | length) })
      | map(.n * 7 + .revenue + .top * 3 + .paid + (.region | length)) | add ) as $by_region
  | ( reduce ($orders[] | .items[]) as $it ({}; .[$it.sku] += $it.qty)
      | to_entries | sort_by([0 - .value, .key]) | .[:5]
      | from_entries | to_entries | map(.value + (.key | explode | add)) | add ) as $top_skus
  | ( [foreach $orders[] as $o (0; . + $o.total; . % 1000003)] | last ) as $running
  | ( $orders | map("\(.id),\(.user.name),\(.total)") | join("\n") | length ) as $csvlen
  | ( $orders[:200] | [paths(type == "number")] | length ) as $npaths
  | ( $orders[:200] | (.. | objects | select(has("qty")) | .qty) |= . * 2
      | [.. | numbers] | add | floor ) as $doubled
  | ( [ $orders[] | .user.tags | (.[0] // "-") + "/" + (.[1] // "-") ] | unique | length ) as $tagpairs
  | ( $orders | map(.user.name | ascii_upcase | split("R") | length) | add ) as $splits
  | ( [ $orders[:300][] | tojson ] | map(fromjson | .id) | add ) as $roundtrip
  | ( [ $orders[] | safe_num(.user.name[4:]) ] | add ) as $parsed
  | ( {} | bump(.a) | bump(.a) | bump(.b) | .a * 10 + .b ) as $counts
  | ($by_region + $top_skus + $running + $csvlen + $npaths + $doubled + $tagpairs
     + $splits + $roundtrip + $parsed + $counts + $r) % 1000000007;
. as $orders_in | reduce range($rounds) as $r (0; (. * 31 + ($orders_in | round_digest($r))) % 1000000007)
```

Fixture generator (run once; `generate_jq_fixture.js` ports this):

```jq
[range(4000) as $i | {id: $i,
  user: {name: "user\($i % 97)", region: (["us-east","eu-west","ap-south",null][$i % 4]),
         tags: [range($i % 4) | "t\(.)"]},
  items: [range($i % 5 + 1) as $k | {sku: "S\(($i*7+$k) % 50)", qty: ($k+1),
          price: ((($i*13+$k*17) % 1000) / 10)}],
  status: (["paid","refunded","pending"][$i % 3]),
  note: (if $i % 11 == 0 then "bad" else null end)}]
```

### A.3 `bf.jq`

This is jaq's `examples/bf.jq` body indented under `def bf:`, with the J-1/J-3
edits from §2.3, followed by:

```jq
. as $prog | reduce range($rounds) as $r (0; (. * 31 + ($prog | bf | explode | add) + $r) % 1000000007)
```

### A.4 `tree.jq`

See §2.4.

## Appendix B — reproduction

```bash
jq -n --argjson rounds 1 -f mix.jq                 # 148015043
jq -sR --argjson rounds 1 -f bf.jq fib.bf           # 2046
jq -n --argjson rounds 1 --argjson depth 12 -f tree.jq   # 12286
jq --argjson rounds 2 -f records.jq orders.json     # 215194764
```

Engines used: jq 1.7.1 (macOS system `/usr/bin/jq`), gojq 0.12.19 (`go install`),
jqjs git `f2894f6` on Node 22.13, purejq 0.3.1 on CPython 3.14.
