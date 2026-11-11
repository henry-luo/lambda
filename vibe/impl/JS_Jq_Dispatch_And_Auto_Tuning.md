# jq dispatch and AUTO promotion tuning

**Spec linkage:** D7.4.1, D8.4.2, D8.4.4, D8.4.5. User authorized continuing the
jq optimization proposals on 2026-11-11.

## Completion record

- Completed 2026-11-11 in `/Users/henryluo/Projects/lambda`, base `f379c940c`
  plus the preceding compact-generator/trampoline work.
- Implemented source receiver predictions for method reads/inlining, checked
  ordinary classic methods and arrows in AUTO, method-owned function-expression
  closures, and ordinary iteration. Captures, metadata, strictness, live globals
  and guarded semantic misses retain their ownership.
- Fixed the pre-existing AUTO batch harness lifecycle exposed by qualification;
  the Test262 runner and its baseline are unchanged. A permanent regression
  covers JS harness/includes and native harnesses across three fresh realms.
- Lambda baseline passes 7,384/7,384; after the batch correction the final
  JavaScript gate passes 573/573 and selected Test262 passes 9,192 cases in
  each of hybrid and forced threshold-one AUTO modes, zero regressions.
- Final release AUTO measurements improve records 1.25x and isolated group_by
  2.81x over the compact/trampoline control, with identical checksums. The
  fifth-class MIR method cliff is removed. MIR jq query times remain roughly
  flat, and native compilation still costs about 7.4 s.
- The unsuccessful foreign own-data adapter experiment was removed. General
  shape propagation, wider generator closure admission, activation policy and
  general generator local spilling remain separate follow-on work.
- `lambda.exe` is the final release host; exports, formal-index and whitespace
  checks pass. Its SHA256 is
  `9ebcbc23ddb4a13a6d50287456be490775746b22209f53f948fac20f5b64e728`.
  Evidence, measurement scopes and the batch correction are detailed below.

## Verification plan

Exercise four/five/many-class dispatch, own shadows, argument-time replacement,
prototype mutation, borrowed receivers and proxy fallback. For AUTO, check hot
thresholds, AST selection, original callable metadata, strict receiver behavior,
live globals, nested MIR closures, GC, and rejected outer/lexical captures.

Measure compilation separately from query execution. Reuse unchanged one-round
jq diagnostic inputs/checksums; identify these as diagnostics rather than the
canonical multi-round suite. Preserve the original follow-up analysis unchanged.

## Retained implementation

The compiler reuses constructor provenance, including arguments at visible
direct call sites, to select a small set of possible receiver classes. Both
method-property loading and guarded body inlining use that selection before
their four-target budget. Own-property shadows, prototype/descriptor changes,
borrowed receivers and unknown calls retain the canonical miss. Predictions
are immutable; no inline cache or receiver profile is introduced (D7.4.1).

AUTO admits ordinary public classic instance methods through the existing
method proof, including synchronous contained function expressions whose
captures are created inside the compiled invocation. Capture-free classic
arrows may promote at script level or under one wrapper, including a generator
wrapper. Ordinary for-of/for-in bodies reuse the shared iterator kernels;
compound heads remain excluded. These extensions cover jq's
`FunctionCall.apply`, sorting comparators and `compareValues`. Original
identity/home/source, inherited strict mode, live global cells and existing
AST environments keep their ownership (D8.4.4, D8.4.5).

## Release measurements

2026-11-11, macOS arm64, release O3/full LTO. Control is the preceding compact
generator/trampoline build, not Result55. Each jq row is one paired process
run, with control/candidate order alternated by workload and no overlapping
builds/tests. Query timers exclude input/filter preparation and whole-script
MIR compilation; AUTO compilation triggered during a query is included.
These are one-round diagnostics, not canonical multi-round benchmark scores.

| Query | Backend | Control | Candidate | Speedup |
|---|---|---:|---:|---:|
| records | AUTO | 4.281 s | 3.438 s | 1.25x |
| isolated group_by | AUTO | 10.886 s | 3.872 s | 2.81x |
| records | MIR | 2.626 s | 2.578 s | 1.02x |
| isolated group_by | MIR | 5.275 s | 5.296 s | 1.00x |
| Brainfuck | MIR | 10.819 s | 10.841 s | 1.00x |
| tree | MIR | 23.659 s | 23.241 s | 1.02x |

Checksums match: records `100474837`, group_by `2`, Brainfuck `2046`, tree
`393214`. MIR differences are small and do not establish a broad MIR jq gain.
AUTO process wall time also improves: records 4.414 to 3.566 s, group_by
10.963 to 3.953 s. AUTO rows were rechecked on the final host after the
batch-entry fix; other rows use the measured optimization build below.
No isolated memory saving was measured in this milestone.

The mechanism probe calls the same `C0.apply` 500,000 times, changing only
the number of class definitions with that spelling. Median of the last three
of four passes within one process, checksum `125003250000` throughout:

| Classes | MIR control | MIR candidate | AUTO control | AUTO candidate |
|---:|---:|---:|---:|---:|
| 4 | 7.809 ms | 7.271 ms | 83.535 ms | 53.329 ms |
| 5 | 17.411 ms | 7.439 ms | 79.934 ms | 52.170 ms |
| 32 | 17.451 ms | 7.131 ms | 80.425 ms | 51.828 ms |

The fifth-class cliff is removed. The corresponding MIR gains are 2.34x at
five classes and 2.45x at 32. These are mechanism results, not whole-jq gains.

A separate compiler-timing diagnostic on records reports about 7.4 s of
compilation in either build: candidate link/code generation 6.229 s, emission
1.097 s, planning 0.036 s, parsing/binding/validation 0.009 s. The remaining
startup bottleneck is native code generation. Logs are separate from the
uninstrumented runtime comparisons.

Artifacts: `benchmark.py`, `jq-results.json`, `mechanism-results.json`,
`compile_timings.py`, `compile-results.json`, `final-host-results.json` and
`binary-sha256.json`, all under
`temp/js_jq_dispatch/`. The measured optimization candidate SHA256 is
`326596365920eb47ee106a67b4f023231f878c0dfd39679dd7ae05ab46ba1c00`;
control is `6bf6733ebb63318ab302fc1d468f9b4b9392a4b706b94aeaa69a59cf1df2cc92`.
The final host adds the batch-entry correction, SHA256
`9ebcbc23ddb4a13a6d50287456be490775746b22209f53f948fac20f5b64e728`.
The final AUTO recheck reproduces the gains with matching checksums.

## Qualification

- Lambda baseline: 7,384/7,384 before the batch-entry correction, including
  275/275 script-ownership and 857/857 MVP cases.
- Final JavaScript gate, including the new permanent AUTO batch regression:
  573/573. The sandboxed media test truncates identically on the control;
  it passes with the baseline's macOS media-service access. No media or
  sibling-project code was changed.
- Final selected Test262 gate: 9,192 passed in normal hybrid selection and
  9,192 passed with AUTO forced at threshold one. Both have 2,340 existing
  exclusions and zero regressions. The runner and baseline are unchanged.
- The AUTO batch fix follows D8.4.2. Previously its first JS-harness case
  passed, then two later arrow cases lost `assert` on the control as well.
  AUTO now restores retained AST harnesses/includes in each realm, forwards
  the native-harness flag through its normal provider, and retires AST body
  generations. It preserves normal per-source MIR fallback. The three-case
  reproduction and the permanent JS/native harness regression pass.
- Host exports: 284/284; formal index and diff whitespace checks pass.

Relevant logs under the artifact directory: `lambda-baseline-final.log`,
`js-golden-final-complete.log`, `test262-hybrid-final.log`,
`test262-auto-fixed.log`, `auto-harness-control.log`,
`auto-harness-candidate-after.log` and `auto-batch-regression.log`.

## Remaining performance work

Source record shapes still need to flow through mapping results, arrays and
comparator parameters. AUTO callbacks still pay a substantial generic call
cost, and generator functions with contained callbacks remain outside the
whole-definition promotion slice added here. General generator local spilling
and the activation allocation/policy proposals remain separate work. The
discarded own-data adapter shortcut does not count as an implemented gain.
