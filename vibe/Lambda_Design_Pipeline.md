# Lambda Pipeline Design — Three Pipeline Kinds over One Core (Text · Data · Binary)

**Status:** design proposal — for discussion (ledger PL1–PL11)
**Date:** 2026-07-14
**Context:** extends the streams×concurrency design (`Lambda_Design_Concurrency.md` §11, K21–K29) and the lazy-stream decisions (`Lambda_Design_Data_Processing.md` §8, D9–D12). Motivating question: Lambda's pipeline is today designed as a *data* pipeline — can the design carry three pipeline kinds: (1) CLI-style text pipelines, (2) Lambda data pipelines, (3) binary pipelines (building on the existing `binary` type)? Conclusion first: **yes, without restructuring** — the K27 Layer-1 core already absorbs all three; text costs only adapters, and the genuinely new design work is all on the binary side (six decisions, each with a named precedent already cited in §11.6.1's comparison table).

**The unifying observation (PL1):** there are only **two regimes**, not three. A text pipeline is a data pipeline whose elements happen to be strings — same queue, same operators, same contracts; the only new machinery is the adapters that produce it (byte→UTF-8 decode, line framing). A binary pipeline is the genuinely different regime, because its difference is *semantic*, not just payload: in a data/text pipeline, element boundaries are meaningful (each Item **is** a record); in a binary pipeline, chunk boundaries are **accidental** — a byte stream re-chunked arbitrarily is the *same* stream. Two binary streams are equal on their concatenation, not their chunk sequence. That one fact drives nearly everything in Part 2.

---

# Part 1 — Recap: the data pipeline, extended to text

## 1. The data pipeline as designed (D9–D12, K21–K26, K27)

Settled elsewhere; restated here as the substrate this proposal builds on.

**Surface (D9/D10):** `input()` is eager, `stream()` is lazy. A stream is a **recorded pipeline plan**, not running code — laziness is carried by the stream *value* through unchanged `|>`/`for` semantics (dispatch, not new operator semantics); terminal ops force. Value-backed streams are re-forcible values; live-I/O streams are one-shot resources, **`pn`-only**.

**Pipe operator (`|>`):** auto-maps `~` over collection elements; without `~`, passes the whole operand (aggregated pipe); `that` filters. On a stream value, the same operators build the plan instead of executing — D10's dispatch rule.

**Execution at forcing (K22–K24):** ordered by default (a parallelized `fn` stage must not reorder output); `fn` segments auto-parallelize via Stage-A fork-join, `pn` stages are sequential anchors; the executor's inter-stage plumbing uses a runtime-internal blocking send on bounded queues.

**Errors and lifecycle (D12, K25–K26):** faults surface as `T^E` at the forcing point, with `on error(e)` on the enclosing `pn`; a forced pipeline is an implicit task scope — early termination (`first(10)`) *cancels* upstream stages and closes sources (Unix SIGPIPE's structured descendant).

**The shared core (K27, four layers):**

| Layer | What | Shared with |
|---|---|---|
| 0 | uv sources/sinks (fs/net/http/stdio), built once | Node `fs.createReadStream` etc. |
| 1 | **the bounded Item chunk-queue** with completion `T^E` + cancel | executor queues (K24) · mailboxes (K21) · Node `_readableState` · WHATWG internal queue |
| 2 | surfaces: Lambda plan values · WHATWG Web Streams face · legacy Node shim | K28 commitment split |
| 3 | cross-language adapters (`stream(nodeReadable)`, `toReadable(...)`) | a few dozen lines each |

**Messaging convergence (K20e ≡ D12):** "all messages, per-sender FIFO, termination last carrying the final `T^E`" is simultaneously the mailbox contract and the end-of-stream contract. `stream(handle)` makes any task/process a stream source; the bounded mailbox is the push/pull reconciling buffer.

Everything below is *additive* to this design. No decision in this document reopens a K or D decision.

## 2. Text pipelines: a framing preset, not a third core (PL2)

**PL2 — Text is not a new pipeline kind.** A text pipeline is the Item chunk-queue carrying *string* Items, one string per record (typically one per line). All existing operators, ordering guarantees, error handling, and forcing semantics apply verbatim — `|> ~`, `that`, `group by`, terminal ops. The design should state this explicitly so no parallel "text stream" machinery ever grows.

What text *does* need is the adapter set that produces string-Item streams from byte sources — specified in Part 2 §5 as rungs of the decode ladder, because they are shared with binary:

1. **Charset decode** — incremental UTF-8 (byte stream → text), stateful across chunk boundaries (a multi-byte sequence can split across two reads).
2. **Line framing** — incremental split on `\n` (with `\r\n` normalization), carrying the partial-last-line remainder across chunks; emits one string Item per line.
3. **Source sugar** — a framing option on `stream()` so the common case is one call, e.g. `stream("app.log", 'lines')`. *(Surface spelling open — O-PL1; could equally be a stage: `stream("app.log") |> lines()`.)*

Illustrative CLI-kind pipeline (surface details subject to O-PL1):

```lambda
pn main() {
    stream("/var/log/app.log", 'lines')          // lazy text stream: one string per line
    |> that contains(~, "ERROR")                 // ordinary that-filter over string Items
    |> parse_log_line(~)                         // fn stage → data pipeline from here on
    |> group(.module)
    |> output("errors.json")
}
```

Note the ladder crossing mid-pipeline: the moment a stage maps strings to maps, the text pipeline **is** a data pipeline — no seam, because they were never different things.

**External commands in text pipelines:** piping through `grep`/`sort`/`ffmpeg` requires the raw-byte process spawn mode (PL8, Part 2 §6) plus the decode/frame adapters above. Text pipelines depend on binary Layer-0 work; that is the correct dependency direction (text sits *above* bytes), and it is why this doc specifies binary before text can be complete.

---

# Part 2 — The binary pipeline: the new regime

## 3. Semantics: the re-chunking license (PL3) and the dispatch rule (PL4)

**PL3 — Chunk boundaries are never observable semantics; byte order is pinned.** A binary stream is a sequence of *bytes* delivered in chunks whose boundaries are accidental. Spec rule: **the executor may split and merge binary chunks freely**; equality and all operator semantics are defined on the concatenated byte sequence. This is the mirror image of K22 — where data streams *pin element order* and forbid reordering, binary streams *pin byte order* and release chunking. Same philosophy (deterministic result, execution strategy invisible), applied to the regime's actual invariant. Practical consequences: the executor may coalesce small chunks between stages (K24's granularity note already batches internally — for binary this becomes *licensed at the semantic level*, not just an internal trick), and any conformance test asserting a particular chunking is a bug.

**PL4 — Binary streams do not auto-map `~`.** In a data/text pipeline, `bytes_stream |> ~ * 2`-style per-element mapping is meaningful because each element is a record. On a binary stream a chunk is *not* a record, so per-chunk auto-mapping is wrong by construction (the result would depend on accidental chunking, violating PL3). Dispatch rule (the same D10 dispatch mechanism that makes `|>` lazy on streams): on a binary stream, `|>` accepts only

- **whole-stream terminals** — `output(sink)`, `send_to(h)`, hashing/length-style reductions defined on the byte sequence;
- **framing/decoding stages** — the ladder of §5, which *convert* to a text or data stream (after which normal `~` semantics resume);
- **transducer stages** — the stateful byte-transforms of PL6 (decompress, decrypt, …), which are chunk-boundary-agnostic by contract.

A `~`-expression applied directly to a binary stream is a **compile-time error** with a fix-it pointing at the framing stages ("did you mean `|> lines()` / `|> frames(...)`?"). This keeps the PL3 invariant unrepresentable-to-violate at the surface, in the same spirit as K20c making the selective-receive trap unrepresentable.

## 4. Representation: `binary` joins the flat family (PL5) · byte-metric queues (PL7)

**PL5 — Refcounted flat buffer + zero-copy sub-views.** Today `LMD_TYPE_BINARY` is a monolithic string-like heap blob (`lambda.h:102`; grouped with strings in the type predicates). Streaming needs cheap sub-ranges — every framing stage slices, and copying per slice is quadratic folly. Design: binary becomes a member of the **flat family** (K5/K29a: "big immutable flats share by refcount"):

- a refcounted, immutable, pointer-free byte buffer (making it K5-shareable across isolates for free — a binary chunk is the *canonical* flat);
- a **sub-binary view**: `(buffer-ref, offset, len)` — the BEAM refc-binary/sub-binary split, Node's `Buffer.slice`, exactly;
- **inline-vs-refc threshold**: small binaries stored inline/owned, large ones refcounted — BEAM's 64-byte threshold as the precedent, already cited by K29b for the same split on message serialization;
- immutability is what makes the zero-copy view sound — no defensive copies, no ownership question (the C4 value-semantics story extends unchanged).

This is a representation change to an existing type, not a new type; the K29b inline-serialization rule is unaffected, and K29b's deferred shm-descriptor optimization becomes *more* natural (a sub-binary over a mapped segment is the same shape).

**Latent defect to fix with PL5 (audited 2026-07-14):** binary *literals* do not currently decode. `build_lit_string` (`build_ast.cpp:2871`) stores the raw source characters between `b'` and `'` verbatim — `b'\xDEADBEEF'` holds the 10 ASCII chars `\xDEADBEEF`, not the 4 bytes, despite `doc/Lambda_Data.md` specifying `\x` = hex / `\64` = base64 encodings (a `str_hex_decode` exists in `lib/str.c:1240` with zero callers). Meanwhile file-read binaries (`read_binary_file`, `lambda-proc.cpp:592`) hold *real* bytes — so the type has two producers with inconsistent payloads, masked because `print.cpp:449` re-emits literal chars textually (and its `%s` would truncate real bytes at the first NUL). Literal decode-at-const-build is a prerequisite of the PL5 representation work, folded into O-PL5.

**PL7 — The Layer-1 queue gains a per-element size hook; byte streams bound by bytes.** The K27 bounded queue bounds by *element count* — right for mailbox messages, meaningless for binary where one chunk Item can be 1 byte or 64 MB. Design: the queue takes an optional `size(chunk)` measure; the bound is on the **sum of measures**, not the count. Count mode = `size ≡ 1` (the existing behavior, unchanged for mailboxes and data streams). Byte mode = `size = chunk.len` for binary streams. This is precisely WHATWG's `CountQueuingStrategy` / `ByteLengthQueuingStrategy` pair, so it lands inside the K27/K28 "core shaped for WHATWG semantics by construction" commitment rather than beside it; `desiredSize` remains "remaining capacity," now in bytes for byte streams. One hook, both regimes, no second queue type.

## 5. The type ladder: incremental decoders as the inter-regime adapters (PL6, PL9)

The three pipeline kinds connect via one ladder, each rung an adapter stage:

```
        bytes  ──(charset decode)──►  text  ──(parse)──►  Items
        bytes  ◄──(charset encode)──  text  ◄──(format)──  Items
        bytes  ◄────────(binary Mark encode/decode)────────►  Items    (the direct rung, K29c)
```

**PL6 — A new stage shape: the stateful transducer with flush.** Data-pipeline stages are (mostly) pure per-element `fn`. The canonical binary transforms — **decompress, decrypt, charset decode, frame-split** — are none of that: they carry state *across* chunk boundaries, emit 0..N output chunks per input chunk, and require a **finalize step at end-of-stream** (zlib flush; the UTF-8 sequence whose last byte hasn't arrived; the partial final line). This stage shape does not exist in the current design and must be added:

- contract: `transform(state, chunk) → (state, [out...])` + `flush(state) → [out...]`, invoked exactly once at upstream completion, *before* the stage's own completion is signalled (so downstream sees flushed output, then end-of-stream — preserving the K20e/D12 ordering contract);
- errors from `transform`/`flush` are the stage's `T^E` completion — nothing new, D12 applies;
- cancellation (K26 scope cancel) skips `flush` and runs resource cleanup — matching WHATWG's `cancel` vs `flush` distinction;
- **maps exactly onto WHATWG `TransformStream`'s `transform`/`flush` transformer pair** — Layer 2's Web-Streams face gets these stages nearly for free;
- classification under K23: transducers are stateful, hence **`pn`-territory, sequential anchors** — no interaction with `fn` auto-parallelism to design, and none wanted: byte transforms are inherently sequential. *(A later refinement may recognize* stateless *byte maps as fusible, but v1 keeps the rule simple: all transducers are anchors.)*

**PL9 — All ladder rungs must be incremental; binary Mark is the top rung.** The jq lesson already recorded for P8 — "`stream('big.json')` requires the parser itself to be incremental, not just the pipeline" — generalizes to every rung: incremental UTF-8 decode, incremental line/record framing, incremental format parsers as they are ported to streaming, and eventually **incremental binary-Mark decode**. K29c's binary Mark ("length-prefix framed so the same encoding carries K27 stream chunks") is exactly the direct bytes⇄Items rung: it is what lets a *data* pipeline ride a *binary* pipe (process boundaries, files, sockets). Consequence worth recording: **the deferred binary-Mark encoding design and the binary-pipeline adapter design are one work item viewed from two ends** — when K29c's deferral lifts, it should be designed as a PL6 transducer pair from day one. Until then, text Mark over the pipe (K29c's working format) already gives a functioning bytes⇄Items rung: `parse`/`format` as transducer stages.

Standard rung inventory for v1 (each a PL6 transducer):

| Rung | Direction | State carried | Precedent |
|---|---|---|---|
| `decode('utf-8')` / `encode(...)` | bytes ⇄ text | partial multi-byte sequence | WHATWG TextDecoderStream |
| `lines()` | text → text records | partial trailing line | every CLI runtime |
| `frames(n)` / `frames(delim:...)` / `frames(prefix:...)` | bytes → binary records | partial frame | length-prefix protocols |
| `gzip()` / `gunzip()` | bytes ⇄ bytes | zlib state | Node zlib, CompressionStream |
| `parse(t)` / `format(t)` streaming variants | text/bytes ⇄ Items | parser state | jq `--stream`, P8 note |

(`frames(...)` outputs *binary records* — a data stream whose Items are binary values with meaningful boundaries. That is the escape hatch from PL4: after framing, boundaries are semantic again and `~` works.)

## 6. Layer 0: raw-byte process spawn (PL8) — the real CLI-pipeline enabler

Today `start process("child.ls", args)` speaks **Mark over the pipe** (K5/K29c) — Lambda-child⇄Lambda-parent messaging. A CLI-kind pipeline means piping through `grep`, `sort`, `ffmpeg` — arbitrary external commands that speak bytes.

**PL8 — A second spawn mode: the raw-byte process.** Same `start` keyword, same handle model, different pipe discipline:

- the child's stdin is a **binary sink** and its stdout/stderr are **binary sources** (live-I/O streams in the D10 taxonomy, `pn`-only, one-shot);
- no Mark encoding, no message framing — the runtime moves bytes;
- the handle completes with the exit status as `T^E` (nonzero exit / signal death = error value — `pipefail` on by default, inverting the Unix mistake called out in §11.6.1), **after** stdout/stderr streams complete (the K20e signal-ordering guarantee, transliterated);
- **EPIPE/SIGPIPE maps onto K26**: downstream closing its end cancels the upstream writer through the ordinary pipeline-scope cancellation — the doc already calls K26 "SIGPIPE's structured descendant," so the plumbing is designed; only the spawn surface is new;
- surface spelling open (O-PL2): a distinct builtin (`start exec("ffmpeg", [...])`) vs an option on `process(...)`. Distinct builtin currently preferred — the two modes have different contracts (message mailbox vs three byte streams) and K20's "one noun per concept" argues against overloading.

Illustrative composition (all three kinds in one pipeline):

```lambda
pn main() {
    let x = start exec("ffmpeg", ["-i", "in.mp4", "-f", "wav", "-"])   // raw-byte child (PL8)
    stream("in.mp4") |> send_to(x.stdin)                               // binary pipeline in
    stream(x.stdout)                                                   // binary pipeline out
    |> frames(prefix: 'u32le')                                         // → binary records (PL6)
    |> analyze_frame(~)                                                // fn stage → data pipeline
    |> output("report.json")
}
```

## 7. Deferred: BYOB-style pooled reads (PL11)

**PL11 — deferred perf tier, API-preserved.** WHATWG readable *byte* streams add BYOB ("bring your own buffer") readers so consumers supply the buffer and reads avoid a per-chunk allocation. Lambda's equivalent would be uv sources reading into pooled buffers surfaced as PL5 refc binaries. Deferred until profiling demands it; recorded now so the Layer-0 uv reader API is designed with a buffer-supply seam (don't hard-code allocate-per-read into the interface). PL5's refcounted-flat representation is the enabling half already being built.

---

# Part 3 — Alignment and unification

## 8. WHATWG byte streams: the conformance target (PL10a, extending K28)

K28 committed Lambda to WHATWG Web Streams as the designed-for, 100%-conformance surface. **PL10a extends that commitment to readable byte streams** — the parts of the spec this proposal makes implementable:

| WHATWG concept | This design |
|---|---|
| `ReadableStream({type: 'bytes'})` | binary stream over the Layer-1 queue in byte mode (PL7) |
| chunk = `ArrayBuffer`/`Uint8Array` view | PL5 sub-binary view — same (buffer, offset, len) shape, zero-copy by construction |
| `ByteLengthQueuingStrategy` / `desiredSize` | PL7 size hook / remaining byte capacity |
| `TransformStream` `transform`/`flush`/`cancel` | PL6 transducer contract, one-to-one |
| `TextDecoderStream` / `CompressionStream` | PL6 rung inventory (§5) |
| `pipeTo`/`pipeThrough` + `AbortSignal` | plan composition + K26 scope cancellation |
| BYOB reader / `autoAllocateChunkSize` | PL11 (deferred; API seam reserved) |
| `tee()` | value-backed streams re-force for free (D10); live-I/O tee = a small Layer-1 fan-out buffer — *open item O-PL3* |

The strategic claim of K27 holds and sharpens: because the Layer-1 core is being shaped to WHATWG semantics *by construction* (pull + bounded queue + promised next + cancel), the Web-Streams face over it stays a thin, spec-faithful veneer — including byte streams, which most non-browser runtimes get wrong or skip.

## 9. Node streams: best-effort unification through the shim (PL10b, extending K28)

K27 Layer 2 already re-bases legacy `js_stream.cpp` (~10k lines) as a compat shim over the Web-Streams-shaped core; K28 classifies legacy Node streams as **best-effort** (shim gaps fixed when a real package needs them, no 100%-pass promise). **PL10b applies that split to byte streams specifically:**

- **`Buffer` is a typed array is an Item** — the K27 payload-unification sentence covers binary: a Node `Buffer` chunk and a Lambda binary chunk are the *same* PL5 flat riding the same queue; `Buffer.slice` is the sub-binary view. Cross-language binary streaming is queue-sharing, never copying — the part only Jube can do.
- **`highWaterMark` mapping falls out of PL7**: byte streams → byte-mode bound (Node default 64 KiB); `objectMode` → count-mode bound (Node default 16). Two Node knobs, one queue hook.
- **`stream.pipeline()`/`destroy()`/`'drain'`** map to plan forcing, K26 cancellation, and bounded-queue capacity signaling respectively — the shim translates events; the core stays event-free.
- **What is *not* unified (dialect-faithful shim territory, K28 verbatim):** flowing/paused mode switching, `'data'`-listener side effects on mode, event *timing* folklore, three-generations API quirks (`readable.push` re-entrancy, `unshift`, `_writev`). These are shim behaviors regression-gated by the node baseline, never core semantics.
- **Triage rule restated for binary:** a legacy byte-stream failure in the node baseline is "shim gap — fix if a real package needs it," not a conformance bug. The conformance budget is spent on §8.

Sequencing guard (K17/K27 verbatim): build the byte-mode core under the Lambda executor first or alongside; re-base `js_stream.cpp` only against a green node baseline.

## 10. Decision ledger

- **PL1 — Two regimes, not three.** Text pipelines are data pipelines over string Items; binary is the only new regime, distinguished semantically (record boundaries vs accidental chunking). No third core, ever. §0.
- **PL2 — Text = framing preset.** All data-pipeline semantics apply verbatim to string-Item streams; text needs only the decode/frame adapters (specified as PL6 rungs) plus source sugar (spelling open, O-PL1). §2.
- **PL3 — Re-chunking license.** Binary chunk boundaries are never observable; executor may split/merge freely; semantics defined on the concatenation; byte order pinned. The K22-analog: pin the regime's real invariant, release the accident. §3.
- **PL4 — No `~` auto-map on binary streams.** Whole-stream terminals, framing/decode stages, and transducers only; direct `~` is a compile-time error pointing at framing. Makes PL3 violations unrepresentable (K20c spirit). §3.
- **PL5 — Binary joins the flat family.** Refcounted immutable pointer-free buffer + zero-copy `(buffer, offset, len)` sub-views; BEAM 64-byte inline/refc threshold (K29b precedent); K5-shareable across isolates by construction. Representation change to the existing type, not a new type. §4.
- **PL6 — Transducer stage shape.** `transform(state, chunk) → (state, [out...])` + `flush(state)` at end-of-stream (before completion signal); cancel skips flush; `T^E` errors per D12; classified `pn`/sequential-anchor under K23; one-to-one with WHATWG TransformStream. §5.
- **PL7 — Byte-metric queues.** Layer-1 queue gains a per-element `size()` measure; bound = sum of measures; count mode ≡ existing behavior; byte mode for binary. ≡ WHATWG Count/ByteLength queuing strategies; one hook, no second queue type. §4.
- **PL8 — Raw-byte process spawn.** Second `start` mode for arbitrary external commands: stdin/stdout/stderr as binary live-I/O streams, exit status as `T^E` after stream completion (K20e transliterated; `pipefail` on by default), EPIPE ≡ K26 cancellation. Surface spelling open (O-PL2, `exec(...)` builtin preferred). §6.
- **PL9 — Incremental decode ladder; binary Mark = top rung.** Every bytes⇄text⇄Items adapter must be incremental (jq/P8 lesson generalized); K29c's binary Mark, when its deferral lifts, is designed as a PL6 transducer pair — the binary-Mark and binary-pipeline designs are one work item, two ends. Until then text Mark serves as the working rung. §5.
- **PL10 — Compat commitment split, extended to bytes (a/b).** (a) WHATWG readable byte streams join the K28 100%-conformance target; (b) legacy Node byte streams stay best-effort through the shim — `Buffer` ≡ PL5 flat, `highWaterMark` ≡ PL7 modes, mode-switching/event-timing folklore stays shim-only. §8–§9.
- **PL11 — BYOB deferred, seam reserved.** Pooled-buffer reads are a later perf tier; the Layer-0 uv reader API must not preclude buffer supply. §7.

## 11. Open items

- **O-PL1** — text-source surface: framing option on `stream()` (`stream(path, 'lines')`) vs stage-only (`|> lines()`) vs both.
- **O-PL2** — raw-byte spawn spelling: `start exec(...)` builtin (preferred) vs option on `process(...)`; stderr default (stream vs inherit); env/cwd option surface.
- **O-PL3** — live-I/O `tee()`: needed for the WHATWG face; a small Layer-1 fan-out buffer with joint backpressure (slowest-reader bound) — design when the Web-Streams face lands.
- **O-PL4** — stateless byte-map fusion: whether provably-stateless transducers can lose their K23 anchor status. Deferred; v1 rule stays simple.
- **O-PL5** — `binary` literal/API ergonomics after PL5 (slicing surface on binary *values*, `len`/indexing/iteration semantics) — belongs with the PL5 representation work. **Includes the latent literal-decode defect (§4): `b'\x…'`/`b'\64…'` must decode hex/base64 to bytes at const-build time; today the raw source text is stored, inconsistent with file-read binaries which hold real bytes. Implementation plan: `Lambda_Impl_Binary.md` (B1–B8, Phases 1–6 — decode fix + copy-bridge to JS Uint8Array/Buffer; zero-copy convergence stays PL5-gated).**

## 12. Cross-reference map

| This doc | Composes with |
|---|---|
| PL1/PL2 text regime | D9/D10 (stream values, dispatch) · K22 (ordering) · full operator surface (`doc/Lambda_Expr_Stam.md` §Pipe) |
| PL3 re-chunk license | K22 (the analog: pin invariant, release accident) · K24 (internal batching, now licensed) |
| PL4 dispatch rule | D10 (dispatch mechanism) · K20c (make the trap unrepresentable) |
| PL5 flat binary | K5/K29a (flat family, refcount sharing) · K29b (64-byte threshold, shm deferral) |
| PL6 transducers | D12 (`T^E` at forcing) · K23 (`pn` anchors) · K26 (cancel skips flush) · WHATWG TransformStream |
| PL7 byte metric | K27 Layer 1 (one queue) · K20d (mailbox bound unchanged — count mode) · WHATWG queuing strategies |
| PL8 raw-byte spawn | K5/K29c (the *other* pipe discipline) · K20e (exit ordering) · K26 (EPIPE ≡ scope cancel) · D10 (live-I/O, `pn`-only) |
| PL9 incremental ladder | K29c (binary Mark, same work item) · P8 note (incremental parsers) |
| PL10 compat split | K27 Layer 2 (shim re-base) · K28 (the commitment split, extended) · K17 (protect-what's-green sequencing) |

**Sequencing note:** nothing here changes the K27 build order (core under the Lambda executor first; shim re-base against a green node baseline). Within this proposal the dependency spine is PL5 → PL7 → PL6 → (PL2 adapters ∥ PL8) → PL10 faces; PL3/PL4 are spec text with no build cost and should be written into the semantics doc alongside the first implementation step.
