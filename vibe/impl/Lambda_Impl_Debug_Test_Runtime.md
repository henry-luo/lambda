# Debug test runtime tuning

**Status:** in progress, 2026-11-10. Scope: the ten slow tests named by the user.
Authority: D5.3.1, D6.3.1–D6.3.3, D8.2.4, D8.3.1, D8.5.1 and D11.2.1.

## Current checkpoint

- Goal: each requested test below five seconds with the ordinary debug host;
  preserve tiers, goldens, authority checks and all forced-GC profiles.
- Tree: `/Users/henryluo/Projects/lambda`, `master`; initial HEAD
  `5c0d7c68c5f5dde248d6d951ec85b6d62f928569`, initially clean. All task changes
  are uncommitted. Input HEAD at qualification is `a5a52782312d43ca46a396d5cda4955262502dea`.
- No sibling implementation, header, test, build source, vendor source or
  `log.conf` changes. LI-010 is the permitted input-ledger edit.
- Both security expl3 cases remain blocked by input ownership (D9.1.1v4 /
  D9.1.3v2). Evidence and reproduction are in LI-010; do not bypass the test.
- The user explicitly requests debug acceptance timing. An optional question
  asks whether selected host files may use O1 with symbols/assertions retained;
  no host optimization flags have changed while awaiting an answer.
- Current runtime changes: remove obsolete GC Item reloads; memoize immutable
  borrow queries; remove duplicate module load/link from entry lookup; cap
  oversized debug native generation at O0 (release keeps its existing O1 cap).
- Current harness changes: checked bounded batches for tier goldens, shared
  process budget, independent parallel pure Lambda GC profiles, configurable
  idle watchdog and a shorter test delay that still crosses watchdog intervals.
- A narrower native-generator lock and an aggregate JS optimization budget were
  evaluated and discarded. The temporary compiler-pass observer is removed.
- The full debug Lambda baseline is running in
  `temp/test-runtime-tuning/lambda-baseline.log` (exec session 89212).
- Earlier stable-root code passed 366/366 forced-GC tests; that result predates
  later query/link/budget/harness changes and does not qualify this tree.
- Focused current candidates pass. Tune27 is about 3.6 s; pending fetch about
  0.5 s; parallel music GC profiles 2.25 s. Other requested Lambda cases still
  exceed five seconds. Final repeated timings remain to be collected.
- Next: inspect baseline failures, run focused HTTP and repeated timing checks,
  then record results and any unmet targets. All evidence is under
  `temp/test-runtime-tuning/`.

## Root causes and retained changes

D5.3.1 makes Item/header addresses stable. The stack-rooting design §4.5
explicitly permits retaining their register copies across collection. The
shared MIR emitter nevertheless reloaded every live root after each MAY_GC
call. This expanded Hotwire's outer function from 46,138 useful MIR
instructions to 776,829 instructions. Remove those loads and the now-unused
call-live-out matrix. Exact lifetime stores, root publication, shared-binding
coherence and data-zone refreshes retain their existing owners (D6.3.1–D6.3.3).

The module link pass already publishes callable entries. Its old entry helper
loaded and linked the same module a second time, resetting every thunk before
redirecting it again. Entry lookup now reads the published address. The
unused load/link helper is removed. Native code-address diagnostics remain.

Plain var-borrow queries depend on the frozen AST and parameter declarations,
but repeatedly scanned that tree. Cache their two boolean forms per private
MVP binding (D8.2.4). Queries that depend on an evolving preserved contract
remain uncached.

Large debug native images use the existing instruction budgets with O0 native
generation. This retains native execution and all host assertions, logging and
debug symbols. Optimized hosts keep the previous O1 limit. The host itself
remains compiled with its ordinary debug flags.

Tune27 previously launched a fresh CLI for every fixture/tier pair. It now uses
bounded `test-batch` processes and the existing complete ordered-protocol
decoder. Each script still gets a fresh runtime/heap and its platform golden
(D8.5.1). Pure tier groups share that helper and start native work first. No
procedural tier runs overlap because those fixtures can write identical paths.

The isolated pure Lambda GC cases ran their independent profiles serially.
Run them through the same bounded process budget while preserving a distinct
process, compiler and heap for every reference, forced and randomized profile.
Procedural cases, JavaScript cases and existing multi-script GC batches retain
their scheduling. Process counts and byte-for-byte output checks remain.

The pending-fetch fixture slept 11 seconds to cross the five-second watchdog
twice. `LAMBDA_JS_DRAIN_IDLE_MS` makes the idle interval configurable while the
default remains 5,000 ms and finite-request grace remains 30,000 ms. The test
uses a 100 ms interval and a 300 ms response delay, preserving the same
watchdog-crossing assertion.

## Evidence and remaining costs

Initial isolated debug timings differed from the user's loaded-suite report:

| Requested case | Initial isolated seconds |
| --- | ---: |
| Security expl3 /0 | 13.883 |
| Procart noise parity | 7.740 |
| Tune27 parity | 10.690 |
| Pending fetch | 11.155 |
| Security expl3 /1 | 8.482 |
| Chart paint parity | 9.487 |
| Hotwire MIR HTTP | 7.866 |
| Music forced GC | 7.127 |
| Plant parity | 7.289 |
| LaTeX corpus | 8.945 |

The chart imports 99 modules. A pass profile measured about 0.36 s planning,
1.48 s lowering and 4.78 s native linking before the duplicate-link fix.
The LaTeX corpus's first algebra record takes about five seconds; a separate
probe measured only four milliseconds parsing and about 4.93 s rendering.
It is therefore not recorded as an input-parser blocker. Native compilation
and package rendering still require further work if the final measurements
remain above the requested bound.

`stable_roots_across_calls.{ls,txt,mir-check}` retains allocating calls, mixed
joins and a checked-multiply overflow branch in both the golden and forced-GC
corpora. No formal ruling changes are made by this implementation.
