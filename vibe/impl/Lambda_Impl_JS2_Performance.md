# LambdaJS performance: shared slot initialization

> **Status: IMPLEMENTED; EXISTING BASELINE FAILURES REMAIN — 2026-09-10.**
> Implements the first performance step
> in [JSCU37](../Lambda_Design_Structs_JS.md#10-jscu37--one-construction-and-slot-initialization-mechanism)
> following the user's implementation request. Authority: **D3.4.5–D3.4.6**
> (shape/storage consistency), **D3.4.4v2** (name identity), **D4.6.1v2**
> and **D5.1–D5.4** (precise ownership and reusable-code boundaries).
> Base source: `57addc5cf`. This record concerns the bounded construction
> repair; the full JSCU36–43 extraction program remains open.

## Root cause and behavior

Result41's predicted literal initializer rejected every null value. The caller
saw an existing field in the predicted shape and constructed a complete
four-field property descriptor. A leaf `{left: null, right: null}` therefore
created descriptors for both ordinary field initializations.

The original initializer also treated every null-typed slot as unwritten.
The unchanged control release reproduces a correctness failure:

```js
function make(v) { return { value: v, tail: 7 }; }
const first = make(null);
const second = make(1);
console.log(first.value, second.value); // control: 0 1; required: null 1
```

The slow descriptor path did not guarantee detachment for an already-null
field. A later instance upgraded the shared field to an integer, causing the
earlier object's zero storage to read as zero. Merely removing null rejection
would retain this bug. The fix must preserve both values and the collector's
interpretation of each stored lane (**D3.4.5–D3.4.6**).

## Implementation

`js_predicted_slot_initialize` admits only mutable plain maps with a shared
fixed-slot shape, an identity-resolved string key, default descriptor flags,
valid storage bounds, and a non-reserved slot.

1. An explicit null initialization turns a constructor-style predicted
   blueprint into an immutable shared transition root. Existing TypeMap flags
   express this; no field, value representation, or presence mask is added.
2. Compatible values use the existing named-slot writer, including null.
3. A blueprint that has never published null can still learn its first
   compatible non-null lane, preserving the established literal fast path.
4. Incompatible lanes use **`fn_map_set`**, the existing Lambda storage writer.
   Its transition/detachment machinery preserves sibling instances and reuses
   transition shapes. JS no longer constructs a descriptor for this case.
5. The helper reports refusal separately from an error completion. The caller's
   existing precise roots retain the object, key and value across allocating
   fallbacks; errors propagate through `JS_RETURN_IF_ERROR`.

Accessor/attribute mutations, deleted fields, reserved constructor fields,
special keys and exotic objects retain their existing semantic admission.
CreateDataProperty still bypasses inherited setters. This implements part of
JSCU37 using the existing Lambda writer; it does not claim that both frontends
now share a full construction planner or allocator interface.

The existing paired benchmark runner now accepts `--language js`, reuses the
benchmark manifest and JetStream wrapper, and records source hashes alongside
binary hashes and individual samples. Its default remains Lambda MIR.

## Validation

The full MIR JS run completed **472/494**, with 22 failures. These comprise
eight distinct existing semantic/library failures and seven networking
fixtures, each of which is also invoked by a named regression test. Replaying
all 15 failed scripts on the pinned control and candidate produced identical
stdout, stderr and exit status (`temp/js2/js-failure-comparison.json`). The
networking failures include `bind EPERM 127.0.0.1:0` and a DNS failure in the
restricted environment. No golden or baseline was changed.

The Test262 candidate and unchanged control both completed **40103/40261**.
Their **158 failed test names match exactly**, with zero additional candidate
failures and zero recoveries (`temp/js2/test262-comparison.json`). This is not
a passing Test262 baseline: the checkout already fails those recorded cases.
`make test-lambda-baseline` completed **5231/5243** outside the sandbox:
input **2104/2104**, runtime **3127/3139**. The failures are eight already
replayed JS semantic/library cases, the DNS fixture twice, and two AST
JSON-holder tests. Both pinned binaries also crash with exit `-11` on the DNS
fixture outside the sandbox (`temp/js2/dns-unrestricted-comparison.json`).
Extracted reproductions of the
two JSON-holder cases give identical failing results on the pinned control and
candidate; Node returns the required answers
(`temp/js2/json-holder-comparison.json`). No failure is hidden or removed.

Relevant passing suites include Lambda scripts **851/851**, MIR GC stress
**118/118**, MIR emission **88/88**, JS MIR emission **21/21**, JS optimization
**19/19**, JS coercion **15/15**, and MathLive markup **921/921**.

The new `test/js/js_predicted_null_slots.js` fixture has a matching `.txt`
golden verified with local Node. It covers null-first and number-first sites,
mixed scalar/container values, signed zero/NaN, sibling mutation, descriptors,
freeze, deletion/reinsertion, inherited setters, abrupt field evaluation and
recursive allocation with retained trees. The unchanged control fails its
first sibling-value assertion; the candidate passes the focused MIR tests.
The complete new fixture also passes with `LAMBDA_GC_FORCE_EVERY=1,7,31,100`,
explicitly selecting MIR. These are focused ownership checks, not a claim
that unrelated broad forced-GC failures from earlier work are resolved.

## Measurement protocol

Control and candidate use the same release-native build configuration,
toolchain and host. `make build-release-compile` is the release compilation
target, with TMPDIR under `temp/js2/build-tmp`. Binaries are pinned under
`temp/js2`; debug builds are never used for timing.

After the baseline's debug build, remove the generated `lambda.exe` before
relinking release. Both configurations share that output path, and make can
otherwise consider the newer debug executable up to date against release
objects. This was detected by the changed executable size/hash before running
any follow-up timing. The archived release binaries remain the A/B inputs.

The pair runner alternates control/candidate order and retains every sample,
status and timing-stripped stdout digest. Execution intervals come from each
benchmark's `__TIMING__` marker; process wall time is recorded separately.
Compilation/startup and `gcbench`'s final long-lived-tree check lie outside its
own execution marker. Workload sources and iteration counts are unchanged.

No historical Result38/41 timing is used as the paired control. The attempted
macOS sampling profile could not attach to the control process; no sampled
hot-path percentage is claimed. The performance attribution below is to
the complete bounded patch, including descriptor avoidance and transition
reuse, rather than either component in isolation.

### Initial paired release result

Five alternating pairs per row; macOS arm64, on AC power. The full samples and
environment are in `temp/js2/paired-js.json`. All 65 pairs completed with
matching timing-stripped stdout. Each of the three object-heavy improvements
won all five pairs.

| Workload | Control median ms | Candidate median ms | Control / candidate |
|---|---:|---:|---:|
| BENG binarytrees | 739.534 | 36.949 | **20.02×** |
| Larceny gcbench | 17143.555 | 901.779 | **19.01×** |
| JetStream splay | 1632.116 | 352.084 | **4.64×** |
| R7RS fib | 50.964 | 48.553 | 1.05× |
| R7RS fibfp | 42.968 | 43.411 | 0.99× |
| R7RS sum | 29.119 | 28.067 | 1.04× |
| R7RS sumfp | 2.720 | 3.002 | 0.91× |
| AWFY list | 1.887 | 1.892 | 1.00× |
| AWFY storage | 4.996 | 5.198 | 0.96× |
| AWFY nbody | 544.774 | 557.379 | 0.98× |
| AWFY richards | 988.481 | 1009.196 | 0.98× |
| AWFY deltablue | 478.742 | 482.582 | 0.99× |
| BENG spectralnorm | 35.714 | 35.796 | 1.00× |

Execution ranges were 670.989–757.491 / 36.789–42.302 ms for binarytrees,
16910.985–17194.473 / 862.369–942.363 ms for gcbench, and
1615.201–1633.421 / 337.509–414.474 ms for splay (control / candidate).
The short sumfp row was 10.4% slower in this initial sample. A 21-pair follow-up
on the rebuilt candidate reduced that difference to 1.7%; storage changed from
4.0% slower to 5.4% faster. All 42 follow-up pairs matched stdout
(`temp/js2/paired-followup.json`). These rows do not establish a general
numeric-loop improvement, and the initial 10.4% slowdown did not reproduce at
that magnitude.

The JetStream splay wrapper emits timing without a result checksum. Its
matching stdout is a weaker correctness signal than the tree checksums;
the behavioral gates carry the semantic validation.

A second tree run on the final rebuilt release used three alternating pairs.
All nine pairs matched stdout and favored the candidate
(`temp/js2/paired-final-trees.json`):

| Workload | Control median ms | Final candidate median ms | Speedup |
|---|---:|---:|---:|
| binarytrees | 739.141 | 36.675 | **20.15×** |
| gcbench | 16928.088 | 910.278 | **18.60×** |
| splay | 1676.669 | 400.199 | **4.19×** |

The final `lambda.exe` is the release binary with the same hash as the final
candidate below. No debug executable was used for either paired experiment.

Pinned binary SHA-256:

* Control: `632b5ce1ea6c92971d70748670d69385bb59f73b5839eca0a3e9d363c0149ffb`.
* Initial candidate: `fe6ef9777098a1eeb38ce2e67d570880120171f51de5594a24c9c1b1645b275e`.
* Rebuilt candidate after comment corrections:
  `ee00fe61d7b906ed770b71888ff7cdefae3c201acd325fdcc5a7b6b6581d49a7`,
  pinned as `temp/js2/lambda-candidate-final` and used by the Test262 gate.

```sh
python3 test/benchmark/run_paired_benchmarks.py --language js \
  --control temp/js2/lambda-control --candidate temp/js2/lambda-candidate \
  --bench binarytrees,gcbench,splay,richards,deltablue,list,storage,fib,sum,nbody,spectralnorm \
  --pairs 5 --output temp/js2/paired-js.json
```

## Sharing and remaining work

This change reuses the existing named-slot writer and Lambda `fn_map_set`;
it adds no C++ carrier, storage descriptor, GC protocol or shape-cache family.
The four affected C/C++ files have 56 physical added lines and 67 deleted lines,
including comment corrections. The net line reduction is not a claim that
eleven lines of duplicated implementation were removed.

The full shared construction interface, shared candidate-shape propagation,
native field/index lowering and direct-call completion work remain in the
JS2 proposal. The measured gains close the targeted null/transition allocation
problem, not the broader JSCU36–43 program or the existing conformance failures.

## Subsequent field/index work

The construction measurements above are historical and use base `57addc5cf`.
Tasks 1 and 2 now continue JSCU38–JSCU39 against `a436d168f`; the shared candidate
walk, emitted guards, native indexing, validation and matched measurements are
recorded in [Lambda_Impl_JS2_Guarded_Access.md](Lambda_Impl_JS2_Guarded_Access.md).
