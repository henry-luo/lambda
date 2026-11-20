# Lib and Input Issue Ledger

Working issue record for `lambda-input` and its shared `lib` code. Input owns
these providers under D9.1.1v4 and D9.1.3v2. This ledger records defects,
test and build issues, and their evidence; it does not make design rulings.

Use stable IDs `LI-001`, `LI-002`, and so on. For each new issue record its
status, affected code or test, reproduction, observed and expected behavior,
and whether it blocks work in Lambda. Cite the applicable `S#` or `D#` ruling
when one covers the issue. Record uncertain causes as unconfirmed, link any
owner-side fix and verification, and retain the ID when the issue is resolved.


Resolved records are in the [fixed issue ledger](<Lib_Input_Issue_Ledger (fixed).md>).

## Open issues

<a id="li-080"></a>

### LI-080 — FIXED

Canonical declarations share stylesheet source paths within their CSS epoch
(**D5.2.3 / D5.2.4**). See the
[CSS memory reproduction](<Lib_Input_Issue_Ledger (fixed).md#li-080>).

<a id="li-078"></a>

### LI-078 — FIXED

The shared color engine now computes absolute Lab and rectangular Lab mixes,
retaining their computed model through cloning/serialization and used sRGB
conversion. Stylesheets can select their real Lab-supported branch.
See the [bounded Lab repair](<Lib_Input_Issue_Ledger (fixed).md#li-078>).

<a id="li-077"></a>

### LI-077 — FIXED

A merge duplicated the bounded value-parser definition and blocked Input builds.
See the [merge repair](<Lib_Input_Issue_Ledger (fixed).md#li-077>).

<a id="li-076"></a>

### LI-076 — FIXED

Constrained error/null outcome metadata now follows its base. See the
[shared-core repair](<Lib_Input_Issue_Ledger (fixed).md#li-076>).

<a id="li-075"></a>

### LI-075 — FIXED

The extracted document resource-base resolver was missing from the public Input
API, blocking Lambda release builds. See the
[resource-base restoration](<Lib_Input_Issue_Ledger (fixed).md#li-075>).

<a id="li-074"></a>

### LI-074 — FIXED

Closing head templates no longer captures following metadata and body.
See the [parser repair](<Lib_Input_Issue_Ledger (fixed).md#li-074>).

<a id="li-073"></a>

### LI-073 — FIXED

Element discovery loses completion after byte-only prefetch.
See the [completion repair](<Lib_Input_Issue_Ledger (fixed).md#li-073>).

<a id="li-072"></a>

### LI-072 — FIXED

Font-family lists rejected pending substitutions mixed with later fallback families.
See the [shared family-list parsing repair](<Lib_Input_Issue_Ledger (fixed).md#li-072>).

<a id="li-071"></a>

### LI-071 — FIXED

`shape-outside` rejected supported gradients before contour construction.
See the [shape-outside gradient admission repair](<Lib_Input_Issue_Ledger (fixed).md#li-071>).

<a id="li-070"></a>

### LI-070 — FIXED

Backing insertion misordered native children before DOM-only comment anchors.
See the [native reference-order repair](<Lib_Input_Issue_Ledger (fixed).md#li-070>).

<a id="li-069"></a>

### LI-069 — FIXED

Derived pseudo-style refresh invalidated unchanged authored host sizing inputs.
See the [local borrowed-cascade repair](<Lib_Input_Issue_Ledger (fixed).md#li-069>).

<a id="li-068"></a>

### LI-068 — FIXED

Automatic document font faces lost the requested variable weight.
See the [automatic-weight and instance-cache repair](<Lib_Input_Issue_Ledger (fixed).md#li-068>).

<a id="li-067"></a>

### LI-067 — FIXED

An omitted flex shorthand basis collapsed auto-height columns.
See the [interoperable percentage default](<Lib_Input_Issue_Ledger (fixed).md#li-067>).

<a id="li-066"></a>

### LI-066 — FIXED

Individual `translate` rejected typed calculations accepted by `translate3d()`.
See the [shared-validation repair](<Lib_Input_Issue_Ledger (fixed).md#li-066>).

<a id="li-065"></a>

### LI-065 — FIXED

See the [CSS Shapes registration repair](<Lib_Input_Issue_Ledger (fixed).md#li-065>).

<a id="li-063"></a>

### LI-063 — FIXED

See the [fixed record](<Lib_Input_Issue_Ledger (fixed).md#li-063>).


<a id="li-062"></a>

### LI-062 — FIXED

See the [fixed record](<Lib_Input_Issue_Ledger (fixed).md#li-062>).


<a id="li-061"></a>

### LI-061 — Textile span attributes are parsed as visible text

**Status:** OPEN, reproduced 2026-11-15 against Lambda `e55450ec8` plus
editor work and input `3e14a05`. **Lambda task blocker:** yes for safely
widening rich Textile admission. Input ownership is D9.1.1v4; document
processing is D9.3.4.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/textile-style-roundtrip.ls`.
For `Text %{color:red}red% end.`, the parser produces unstyled nested spans
containing literal `{color:red}red`. Markdown conversion and formatting back
to Textile yield `%Text {color:red}red end.%`, losing the styled span and
changing the text. Textile's [span documentation](https://textile-lang.com/doc/spans)
uses the same inline CSS attribute syntax. Evidence is in
`Lambda-opus/temp/edit-viewer/textile-style-roundtrip.log`. The responsible
Input-side code and repair are unconfirmed.

After the LI-045 fix, the parser adds nested attribute-free spans for ordinary
Textile emphasis. Treating those wrappers as transparent could admit useful
documents, but would also hide the style loss above from the editor's semantic
round-trip check. Keep the existing source fallback for such documents until
the provider retains the styled span. No Input implementation or reference
was changed by this report.

<a id="li-059"></a>

### LI-059 — RST literal block round trip adds code-block newlines

**Status:** OPEN, reproduced 2026-11-15 against Lambda `e55450ec8` plus
editor work and input `3e14a05`. **Lambda task blocker:** yes for admitting
affected RST literal blocks to rich edit. Input owns the parser and formatter
under D9.1.1v4; D9.3.4 governs document processing.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/rst-direct-roundtrip.ls`.
Parsing `Example::` followed by an indented `code` block yields code text
`code`. Formatting that parsed document as RST and parsing it again yields
code text `code\n\n`, and the original wrapper `div` is also removed. The
Markdown-mediated editor path yields `code\n\n\n`. Evidence is in
`Lambda-opus/temp/edit-viewer/rst-direct-roundtrip.log` and
`Lambda-opus/temp/edit-viewer/rst-current-probe.log`. Extra preformatted
newlines change document content; the editor therefore keeps the existing
source-mode fallback.

Resolved LI-047 repaired the literal introduction and code-block emission,
but this remaining round trip still changes code content. Whether the writer,
reader, or their contract owns the newlines is unconfirmed. No Input source
or reference was changed by this report.

<a id="li-058"></a>

### LI-058 — FIXED

See the [fixed record](<Lib_Input_Issue_Ledger (fixed).md#li-058>).


<a id="li-057"></a>

### LI-057 — FIXED

See the [fixed record](<Lib_Input_Issue_Ledger (fixed).md#li-057>).


<a id="li-056"></a>

### LI-056 — FIXED

See the [fixed record](<Lib_Input_Issue_Ledger (fixed).md#li-056>).


<a id="li-053"></a>

### LI-053 — Shared allocators lack ruled reference-counted lifetime

**Status:** OPEN — capability absent, 2026-11-14. **Lambda blocker:** not
established. D5.2.3 requires an atomic allocator reference count for shared
arenas and pools, with destruction at zero and exclusive reset/rewind.

The current `lib/mempool.h` and `lib/arena.h` expose no allocator retain or
release API, and `pool_destroy` and `arena_destroy` directly reclaim their
storage. `mem_context_destroy` also recursively releases registered child
nodes before destroying an allocator. The formal implementation inventory in
`Lambda_Formal_Design.md` likewise marks D5.2.3 reference counting as not
implemented. A retained allocator must keep its backing nodes, context and
any dependent cleanup state alive until its final release; adding a counter
to the allocator alone would leave those dependencies dangling. Reset and
rewind also need an exclusivity check.

This prevents using a retained source allocator as a general solution to the
source-owned type-contract lifetime in [LI-003](#li-003). It does not establish
that allocator retention is the only possible repair for that issue. No new
allocation mechanism or conservative GC scan is authorized by this record.

<a id="li-027"></a>

### LI-027 — Shared libuv loop spins during concurrent context scheduler teardown

**Status:** OPEN — process-global provider state confirmed; the exact intrusive-list
corruption/race is unconfirmed. **Observed:** 2026-11-12, macOS native debug,
Lambda checkout `85cc95ad4` plus the existing-ruling follow-up. **Lambda task
blocker:** yes for claiming the complete standalone shared-module concurrency
gate; no for the selected ledger repairs, whose Lambda and Test262 baselines pass.
Ownership: **D9.1.1v4 / D6.4.4 / D7.5.1** require a loop owned by each context.

Reproduction from `../Lambda-opus` after linking the current native host:
`./test/test_lambda_concurrency_gtest.exe --gtest_death_test_style=threadsafe`.
The preceding 97 native checks pass, and
`RuntimeGlobalsConcurrency.SharedChartAndPdfModulesStayStableAcrossEvalThreads`
passes its full 20-second concurrent phase. The next
`ModuleStateSlabsArePrivateToEachEvalContext` test spins during worker cleanup.
A one-second `sample` places its surviving worker in
`lambda_scheduler_destroy` (`lambda/runtime/concurrency.cpp:872`) ->
`lambda_uv_run_loop` (`lib/uv_loop.c:143`) -> `uv_run` -> `uv__run_prepare`.
The main thread waits in `pthread_join`; CPU is about one core and peak native
footprint is 3.5 GB. The unbounded run was stopped after capturing evidence.

`lib/uv_loop.c` still owns one process-global `g_loop`, `g_prepare`, `g_check`,
drain callback pair and active flags. Lambda's scheduler creates/closes each
context's wake handle on that shared loop. Four independent evaluator workers
can therefore enter its provider during teardown. A shared prepare/check-list
race is consistent with the sample, but an exact corruption-producing operation
has not been established. The existing Lambda Runtime Globals RG8 record
explicitly requires an instance-based `lambda_uv_*` API; moving only the loop
pointer or adding a Lambda-side serialization workaround does not implement it.

Evidence in `../Lambda-opus/temp/ledger-ruled-fixes/`:
`concurrency-complete.log`, `concurrency-hang.sample.txt`, and
`concurrency-sample.log`; implementation/checkpoint:
`../Lambda-opus/vibe/impl/Lambda_Impl_Ledger_Ruled_Followup (done).md`.
The test setup sources only add explicit acknowledgement of now-inferred chart
and PDF error outcomes; no workload, check, deadline or scheduler path is removed.
The affected additional qualification is stopped. No sibling implementation,
header, test or build source is changed by this report.

**Provider follow-up, 2026-11-14:** input now exposes `LambdaUvLoop` instances
with per-instance loop, prepare/check handles and drain context. Instances are
owned and driven by one thread; destruction returns `UV_EBUSY` while a user
handle or request remains or the loop is running. The legacy global API uses
the same checkpoint implementation and remains available for existing hosts.
Native tests exercise four concurrent owners, checkpoint ordering and
isolation, pending work, unreferenced handles, reentrant cleanup refusal and
legacy behavior (`temp/ledger-uv-instance-focused.log`); twenty repeated runs
pass (`temp/ledger-uv-instance-repeat.log`). This is an input
provider capability, not a repair of the reported Lambda hang: the scheduler,
JS event loop and other runtime consumers still call the legacy global API.
Lambda must migrate its context-owned consumers and qualify its concurrency
gate before LI-027 can close. D7.5.1 governs scheduler ownership.

<a id="li-022"></a>

### LI-022 — Online document captures hit TLS/connect failures despite intermittent curl success

**Status:** host-trust initialization repaired, 2026-11-11; residual public
origin/network failures remain open. **Lambda task blocker:** cleared for
verified Lua, Lua manual and Xiph captures; PIL and other transport failures
remain explicit after repeated live attempts. Acquisition ownership **D9.1.2v2**, project boundaries
**D9.1.1v4 / D9.1.3v2**. The user authorized sibling source updates.

The host trust adapter previously selected/loaded platform roots inside the
mbedTLS handshake callback, after TCP connection. Local first-record captures
show about 91–94ms cold delay after connect versus about 2ms with roots already
loaded. Cold Xiph probes failed 3/3; warmed probes succeeded 2/3, with an EOF
also observed after warming. This supports a host-side initialization defect
but does not explain every public request failure.

`curl_use_host_trust_store` now resolves roots before transport and passes the
captured chain through `CURLOPT_SSL_CTX_DATA`. The callback applies that chain;
CA and hostname verification remain enabled. The deterministic regression
`InputOwnedCurl.CapturesHostRootsBeforeStartingTransport` selects valid roots,
changes the ambient override before perform, and proves selection occurred
at configuration. It fails before the change (CURLcode60) and passes afterward.
All three InputOwnedCurl cases pass, including rejection of an incorrect CA
and hostname. No vendored libcurl/mbedTLS source was changed.

Input HTTP diagnostics now use the backend error buffer and distinguish
`CURLE_SSL_CONNECT_ERROR` from certificate-validation errors; handshake EOF
is no longer incorrectly described as a certificate error. Same-host live
Lua and Xiph renders succeeded with valid PNGs after the repair. PIL still
hit the existing 60s downloader timeout in that check and remains a failure;
no fallback/error page PNG is accepted as a loaded document. URLs, protocol
policy, deadlines and TLS validation are unchanged.

Evidence: owner `temp/li014-before.log`, `temp/li014-after.log`; Lambda
`temp/online-recovery/trust-clienthello.json`, curl cold/warm probes, and
`temp/online-recovery/li014-live/results.json` with per-page logs/PNGs.

**Acquisition follow-up, 2026-11-12:** reran the remaining corpus using the
verified Lambda host. Exact PIL URL `https://www.lua.org/pil/contents.html`
loads in Chromium, and an input-owned curl probe also completed with HTTP200.
Three isolated Lambda captures still failed: two connection resets during
TLS handshake and one 60s timeout. System curl also timed out on that exact
URL in another attempt. The discrepancy remains intermittent; a new local
TLS defect or an origin/provider cause is **unconfirmed**. No error-page PNG
is accepted and no protocol, certificate or redirect limit was weakened.

**PIL follow-up, 2026-11-12:** The unchanged qualified Lambda host and the
official snapshot retry both loaded the page, so a valid 1200 × 800 capture is
now available. Three additional Lambda renders produced two TLS connection
resets and one HTTP 200; four alternating system-curl requests returned HTTP
200, while the standalone input-owned curl probe had one reset and three
HTTP 200 responses. A separate input-owned curl probe reproduced resets with
both default TLS and TLS 1.2, and also observed a connect timeout and an empty
reply. These observations locate failures in transport but do not establish
whether the remote peer, network path, or client TLS behavior causes them.
No reliable source-level repair is established. Evidence is in Lambda
`temp/pil-investigation/`; the original failed capture logs and manifest were
preserved there before the successful retry. Ownership remains **D9.1.2v2**.

Independent system-curl probes reproduce TCP connect timeouts for GNU and Qt,
connection refusal for Eigen, the Onfolk hostname mismatch (a Squarespace
certificate), and the expired ObservIQ certificate. The OS resolver cannot
produce addresses for `www.narrative.bi` or `empower.me`; local DNS queries
return NOERROR with no A answers. These are acquisition blockers in the
current environment, not evidence of a native rendering failure. Ownership
remains **D9.1.2v2**; attribution of the transport/DNS causes is not established.
Evidence: Lambda `temp/online-recovery-20261112/transport/`, `pil-render/`,
`pil-owned.log`, `pil-system.log`; per-case browser diagnostics live beside
`test/online/snapshots/capture_results.csv`. The capture helper now retains
selected origin headers, lifecycle events and failed/pending resources.

2026-11-14 refresh: a bounded system-curl request to the same PIL URL failed
with `SSL_ERROR_SYSCALL` (exit 35), before an HTTP response. This is a system
transport control, not evidence of a new input-provider defect. See
`temp/ledger-network-pil.log`.

LI-016 is [archived in the fixed issue ledger](<Lib_Input_Issue_Ledger (fixed).md#li-016>).

<a id="li-015"></a>

### LI-015 — Cross-process SQLite scope contention lacks an admission/retry contract

**Status:** OPEN — provider capability/diagnostic gap; no lost-update or SQLite
corruption claim. **Observed:** 2026-11-11, macOS native debug, Lambda peer gate
on interpreter/AUTO/JIT. **Lambda blocker:** yes for the additional simultaneous
peer-credential administration acceptance case; no for sequential administration
or independently owned peer repositories. D9.1.1v4 / D11.1.2 / S12.4.4v4 apply.

Two Lambda processes concurrently run `invite` and `revoke` against one encrypted
credential row. One reports E400 `io.sqlite: cannot open transaction`; on JIT one
reports `commit outcome is uncertain; reopen before retrying`. Evidence in the
Lambda checkout: `temp/sync-peer/final-process-{interp,auto,jit}.log`. These
aggregate failure logs preserve the exact commands, errors and failing per-step
filenames; per-step directories are reused by later process qualifications. The retained reproduction
is `python3 test/sync_peer/test_peer.py --contention` (after building Lambda).
The normal peer gate excludes this still-unqualified additional case explicitly.

Source evidence: `lib/rdb_sqlite.c:sqlite_tx_wrap` configures synchronization and
uses deferred `BEGIN`, not `BEGIN IMMEDIATE`; no busy timeout/handler is configured
there. The Lambda wrapper collapses failed acquisition to E400; failed commit is
conservatively uncertain under the documented contract. Deferred contention is an
expected SQLite behavior, but an acquire-before-read writer admission mechanism
and retryable/busy diagnostics are not exposed to this host integration. A common
cause for all reported acquisition/commit failures is not yet established.

The credential patch prevents stale-file overwrites by reading/modifying one
transactional row; it does not establish successful concurrent writer admission.
Do not add SQL lock tricks, retry uncertain commits, weaken the failed gate or
modify sibling source from Lambda. The affected concurrency work is stopped;
owner investigation should establish supported writer admission and outcome
inspection before Lambda expands that qualification. No owner source changed.


<a id="li-011"></a>

### LI-011 — Input dependency build pipeline and cross-platform qualification

**Status:** PARTIAL — macOS implementation verified; Linux and Windows need
verification. **Observed:** 2026-11-10, macOS, Lambda `make build`.
**Lambda blocker:** no on the verified macOS configuration. Authority:
**D9.1.1v4 / D9.1.3v2** (project ownership), **D9.1.5** (build pipeline).

Previously, input invoked CMake for utf8proc and Brotli, Meson/Ninja for
ThorVG, and curl's upstream Autotools `configure`. Meson's backend detection
produced the reported Ninja message. The owner now keeps the pre-split
Lambda recipes in `build_vendor_deps_config.json`, with a shared JSON-driven
Premake/Make builder for vendor and native archives. Curl uses compiler probes
and the upstream source manifest and templates, without running configure.
Pinned versions and the mbedTLS/HTTP2 profile remain in place.

On macOS, `make vendor-deps curl-deps native-deps JOBS=8` passed with CMake,
Meson, Ninja and Autotools programs blocked; `make test-build-dependencies`
passed ten tests. The input-owned curl tests passed, including the correct CA, a wrong CA and
a wrong hostname. The ThorVG manifest selects PNG and JPEG loaders on
macOS, Linux and Windows; actual Linux and Windows builds are not verified.
See `scripts/dependency_builder.py`, `scripts/build_vendor.py`,
`scripts/build_curl.py`, and `scripts/generate_native_deps_premake.py`.

2026-11-14 environment check: this host is Darwin arm64 and has no available
Docker, Podman, Lima, QEMU system emulator, or Wine command. Linux and Windows
native qualification remains pending; this check is not a platform build.

<a id="li-002"></a>

### LI-002 — FIXED

See the [fixed record](<Lib_Input_Issue_Ledger (fixed).md#li-002>).

<a id="li-003"></a>

### LI-003 — FIXED

See the [fixed record](<Lib_Input_Issue_Ledger (fixed).md#li-003>).

<a id="li-064"></a>

### LI-064 — LaTeX command scanning depends on process locale and can split UTF-8

**Status:** PARTIALLY FIXED for locale independence; UTF-8 boundary handling
remains OPEN. Reproduced 2026-11-16 against Input
`2672c1ada7cabbd4144f21f97f15ee96a62b1b6f` and Lambda
`17ce141e10aa10d0b36b1e9e211463df13c3a088` with their recorded working-tree
state. **Lambda task blocker:** yes for exact cross-locale Script/native LaTeX
front-end parity. Input owns the scanner under D9.1.1v4 and D9.1.3v2;
D10.2.2 requires one source to give one AST and output to depend only on the
source and resources. Text sequence operations use code points under S2.5.8.

`lambda/input/input-latex-scanner.c` classifies command-name bytes using
locale-sensitive `isalpha((unsigned char)c)`. A direct probe of the actual
scanner, built from the checksum-verified Input source snapshot, scans the
same valid UTF-8 bytes `5C C3 AA 61 62 63` differently: under locale `C` it
returns end offset 2 and name bytes `C3`; under `fr_FR.UTF-8` it returns end
6 and name bytes `C3 AA 61 62 63`. The `fr_FR.ISO8859-1` locale also returns
end 6. Run `python3 temp/tex-script-f/native-scanner-locale.py` from the
Lambda checkout; the generated probe library and recorded output are in
`temp/tex-script-f/native_latex_scanner.dylib` and
`temp/tex-script-f/native-scanner-locale.out`. This scanner is called by the
native LaTeX parser, so the differing token boundaries can affect its AST.

Separately, the existing included fixture
`test/lambda/latex/fixtures/local_packages/script_definition_unicode_main.tex`
contains `\def\é{Q}`. Native parsing in the default C locale returns a
command target and argument descriptor with bytes `5C C3`, splitting the
UTF-8 encoding `C3 A9` of `é` across values. Run
`./lambda.exe --no-log temp/tex-script-f/native-unicode-bytes.ls`; the result
is recorded in `temp/tex-script-f/native-unicode-bytes-current-jit.out`.
The Script data parser can preserve those bytes, but valid-text decoding
rejects the partial sequence. The scanner now uses the existing ASCII classification helper, so command
boundaries do not depend on the host locale. The new owner GTest failed against
the original scanner and passes against the candidate archive; all 14 linked
TeX engine tests passed in the recorded Input snapshot. A 564-case byte and
buffer-capacity matrix matches the original C-locale scanner under each of
three locales (`temp/tex-script-f/input-scanner-locale-diff.out`); the GTest
logs are `native-scanner-locale-gtest-before-final.log` and
`input-locale-tex-engine-suite-final.log` in the same directory. Installed
source and test match the tested candidates byte for byte. This qualifies the
locale fix, not the remaining partial UTF-8 result: decide and implement its
intended boundary behavior with owner-side tests before closing LI-064.

The subsequent Input baseline was run from a checksum-verified copy of the
current dirty Input source with this fix (`temp/tex-script-f/input-locale-baseline.log`
in the Lambda checkout). It failed because the sandbox denied localhost binds
for two fixture-server runners and nine HTTP cases, and because the already
open [LI-002](#li-002) external-parent admission test failed. Its TeX engine
suite passed 12 tests and skipped two expl3 tests without the optional resource
setting; the separate focused run with that setting passed all 14. This does
not qualify the full Input baseline as passing.
