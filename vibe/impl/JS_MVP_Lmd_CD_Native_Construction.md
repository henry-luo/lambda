# JS MVP CD native construction and calls

Date: 2026-10-09. Follow-up to [CD allocation and shape stability](JS_MVP_Lmd_CD_Allocation.md).

## Diagnosis and implementation

CD already mutates objects in place. Its remaining costs include repeated guards,
generic constructor field stores, argument-dependent layout changes, and boxed
boundaries around small numeric methods. The incoming diagnostic recorded 82,438
slow initialization stores for the tree node's alternating Boolean/Map value.

- Recursive construction sites retain four argument-kind layout variants. Every
  entry still carries the class allocation epoch; ordinary stores validate the
  selected layout. Numeric subtypes may share a key and use ordinary retyping.
  This is a layout hint, never a type proof. Sites without nullable links retain
  their existing single entry (**D3.4.3v5**, **D3.4.5**, **D8.4.1v2**).
- Extend the existing `scalar_constructor_plan` for concrete numeric/Boolean
  arguments in proven numeric regions. Only small, receiver-unobserving,
  non-derived constructors with unique fields and forwarding/literal
  initializers qualify. Replay the layout through Lambda's runtime shape tree,
  starting at the class root, then allocate through `mvp_lmd_object_new` and
  initialize through `field_write`. Constructor identity, argument evaluation,
  property order, real object identity and generic fallback remain intact.
  Native doubles need no boxing just to validate their already-known lane
  (**D3.4.3v5**, **D3.4.5**, **D6.2.1**).
- Share receiver shape/data loads among sibling reads inside a numeric region's
  guard sequence. This sequence neither mutates objects nor collects; each
  property still validates its cache entry and storage kind. Raw data addresses
  do not survive a safepoint (**D5.3**, **D8.4.1v2**).
- Reuse an enclosing pure region's checked method identity for its admitted
  scalar callees. Guard argument lanes, retain original snapshots on fallback,
  and keep proven numeric/Boolean returns native, including early-return joins.
  Preserve the existing precise root and scalar ownership machinery
  (**D5.2–D5.3**, **D6.2.1**).
- Admit allocating numeric methods of at most 48 proof nodes to the existing
  guarded inliner inside ordinary functions. Avoid expansion inside other
  methods, which multiplied method graphs and slowed startup even on unrelated
  workloads. Existing recursion/depth limits remain. The cheaper concrete
  constructor path makes this smaller than the earlier rejected broad expansion.
- Reuse `str_cmp` when both strings have Lambda's ASCII flag, and short-circuit
  identical pointers. Other strings retain UTF-16 comparison, including WTF-8
  surrogate cases; this does not change string-key canonicalization
  (**S1.11**, **S8.2.2v5**).

No helper function, JIT import, value representation or vendor change is added.
The new variant table uses 104 bytes only at eligible recursive construction
sites; ordinary construction sites remain 16 bytes.

## Measurement and validation

Artifacts: `temp/mvp_cd_followup_20261009/`. The incoming release is SHA-256
`2756f62f430544be114769d8ec133af30d58befc5818e3d7279079a8dafe8f40`.
The candidate frozen for acceptance is
`2b6cb21ffe7e91757a7c8fe847db2b0af3856981b11ba50a114c8d9e0c5a1210`.
`accepted/manifest.json` freezes source and binary hashes.

Measurements use unchanged JS bodies and output checks, pinned native MIR,
release binaries, fresh processes, balanced candidate/control/identical-control
order and self-reported execution times. Initial compilation is excluded from
execution timing. Cold process times are tracked separately; Node tiering during
the workload is included. The Node wrapper surrounds the same CD body with a
function and a timer. Both JS engines run 100 aircraft and 200 frames and verify
4,305 collisions. The typed Lambda port uses different native data structures,
so its comparison does not isolate the effect of typing.

The direct null/undefined property-cache expansion was removed: a five-run
screen measured 198.653 ms versus 195.930 ms for CD. Subsequent screens are
diagnostic, not the final acceptance matrix. Expanding allocating methods inside
other methods measured CD 172.106 ms versus 196.410 ms, but cold process time
grew from 3.161 s to 3.502 s and also regressed Bounce/Storage startup. Its
acceptance run was stopped during the 48-workload cohort and retained under
`pre-method-limit/`. Restricting expansion to ordinary function bodies preserved
the execution gain while bringing CD cold process time to approximately 3.20 s
versus 3.15 s in the next screen. The final measured tradeoff is recorded below.

The focused suite passes 65 tests normally and with forced GC/freed poisoning.
Extended coverage includes alternating constructor parameter types, nullable
links, reordered constructor fields, constant fields, object identity, tiny
floating values, negative zero, method replacement, function-valued nested
property paths and nonnumeric argument fallback. MIR inspection verifies that
the admitted construction path contains packed stores without property-helper
calls.

### Execution measurements

The final 60-workload matrix uses 15 measured runs per lane. All output checks
pass. Times below are medians in milliseconds from `accepted/new6/comparison.json`.

| Workload | Incoming MVP | Candidate MVP | Node | Untyped Lambda | Typed Lambda |
|---|---:|---:|---:|---:|---:|
| CD | 195.819 | **171.139** | 35.819 | 610.298 | 182.115 |
| Richards | 74.940 | 75.630 | 8.287 | 304.274 | 78.735 |
| NBody | 45.389 | 45.292 | 5.364 | 7.430 | — |
| Bounce | 0.825 | 0.825 | 0.722 | 0.069 | — |
| Storage | 0.508 | 0.515 | 0.692 | 0.590 | — |
| SHA1 | 13.760 | 13.847 | 8.762 | 50.433 | — |

CD is **12.6% faster**, **4.78× Node**, and **6.0% faster than typed Lambda**
under these workloads and timing boundaries. Its candidate/control ratio has
paired bootstrap 95% interval **0.8706–0.8789**; the identical-control peer's
interval is **0.9982–1.0084**. Lambda clocks its benchmark kernel, whereas MVP
includes class setup and result verification. Cross-language ratios retain
these timing and data-structure differences.

CD cold process time is **3.189 s versus 3.155 s**: a **1.1% increase** in the
point estimate. Bounce and Storage cold times are essentially unchanged. The
earlier broad inlining experiment's roughly 10% startup increase was rejected.

The 30-run Bounce confirmation measures 0.8255 versus 0.8250 ms, with ratio
interval **0.9952–1.0036**, matching the identical-control peer. This establishes
no additional detected slowdown in this round; it does not resolve the preceding
round's 0.92% regression against its older control. The three shared Lambda
controls (`fib`, `gcbench`, `json_gen`) also include parity in their 30-run
confidence intervals.

The initial matrix flags `object_delete` at 1.167 versus 1.121 ms (+4.1%). Its
normalized MIR is identical between binaries; the cause is not established.
`args_fp` also shifts slightly, but its identical-control peer shifts equally.
The 60-run confirmation clears `args_fp` (ratio interval 0.9977–1.0041), while
`object_delete` remains **1.157 versus 1.116 ms (+3.7%)**, with ratio interval
**1.0259–1.0503** and peer interval 0.9902–1.0099. This remains an open regression.
Richards' 30-run confirmation is **75.111 versus 75.1095 ms**, interval
**0.9970–1.0070**, consistent with parity.

Two separately linked isolation builds each received 60 paired measurements:
removing only the ASCII change retains the slower result (1.1595 versus
1.170 ms; interval 0.9797–1.0035), while the incoming compiler with the ASCII
change matches the incoming release (1.1195 versus 1.1175 ms; interval
0.9876–1.0117). Thus the ASCII change is not the measured cause. The two builds'
native `map_shape_set` and `map_shape_delete` bodies are byte-address-identical
in disassembly; `mvp_lmd_property_get` moves within the image. These observations
narrow the investigation but do not establish a layout/cache explanation.
Artifacts and exact link commands are under `deletion-isolation/`; production
sources and the accepted binary were not changed for these experiments.

### Dynamic work

One separate instrumented CD execution produces the same 4,305 collisions and
the following counters, with no registry overflow. Synthetic detail counters
are excluded from helper totals to avoid double counting.

| Counter | Incoming | Candidate | Reduction |
|---|---:|---:|---:|
| Helper calls | 4,823,132 | 4,118,145 | 14.6% |
| Function-frame entries | 2,174,399 | 1,551,802 | 28.6% |
| GC root reloads | 9,850,703 | 9,521,172 | 3.3% |
| Tree-node value initialization fallbacks | 82,438 | 7 | 99.99% |
| Vector plus/minus frame entries, each | 311,304 | 3 | 99.999% |

Recursive traversal itself still enters 435,072 frames and accounts for
5,598,492 root reloads. Removing vector call boundaries does not eliminate that
remaining recursive ownership work. These counts identify remaining work;
they do not assign a percentage of elapsed time to each operation.

### Gates

- Focused suite: **65/65**, both normal and forced GC with freed-memory poisoning.
- Lambda/input baseline: **6,485/6,485**.
- Test262: **40,261/40,261**, all 169 batches exit zero, zero retry time.
- All 60 MVP output oracles and all three shared Lambda controls pass.
- `accepted/closeout-hashes.json` confirms unchanged measured sources and that
  the restored release executable matches the frozen candidate hash.

The frozen performance binaries are not instrumented. Diagnostic helper/frame
counts are collected separately, following **D5.4.4**.

Correctness gates are complete. Performance acceptance remains open for the
confirmed `object_delete` regression and the disclosed CD cold-start increase;
no benchmark padding or unrelated mutation change was applied to hide either.
