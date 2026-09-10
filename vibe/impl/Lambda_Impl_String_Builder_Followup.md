# String builder: audit, implementation and measurements

- Date: 2026-09-10
- Status: stages 1–3 and the self-tail-accumulator portion of stage 4 implemented;
  whole-renderer destination passing remains a separate follow-up. Baselines and
  the focused release comparison are complete; small residual slowdowns are recorded below.
- Authority: S1.4/S1.6 (observable mutation and representation), S9.1.1–S9.1.2
  (finality and snapshots), D2.4.2–D2.4.3 (value descriptors and representation
  conversion), D5.3.3–D5.3.4 (precise roots), D8.2.6 (expression demand),
  D8.3.1–D8.3.3 (function entries and source-relative correctness).

## Existing implementation at the pre-extension audit

The earlier statement that unique internal string builders were unimplemented
was too broad. The runtime and MIR assignment optimization are present:

| Mechanism | Current source | Scope |
|---|---|---|
| Owned GC buffer | `lambda/runtime/lambda-eval.cpp`: `fn_strcat`, `string_buffer_copy_join` | Append in place when `String::is_buffer` is set and capacity suffices; geometric growth otherwise |
| Publication | `fn_string_freeze`, `transpile_ident_value` | Clear the buffer flag on an ordinary read of an owned binding |
| Assignment recognition | `transpile_assign_stam` | A non-captured local string assigned a concat rooted at that same binding |
| Nested RHS flattening | `mir_emit_string_concat_item` | Append RHS leaves to the same buffer |
| Conversion elision | `mir_emit_string_concat_item` | Decode operands whose carrier oracle reports String instead of calling `fn_string` |
| Left-associated chain recognition | `mir_collect_owned_string_concat_parts` | Up to 16 suffix parts on the left spine; multi-suffix chains require error-free string suffixes |

Tune15's builder, Tune16's nested RHS, Tune20's operand conversion, and Tune22's
chain fixtures remain in `test/mir/lambda/`. All four passed on both JIT and
interpreter during this audit (8 executions). Interpreter parity checks output,
not whether the interpreter implements the MIR builder optimization.

`lib/strbuf.*` and `lib/stringbuf.*` also exist. They serve native construction
and formatting; they do not automatically optimize Lambda `++`. D4.1.5's
standalone/owner-backed allocator distinction must not be confused with this
GC-managed String buffer. Reuse the latter for generated Lambda values.

## Reproduced limitations before this change

### Ordinary reads revoke ownership too early

`transpile_ident_value` freezes every read while `string_buffer_owned` is set.
Thus this loop freezes once per iteration, even though `len` retains no alias:

```lambda
var s: string = ""
while (i < n) {
    s = s ++ "x"
    checksum = checksum + len(s)
    i = i + 1
}
```

The next `fn_strcat` allocates and copies the prefix. Repeating that work is
quadratic in the number of fixed-size pieces. `fn_len_s` already uses the stored
length for ASCII, so these ASCII probes do not measure repeated Unicode scans.

Fresh-process, release-only kernel timings, medians of three runs:

| Appended characters | Owned local; checksum uses index | Same loop reading `len(s)` | Self-tail-recursive `grow(..., acc ++ "x")` |
|---:|---:|---:|---:|
| 10,000 | 0.066 ms | 5.687 ms | 5.831 ms |
| 20,000 | 0.130 ms | 18.474 ms | 18.640 ms |
| 40,000 | 0.233 ms | 65.915 ms | 68.756 ms |

The recursion probe checks final length; its checksum is an expected constant,
so it is not an instruction-for-instruction match for either loop. These are
mechanism/scaling probes, not benchmark speedup forecasts. The MIR confirms an
in-loop freeze for the observed case and a generic `fn_join` for recursion.
The owned control freezes at publication after the loop.

### Expression and recursive construction miss the assignment optimization

Generic `OPERATOR_JOIN` lowering calls `fn_join`, which converts operands,
allocates a result and returns it frozen. A known string type does not grant
ownership of the value or a consuming function-argument contract.

In `test/benchmark/text/prettier_ast2.ls`, `render_parts_at` passes
`acc ++ rendered.value` recursively. Its current MIR calls `fn_join` at that
site. The complete typed Prettier module has no `fn_strcat` call. Nested
`render_doc` calls also return materialized strings inside `RenderResult` maps.
Optimizing local procedural assignments therefore does not optimize this path.

The current C2MIR Prettier port passes a `RenderState*` through the recursive
walk and writes into one output buffer. It also escapes JSON directly into
that output; Lambda's `json_quote` uses five `replace` passes and a concat.
Matching output does not make these allocation strategies identical. A full
gap attribution requires profiling after each extension, not an assumption
that all remaining time belongs to strings.

### Typed operands still pay conversion and per-piece append overhead

Current `base642.ls` MIR uses the owned builder, but its main encoding loop has
four `fn_string` and four `fn_strcat` calls per three input bytes. Across the
entire encode function, including padding branches, there are nine conversion
and eleven append sites. The table's guarded pointer-array loads return boxed
Items, including absence/fallback edges. `mir_known_index_element_type` does
not publish a module string-element carrier, and conversion elision tests the
carrier oracle rather than the emitted value's available string proof.

This is a gap in proof consumption; changing the carrier oracle to claim that
boxed values are raw strings would be incorrect (D2.4.2–D2.4.3). Nor may an
unchecked type annotation erase out-of-bounds/null conversion behavior.

Even successful ownership reuse still makes one helper call, capacity test,
copy and length update per piece. Appended bytes must be copied, but four tiny
appends can share capacity and publication work when evaluation permits it.

### Log pipeline is primarily a different optimization target

`log_pipeline2.ls::make_log_line` emits 38 generic join sites across timestamp
construction and both output branches. This misses expression construction,
but `build_logs()` runs before `t0`: it cannot explain the timed processing
loop's string costs. That loop performs split, token searches, slices, numeric
conversion and record operations. Its C port parses spans of the existing
input and uses enum-like fields. Builder improvements may reduce setup time;
slice/parser representation work is a separate proposal and measurement.

## Approved implementation plan

1. **Preserve ownership through proven observations.** Start with `len(s)`:
   consume the rooted String without ordinary publication/freezing. Represent
   non-retention explicitly at the consumer boundary; do not globally suppress
   identifier freezes. Extend only to audited scalar-returning observers whose
   evaluation cannot expose the buffer. Joining branches must conservatively
   merge ownership state. Aliases, captures, container stores, unknown calls
   and returns still publish immutable values. Preserve Unicode length semantics.

2. **Use boxed-string proofs and batch safe appends.** Consume the semantic
   contract independently of the physical carrier. A proven String Item can
   decode inline; otherwise use a tag-guarded fast path with existing conversion
   behavior on the slow path. Preserve null, errors and out-of-bounds behavior.
   Extend the existing runtime append machinery with shared reserve/copy logic
   so eligible pieces reserve once, append together and publish once. Optimize
   known one-byte pieces only when their length/encoding is proved. All evaluated
   pieces remain precisely rooted; argument effects and allocation failures
   must preserve observable evaluation order. Do not hard-code Base64 or its
   four-piece shape.

3. **Build ordinary string expressions into one destination.** For an eligible
   `a ++ b ++ c` expression, construct a fresh private buffer or size-then-build
   result and freeze once. Reuse the existing concat walker and runtime growth
   helpers. This covers returns, call arguments and `let` initializers, beyond
   `s = s ++ ...`. Mixed joins and uncertain conversion/error cases retain
   generic semantics. Use producer/consumer facts under D8.2.6; do not reassociate
   overloaded `++` from syntax alone.

4. **Lift proven recursive accumulators into private builders.** Start with
   self-tail recursion: copy a potentially shared incoming accumulator once,
   carry a private buffer across the lowered backedge, then freeze on exit.
   Require that no prior accumulator escapes or is subsequently observed in a
   conflicting way. This targets `render_parts_at` without changing public
   function parameter semantics. Next investigate destination passing across
   a closed recursive renderer call graph, avoiding materialized child strings
   and eventually transient RenderResult objects. The latter depends on record
   escape analysis; it is a separate, larger stage. Keep D8.3.1's single unboxed
   version constraint; multiple public/native specializations require a ruling
   revision and are not assumed here.

## Acceptance and measurement plan

- Add counters through existing opt/copy diagnostics for append calls, in-place
  appends, growth copies/bytes, freezes and generic concat allocations. Counters
  supplement MIR checks; they must be disabled for timing.
- Pin that `len` does not freeze inside the eligible loop, while actual snapshots
  still do. Repeated appends after observation must retain geometric allocation
  growth; the fixed-size-piece probes should scale approximately linearly.
- Verify snapshot aliases, self-concat, captures, branch merges, exceptions,
  mixed/nullable operands, out-of-bounds table reads, Unicode, embedded NUL and
  forced moving GC. Add matching expected output for every new Lambda fixture.
- Pin typed operand fast paths and the surviving conversion/error slow paths.
  Count bytes copied on recursive accumulators rather than testing only the
  presence of a builder call.
- Run Lambda and Test262 baselines after compiler/runtime changes. Measure
  unchanged Base64 and typed/untyped Prettier against the cached release with
  interleaved samples and correctness verification. Report log setup separately
  from the existing timed processing kernel. Do not rewrite Result41 with these
  synthetic probes or claim a full benchmark run.

Audit artifacts: `temp/string_builder_audit/probe.py`, `measurements.json`,
per-run outputs and finalized MIR. Timing used `--no-log`, forced JIT, disabled
MIR cache, no profiling; MIR was collected in separate untimed runs. All 27
timed executions returned expected lengths/checksums. Binary:
`test/benchmark/exe/lambda-tune23-23697ee47d8e`, SHA-256
`23697ee47d8efc45feea5f264e1e74e2e3dd6c8481b70cd41d215eb398f96672`.
Its recorded emitter/runtime source hashes matched the live files when audited.
Concurrent root-witness instrumentation edits appeared afterwards; they are
outside this audit and were not included in the cached-binary measurements.

## Implementation record — 2026-09-10

The implementation preserves S1.4/S1.6 and S9.1.1–S9.1.2: only an unpublished
buffer can grow in place. No language semantics or public function signature
changes are needed.

| Stage | Implemented behavior | Evidence |
|---|---|---|
| 1: non-retaining observation | Direct local `len(s)` reads the live rooted string without freezing it. A binding-wide may-own fact survives branch joins and loop backedges; ordinary reads and closure captures freeze the actual runtime value. | `string_builder_observer`, `string_builder_snapshots`; counter and forced-GC checks |
| 2: typed/boxed operands and batching | String Items decode inline; uncertain single pieces retain a conversion fallback. Eligible chains of up to 64 leaves call `fn_strcat_many`, sharing length/capacity checks and copies. Existing two-piece appends retain `fn_strcat`; a checked one-byte fragment copies with one store. | `string_builder_expressions`; updated Tune16/Tune22 MIR fixtures; NUL-byte and Unicode checks |
| 3: whole expressions | Eligible expressions with at least three string leaves allocate one exact-sized frozen result. Nullable indexed leaves use an all-string guard and replay the original join tree on the fallback edge. Binary expressions retain their existing path because there is no intermediate allocation to remove. | Fresh return and call-argument expressions; nullable/out-of-bounds fixtures; MIR size ratchets |
| 4: self-tail accumulators | A final, plain-string parameter can carry a private builder across a self-tail backedge. Earlier arguments evaluate first; reads that retain the accumulator freeze it. An incoming shared string is copied by the first append. | `string_builder_tail`; output, copied-byte and snapshot checks |

`lambda/runtime/lambda-eval.cpp` shares the reserve/copy implementation between
the binary and variadic helpers. The binary API instantiates the utility with
its fixed count of two, allowing the release compiler to remove the dynamic
loop and wrapper without duplicating source code. Every source survives allocation through precise
`RootSpan` homes (D5.3.3–D5.3.4), with the slot base acquired once per allocation.
The shared piece-copy utility uses a byte store when the stored length is one,
including a NUL byte; multi-byte fragments retain the general copy.
Pointer-equal self-appends allocate a new
destination. Byte lengths preserve embedded NULs; the ASCII flag and existing
Unicode length behavior are preserved. Profiling uses one disabled-profile
branch per successful append. `string_growth_copies` counts both initial/fresh
allocations and actual capacity growth; `string_copied_bytes` includes prefix
and suffix bytes copied by these helpers.

The compiler consumes field/array contracts separately from their physical
carriers (D2.4.2–D2.4.3). Proven or tag-admitted strings decode with a pointer
mask, without repeating the text helper's general tag checks. Forward call return proofs now use the resolved function
binding (D8.2.4), rather than a stale identifier function type. Tail lowering
also needed three correctness repairs exposed by the renderer and new fixtures:

- A proven string accumulator's pure block inherits tail position at its actual
  final expression after local declarations. A value followed by another
  declaration is not a tail call; other block recursion protocols retain their
  existing lowering.
- Callees and argument expressions do not inherit the enclosing call's tail
  position. `wrap(recurse(...))` must still execute `wrap`.
- Tail arguments that alias parameter registers are snapshotted before any
  parameter is overwritten. The new builder backedges enforce argument
  admission; this change does not expand other TCO admission protocols.

These changes stay within MIR Direct and the existing entry plan
(D8.2.6, D8.3.1–D8.3.3).

### Validation

- `make test-lambda-baseline`: **5,276/5,276**, including 91 MIR checks,
  16 MIR size ratchets, 16 optimization contracts and 124 forced-GC cases.
  The socket fixtures require local loopback access; the complete passing run
  used that permission after the sandbox-only run reported `bind` EPERM.
- `make test262-baseline`: **40,261/40,261**, zero regressions.
  An earlier final-build run classified the large Unicode-identifier case as
  slow under parallel load; its isolated retry passed in 1.7 seconds. The final
  complete rerun had no slow/partial cases. No harness or baseline was changed.
- All four new fixtures also passed JIT/interpreter output parity with forced
  moving GC and freed-memory poisoning: eight additional executions.
- Coverage includes aliases, branch publication, captures, self-concat,
  Unicode, embedded NUL, left-to-right effects, recursive errors, nullable
  table reads, prior-accumulator snapshots and nested non-tail calls.

### Remaining scope and C2MIR comparison

The closed-renderer investigation confirms that `prettier_ast2.ls::render_doc`
still returns materialized child strings inside `RenderResult` maps. Its C
benchmark reference instead carries a `RenderState*` through the entire walk
and writes directly into one destination. This change optimizes the sibling
accumulator; it does not remove child results, transient records, or the five
`replace` passes in Lambda's JSON quoting.

Whole-renderer destination passing therefore needs escape/effect analysis and
an internal result protocol preserving errors, snapshots and column updates.
Record scalar replacement is a separate step. These are not implemented here,
nor are additional observer summaries, SIMD append specialization,
or cached Unicode character counts. Repeated `len` on a growing non-ASCII string
can still rescan its contents even though builder copying itself is linear.
The existing public typed entry remains unchanged (D8.3.1); this does not
establish that C-equivalent allocation behavior requires a language change.

## Final release comparison

The control is the same source revision without this change, rather than an
older Result snapshot. Both binaries are release builds; timing uses forced
JIT, disabled MIR cache, `--no-log`, and disabled profiling. One warm-up per
binary precedes five alternating A/B pairs (three pairs for the slower log
controls). All **180 executions** passed output/checksum verification: 148
measured samples and 32 warm-ups. These are focused measurements, not a full
ResultN run; Result41 is unchanged.

| Benchmark kernel | Before (ms) | After (ms) | Before / after |
|---|---:|---:|---:|
| Base64 typed | 13.644 | 10.095 | 1.352× |
| Base64 untyped | 19.397 | 16.736 | 1.159× |
| Prettier typed | 2,669.690 | 2,573.750 | 1.037× |
| Prettier untyped | 1,109.220 | 1,117.800 | 0.992× |
| Log pipeline typed | 6,371.740 | 6,372.110 | 1.000× |
| Log pipeline untyped | 6,316.940 | 6,367.880 | 0.992× |

The untyped Prettier and log medians are slightly slower (about 0.8% in
this run). They remain measured residuals; the implementation does not claim
that every workload improved. The log ranges overlap, so its small median
change should not be treated as strong causal evidence. Raw samples and
whole-process times are retained in the JSON.

| Appends | Owned loop before → after (ms) | With `len` before → after (ms) | Recursive before → after (ms) |
|---:|---:|---:|---:|
| 10,000 | 0.066 → 0.058 | 5.194 → 0.067 | 5.312 → 0.082 |
| 20,000 | 0.128 → 0.113 | 18.301 → 0.130 | 19.087 → 0.163 |
| 40,000 | 0.258 → 0.216 | 56.564 → 0.251 | 58.565 → 0.311 |

The separately instrumented `build_logs()` setup falls from
**35.363 ms to 34.518 ms**. This diagnostic adds clock reads around
setup and prints that result after the original kernel timer. It is distinct
from the unchanged benchmark sources and their kernel rows above.

Separate untimed diagnostics confirm the mechanism:

- The 128-append observer fixture records **125 in-place appends**, **3
  allocations/growth copies**, **193 copied payload bytes**, and **zero freezes**.
- Both 40,000-append scaling probes copy approximately **106 KB** including
  output construction, rather than recopying the growing prefix each time.
- Typed Base64 processes **1,333,506 suffix pieces in 333,406 append calls**,
  with **332,400 in-place appends**.
- Typed Prettier records **278,784 in-place appends** and **38,547,763 copied
  bytes**; the untyped variant records no in-place appends and 101,716,787 bytes.
  The typed renderer still makes **6,692,099 union admissions**. Its remaining
  contract validation and child-result construction costs need separate tuning;
  reduced copying alone does not give it C-like overall performance.

Artifacts:

- Results, raw timing samples, source/binary hashes and diagnostic counters:
  `test/benchmark/benchmark_string_builder.json`.
- Final cache: `test/benchmark/exe/lambda-string-builder-d4decadd2b78`.
  SHA-256: `d4decadd2b78e9e41da1ddd34e2f7ac13288dc096b727485011b78275b949bb4`.
- Matched control: `test/benchmark/exe/lambda-string-builder-before-99ab74b961d1`.
- Source revision: `17258a5429148dd9486ebbb9a618f64b855c0408` plus the recorded runtime patch.
- Raw logs, MIR, scripts and forced-GC parity: `temp/string_builder_impl/`.
