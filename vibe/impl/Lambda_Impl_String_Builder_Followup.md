# String builder: implementation audit and follow-up proposal

- Date: 2026-09-10
- Status: audit complete; extensions below are proposed, not implemented.
- Authority: S1.4/S1.6 (observable mutation and representation), S9.1.1–S9.1.2
  (finality and snapshots), D2.4.2–D2.4.3 (value descriptors and representation
  conversion), D5.3.3–D5.3.4 (precise roots), D8.2.6 (expression demand),
  D8.3.1–D8.3.3 (function entries and source-relative correctness).

## Existing implementation

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

## Reproduced limitations

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

## Proposed extensions, in implementation order

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

## Acceptance and measurement

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
