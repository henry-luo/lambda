# Lambda Review 2 — language ergonomics from the TeX port

> **Status:** Informative review, 2026-11-16. The suggestions below do not change a language ruling.
> **Basis:** Work on the Lambda Script TeX and LaTeX engine in `lmd/package/doc/latex/tex_engine/`, including comparison with the native input engine, focused tests in the interpreter and JIT, and actual bundled expl3 preload probes. The port is incomplete; this review is not a language-wide survey.
> **Spec linkage:** S1.4v2 and S12.1.2v3 (local state and control in pure functions); S7.4.1v2 and S7.4.2v4 (ordinary errors and `T^`); S17.4.1 (text-search indices); D6.3.1–D6.3.4 (precise roots); D8.2.6 (lowering); S1.8, D9.1.3v2 and D10.2.2 (the document input boundary).

## Overall impression

Lambda is well suited to representing and transforming structured document data. Map spread makes state transitions easy to read, and local mutation in a pure `fn` makes an iterative scanner practical without making its callers procedural. The biggest everyday friction was not the basic syntax. It was expressing and debugging the interaction between fallible operations, dynamic maps, and a large amount of evolving interpreter state.

The observations below distinguish a language usability concern from an implementation or tooling limitation. Suggestions are directions for investigation, not new semantics.

## 1. Error acknowledgement can obscure the operation

**Kind:** Language ergonomics and diagnostics. **Rulings:** S7.4.1v2, S7.4.2v4.

The reader ended up with conditions such as:

```lambda
if ((not current.loaded or (current.position >= len(current.line)^ and current.eol_done))^) {
    current = load_line(current)^
}
```

The explicit propagation is consistent with the rule that ordinary errors are values and must be acknowledged. In practice, though, I sometimes had to experiment with which nested expression needed `^`, especially when a map field or a value inferred as `any` flowed into a condition. The resulting code makes the scanner's control flow harder to see. The E228 diagnostic identifies an unhandled outcome, but in a compound expression it does not always make the originating operation obvious.

**Suggestion:** Improve error-outcome inference and diagnostics without weakening S7.4.1v2. In particular, identify the subexpression that introduces the error possibility, show its inferred type, and offer a precise propagation or handling location. Where a field or built-in call is proven safe, let that proof remove unnecessary acknowledgement.

## 2. Tail-recursive code is sensitive to the execution tier

**Kind:** Runtime and compiler limitation. **Rulings:** D6.3.1–D6.3.4, D8.2.6; the language does not promise unbounded recursion.

A small tail-recursive function that carries a map completed 300,000 iterations in the interpreter but reported E308 stack overflow at 200,000 in JIT. With a 16 MiB diagnostic process stack, the JIT completed 300,000. The same kind of problem affected the TeX expansion driver and a reader skipping a long sequence of comments. Local loops fixed the port's observed failures.

```lambda
fn loop(st, n) any^ {
    if (n == 0) st else loop({*:st, value:n}, n - 1)
}
loop({value:0}, 300000)^
```

The current MIR tail-call eligibility excludes boxed `Item` backedges in this case. The general runtime finding is recorded in the [Lambda issue ledger](../Lambda_Issue_Ledger.md). Expanding the optimization must preserve parameter admission and precise roots; the port's loop does not repair that compiler limitation.

**Suggestion:** Extend safe tail-call lowering to applicable open-value state, with tier-parity and forced-GC tests. Independently, make it easier to determine whether a particular tail call was optimized, so a developer can distinguish intended recursion limits from tier-specific behavior.

## 3. Text and byte positions need an easier bridge

**Kind:** Library/API ergonomics. **Ruling:** S17.4.1.

Lambda's text-search indices use code-point positions. The native TeX parser reports source spans in UTF-8 bytes. The Script reader consequently tracks character and byte positions separately and converts between them while scanning. This is a real domain mismatch: a result that is correct for ASCII can give the wrong source range after `é`, a CJK character, or an emoji.

The code-point rule is useful for ordinary text. The inconvenience is the amount of manual offset accounting needed by a parser that must interoperate with a byte-oriented native API.

**Suggestion:** Provide or document an efficient, explicit way to relate code-point positions and UTF-8 byte offsets, ideally with an incremental cursor or reusable index for repeated conversions. Keep units visible in names and results; do not silently change the index domain.

## 4. Open state maps are expressive but hard to audit

**Kind:** Language and tooling ergonomics.

The engine uses transitions such as `{*:state, field: value}` throughout. This made it straightforward to add state while porting the native interpreter. It also made the many optional fields and per-scope records difficult to audit: a reader must know which fields exist at each point and which may be absent. The box-register work, for example, needed an explicit distinction between a locally saved slot and another reference to the same box; identity alone was not enough.

That scope bug was a model error, not evidence that maps are defective. The broader usability concern is that a large dynamic state shape gives limited help in finding omitted or misspelled fields and understanding which variants a function expects.

**Suggestion:** Improve inspection and diagnostics for inferred map shapes, optional fields, and narrowing. Explore a practical way to express related state variants and required fields while retaining map-spread ergonomics. Measure the resulting code and diagnostics on an existing module before proposing a language change.

## 5. Debugging intermediate state requires instrumentation

**Kind:** Tooling.

To locate the expl3 preload failure, I made temporary copies of modules and added fields to the returned state to capture the active token, its meaning, input levels and source position. Those probes were useful, but maintaining separate instrumented copies was slow and made it important to pin the code being tested. Existing native logs and MIR dumps help with runtime and compiler faults; this particular investigation needed visibility into Script-level state transitions.

**Suggestion:** Add a way to inspect selected function arguments, results and errors, or trace selected calls, without editing the module under investigation. A compact display of inferred types and nested map fields would complement it. Preserve the ability to run the same unmodified source in both tiers.

## 6. Large-input costs need to be visible

**Kind:** Library usage and performance tooling, not a proposed semantic change.

The stream reader decoded an entire captured file on every `\read` or `\readline`. Reusing the decoded source already held by the reader preserved lazy invalid-UTF-8 handling and the existing results. In a twelve-read workload using the 2,175,362-byte bundled Unicode data file, the later two alternating release pairs fell from 12.461/12.082 seconds CPU to 10.012/9.718 seconds; output and native comparisons were identical. The first pair ran under variable host load, so those samples are not a general speed claim. The change and its evidence are in the [TeX implementation checkpoint](../impl/Lambda_Impl_Latex_Script_Engine.md).

The repeated decode was a defect in the port, not a Lambda requirement. It nevertheless shows the value of making allocation and repeated work easier to discover when a concise expression hides substantial processing.

**Suggestion:** Provide practical guidance and profiling visibility for repeated decoding, string slicing, collection construction and other allocation-heavy operations. Include large-input examples with exact-output checks.

## What worked well

- **Map spread** made most state transitions concise and made it easy to compare a change to the native engine's state update.
- **Local loops in `fn`** gave the expansion driver and reader bounded stack usage while preserving their pure interfaces (S1.4v2, S12.1.2v3).
- **Text, binary, maps and elements in one language** made it possible to compare expanded text, diagnostics, source locations and LaTeX trees in the same focused tests.
- **Explicit unsupported outcomes** kept known source-mapping gaps visible while allowing qualified features to progress.

The highest-value improvements from this work are clearer error-origin diagnostics and safe, observable tail-call behavior. The other recommendations would make the next large parser or document port easier to inspect and maintain.
