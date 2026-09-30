# Result50 Python backpatch for Results3–4

Results3 and 4 retain their March 2026 MIR, Node.js, other engine, and unaffected Python timings. The ten Python execution-time cells below use the three-run median from `benchmark_results_v50.json` (2026-09-30, CPython 3.14.6). Original Python figures were CPython 3.13.3. This is a workload correction across dates, not a contemporaneous rerun or evidence of an engine speedup.

The original R3 report is in Git revision `f148390d6a4528bef1c1a77f860988e8f885e51e`; R4 is in `d033cb4cdeb16b3a472724d261bb7d73e68b34f9`. The report displays rounded timing values, so derived ratios and geomeans here use those displayed cells. The chart uses best-time deduplication of identical benchmark names across suites, consistent with its original R3/R4 Python points.

The same ten Python values were updated in `benchmark_results_v3.json`, the historical default JSON that was reused by later partial runs. Each changed row retains its prior exact value in `_status_detail.python.original_exec_ms` and names Result50 as its source. Its other cells and latest-run metadata are unchanged; the JSON as a whole is not a single March session.

| Suite / benchmark | R3 Python (old) | R4 Python (old) | Result50 Python (ms) | Workload difference |
|---|---:|---:|---:|---|
| awfy/sieve | 1.76s | 1.76s | 0.503 | old Python harness repeated work beyond the Node wrapper |
| awfy/permute | 2.11s | 2.11s | 1.355 | old Python harness repeated work beyond the Node wrapper |
| awfy/queens | 1.14s | 1.14s | 0.738 | old Python harness repeated work beyond the Node wrapper |
| awfy/towers | 1.11s | 1.11s | 1.824 | old Python harness repeated work beyond the Node wrapper |
| awfy/bounce | 1.39s | 1.39s | 0.835 | old Python harness repeated work beyond the Node wrapper |
| awfy/list | 976 | 976 | 0.613 | old Python harness repeated work beyond the Node wrapper |
| awfy/storage | 1.27s | 1.27s | 1.329 | old Python harness repeated work beyond the Node wrapper |
| beng/fasta | 2.0 | 2.0 | 1.848 | old Python ALU string had two extra characters |
| jetstream/navier_stokes | 1.84s | 1.84s | 118.041 | old Python timed 15 frames; Node times one |
| jetstream/hashmap | 184 | 184 | 83.048 | old Python hash table differed from Node implementation |

AWFY/cd also had an incorrect historical Python harness count (1 versus Node’s 100), but both old reports had no valid Python timing for that row. It remains missing in Results3–4. AWFY/mandelbrot was already excluded for an invalid timing; its count matched Node, so it is not part of this backpatch.

Corrected historical Python/Node chart points: Result3 **4.86×**, Result4 **4.86×**; both compare 55 unique names. Result50 **5.02×** covers 63 canonical rows and remains a separate measurement session.
