# Online loading follow-up, 2026-10-23

Status: partial; overview/pricing watchdogs remain. Atlas currently passes
with both candidate and qualified control on the current server response.

Authority: **D8.1.3v35** (approved module loop handoff), **D8.2.4v2**
(indexed dependency facts), **D8.4.1v2** (guarded operations without inline
caches), **D5.3.3** (precise roots), and **D1.3v3** (profile-owned semantics).
The source baseline is merge `556a1dd8d`.

The retained changes address three measured runtime costs and a root lifetime
failure:

- Checked synchronous top-level ES-module functions can use the existing
  10,000-backedge one-way handoff. A shadowing for-let head retains its own
  scope; module dependencies still resolve through the original live slab.
  Primitive live-in copies have entry kind guards; mismatches remain in AST.
- Separately parsed continuations declare only referenced module bindings.
  The previous synthetic type-evidence call was outside the cloned function
  and never reached its analysis; guarded copies now occur inside the body.
- Foreign Set reuses its canonical key instead of invoking public
  `Reflect.set` and interning it again. In AST satellites, a guarded
  integer-indexed Set retains Number keys until the shared typed-array setter;
  its coercion, detachment,
  BigInt policy and buffer ownership remain authoritative. Ordinary objects,
  proxies and altered receivers retain ordinary property dispatch.
  The guard requires the satellite's full property ABI: closed fixed-property
  units can pair named physical writes with fixed reads and cannot mix those
  with a backing-buffer write.
- Root registration searches from the newest owner, preserving duplicate
  ownership counts and order. Fresh class instances use the existing fresh
  prototype initializer before exposure. Checked root-range registration
  propagates allocation failure; RootVector rolls back partial registration
  when its heap changes, and external spans cannot report false success.

The frozen release is
`temp/radiant-online-oct23-continued/package-guarded-final/lambda`, SHA-256
`8382b442b3e7a2a30143a8abb4823cc2d20c26c9e828599b69f1129be39a5dfb`.
The paired original CSS workload preserves checksum
`1891615 302068 1876230`: control 15,877/15,930 ms, candidate
14,450/14,429 ms before the final guard restriction; the final release repeats
the checksum at 14,425 ms. The isolated indexing pass falls from approximately
3.4 s to 0.44 s before ordinary fifth-call entry promotion. Full native JS
checks (237), optimization contracts (121), GC checks (92, excluding the known
abort-test hang), and forced-GC/poisoned module-loop checks (4) pass. Broader
qualification is recorded in
`temp/radiant-online-oct23-continued/qualification-guarded.json`; the earlier
candidate and its four optimization failures are preserved in
`qualification.json` and its logs.

The corrected release passes all 40,261 Test262 baseline cases, with 2,652
recorded skips and zero regressions. `make test-radiant-baseline TEST_JOBS=3`
passes its recorded baseline: 8,980 passed, 143 partial and two skipped.
`make test-lambda-baseline TEST_JOBS=3` reports 8,147 passed and one
pre-existing failure,
`PdfFileOutput.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers` (image
bytes differ from its golden in both tiers). Three previously reproduced
macOS abort-test hangs are excluded explicitly in the qualification record;
the PDF comparison and optimization expectations remain unchanged.

Overview, Atlas and pricing still reach the unchanged 120-second script
watchdog. All exit normally after the script diagnostic with zero tracked
live allocations. Atlas also requires Worker. Exact corrected-release replays
confirm all three watchdogs (128.6/140.3/138.0 s total wall time respectively,
including teardown); no page repair is claimed from the workload improvement
alone. None has a native crash, outer timeout or tracked live allocation.

The user approved checked contained synchronous declarations and switch
statements at module hot entry on 2026-10-24 (**D8.1.3v36**). The experiment
checks all contained bodies, uses existing compiled closure ownership and
selects transitive live module dependencies. Six focused closure/exclusion and
loop-boundary tests pass normally and with forced GC/freed-memory poisoning.
The implementation is **not retained** because five interleaved original CSS
runs show no repeatable speedup: median execution 14,525 versus control
14,445 ms; median wall 14.998 versus 14.777 s; identical checksum throughout.
The candidate source and reusable delta are preserved under
`temp/radiant-online-oct24-nested/candidate-source/` and
`nested-candidate.patch`. The previously qualified release remains active. After removal, all 237
native JS checks and four module-loop checks with forced GC/poisoning pass.

Generic diagnostics show that tokenizer `lt()` reaches the ordinary fifth call
but remains excluded for three untagged constant template literals. A scratch
copy replacing only those with equivalent quoted strings makes it promote;
this is an isolation probe, not a shipped source rewrite or original acceptance
result. Constant-template admission was requested separately and approved as
**D8.1.3v37** on 2026-10-24. Qualification of that scope is recorded below.

Exact unmodified page replays confirm overview/pricing watchdogs at the
unchanged 120-second boundary (135.045/137.515 s total wall). Atlas passes with
both candidate (48.040 s) and qualified control (45.127 s) on the current
server response, so no runtime repair is attributed to its pass. Every replay
has zero tracked live allocations, no native crash and no outer timeout.
Artifacts and the release comparisons are in
`temp/radiant-online-oct24-nested/`. Worker isolates remain separate work under
**D6.3.2**. Browser-matching site errors and origin/DNS/TLS failures remain
separately classified.

The v37 experiment restores the approved contained declarations/switch and
admits only an untagged template with no expressions and one valid cooked tail
quasi. It uses original source and existing lowering. New checks cover cooked
escapes, empty strings, callable source/identity, escaped closure type changes,
interpolation/tagging refusal and unchanged classic/loop boundaries. The finite
verification plan and artifacts are in `temp/radiant-online-oct24-templates/`.
Release measurements and original page acceptance remain pending.

The first v37 candidate keeps the original checksum but is slower on one-pass
CSS (15,000/15,066 versus control 14,252/14,185 ms). Existing compiler timing
attributes 1.147 s to native code generation versus control 0.164 s. O0 and a
generic direct-inlining budget experiment were also slower and were removed.
Overview/pricing still hit the unchanged script watchdog; Atlas completes in
40.618 s but needs paired attribution. No repair is claimed from that pass.

Temporary generic AST work sampling identifies `next` (byte 94035) and
`allocateCursor` (byte 86264) as substantial remaining AST work. They fit the
already approved **D8.1.3v34** one-wrapper/module-only dependency boundary.
That method proof and span adapter are restored for a combined experiment.
Original method identity, strict receiver, home, live module cells and
non-constructibility remain authoritative. Method bodies retain their original
checks: the v36 nested/switch and v37 template additions apply only to ordinary
top-level module functions. No method loop handoff is introduced. The temporary
AST sampler is removed from the candidate source before this build.

The combined method/constant-template candidate also remains unretained.
All 11 focused checks pass normally and with forced GC/poisoning, but original
CSS execution is 15,115/14,979 ms versus control 14,338/14,386 ms (identical
checksum). Overview/pricing still hit the 120-second watchdog at total wall
127.559/137.716 s. Atlas passes at 41.416 s; the control also passes. All
replays exit normally with zero tracked live allocations and no outer timeout.
The retained source delta exactly matches the previously qualified v35 state;
the combined source and reusable patch are saved in
`temp/radiant-online-oct24-templates/candidate-methods-source/` and
`approved-combined-candidate.patch`. No native-code policy experiment remains.

Of 954 sampled AST-evaluation intervals, `setSource`, its local arrow,
`forEach`, and `insert` account for 35/49/75/47 respectively. Those counts
measure AST work, not CPU time. The next proposal is checked identifier
parameter defaults and contained synchronous arrows for the already approved
module-method sites, with every resolved binding inside the compiled method
subtree or live module slab. No AST environment bridge is proposed. Its scope,
exclusions and acceptance checks are in
`temp/radiant-online-oct24-templates/method-default-arrow-proposal.txt`.
At that stage this was a proposed boundary pending the user decision required
by `doc/Doc_Convention.md` §2. The subsequent approval and implementation are
recorded below.

After rollback, all 237 native JS checks and four forced-GC/poisoned
module-loop checks pass. The active release SHA-256 remains
`8382b442b3e7a2a30143a8abb4823cc2d20c26c9e828599b69f1129be39a5dfb`.


### Checked method defaults and arrows (2026-10-24)

The user approved **D8.1.3v38**: checked identifier defaults and contained
synchronous arrows in the existing module-method slice, applying the contained
declaration/switch/constant-template proof. Every initializer/body and resolved
binding is checked against the selected method subtree or live module slab;
no AST environment bridge is introduced. The saved v34/v36/v37 candidate is
restored as its prerequisite. Artifacts for this finite milestone live in
`temp/radiant-online-oct24-default-arrows/`.

Acceptance covers ordered/default/explicit parameter evaluation, parameter TDZ
and separate body scope, escaping lexical-this/mutable closures across precise
GC, old AST and new compiled closure ownership, original metadata/home and
abrupt callbacks. Hidden imports/captures and excluded callable/pattern shapes
must refuse promotion; ordinary/classic admission and method loop handoff stay
unchanged. After focused checks, compare the unmodified CSS workload against
the qualified release, then replay original overview/Atlas/pricing pages with
the unchanged watchdog. Retain only a useful measured improvement and qualify
it with the existing native/GC/optimization/Test262 and baseline gates.


The combined v34/v36/v37/v38 candidate passes all 13 focused checks normally
and with forced GC/freed-memory poisoning. A new semantic check exposed a
shared MIR defect: after default initialization, a same-named body `var` reused
the parameter cell captured by a default closure. Non-eval bodies now copy that
parameter value into a fresh body cell, preserving the escaped parameter cell.
The interpreter and promoted methods agree on separate scope, TDZ, default
order/throws, old/new closure ownership and metadata after this repair.

The combined experiment remains unretained: large CSS execution is
15,042/14,871 ms versus control 15,031/14,420 ms; six-pass small CSS is
9,112/9,010 ms versus 8,711/8,681 ms, with exact original checksums. Overview
and pricing still hit 120 s (127.292/137.189 s total wall); Atlas passes
42.454 s, also known to pass control. All three exit normally, with zero tracked
live allocations and no outer timeout. No page repair is attributed to Atlas.

The next bounded comparison keeps only the approved v34/v38 method subtree
proof and adapter; ordinary v36/v37 promotion stays deferred as in the qualified
control. Their source snapshots and experimental tests are preserved rather
than changing their expectations. All ten method/loop/object-boundary checks
pass normally and under forced GC/poisoning. The unchanged large CSS checksum
is preserved, and paired execution is 14,281/14,171 ms versus
14,580/14,635 ms control. Repeated-workload acceptance and aggregate
qualification are in progress. No thresholds, page source or watchdog changed.


Aggregate runtime qualification passes: 242 native JS, 92 GC, ten forced-GC
method/loop boundaries, 121 optimization contracts and all 40,261 Test262
baseline cases (2,652 recorded skips, zero regressions). The first Radiant
baseline was interrupted after video and scene3d workers stalled with explicit
macOS HIServices/XPC connection failures inside the sandbox. Its logs/results
and task-owned process audit are preserved; no engine change or timeout increase
was used. The required baselines are rerunning with macOS service access.


The macOS-service baseline rerun completes with the recorded Radiant result:
8,980 passed, 143 partial and two skipped. Previously blocked video and scene3d
checks pass 14/14 and 28/28. Lambda passes 8,152 overall and retains the same
`PdfFileOutput.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers` image-byte
golden mismatch in both tiers; no golden is refreshed. The qualified method-only
release and matching modules are restored and verified byte-for-byte at SHA-256
`fb6b46192427e7f09c6f71ad53a96f54b64f1bb18407dbcd42ac184adf9277f0`.
Final original overview/Atlas/pricing replays use that frozen package and the
unchanged 120-second watchdog, with no competing build/test workload.


Final v38 method-only page acceptance remains partial: overview and pricing
hit the unchanged 120-second script watchdog (128.713/136.294 s total wall).
Atlas passes in 42.504 s; it also passes the qualified control, so this is not
attributed to the method change. All three exit normally with zero tracked live
allocations, complete network/layout/render/cleanup markers and every phase
metric required by the online harness. The measured roughly 3% original large
CSS improvement and semantic parameter-cell repair are retained; no page fix
is claimed for the two remaining watchdogs.

The next measured source shape is the top-level module arrow `Wu` at UTF-8
byte 103929 (80/954 sampled AST-evaluation intervals, not CPU time). Its simple
parameters and expression body have no lexical-this/arguments/new.target/super
observations or external captures. **D8.1.3v38** leaves ordinary-function
admission unchanged, so the concrete module-arrow proposal was submitted for
the user decision required by `doc/Doc_Convention.md` §2. Its scope/checks are in
`temp/radiant-online-oct24-default-arrows/top-level-arrow-proposal.txt`.
No implementation is made while that decision is pending. Qualification,
performance, page outcomes and active release identity are collected in
`temp/radiant-online-oct24-default-arrows/status-final.json`.


### Checked top-level module arrows (2026-10-24)

The user approved **D8.1.3v39**: synchronous top-level ES-module arrows with
simple identifier parameters, no activation observations and the existing
body/dependency proof over live module slots. The approved v37 constant-template
proof is restored independently of ordinary nested-declaration/switch promotion,
which stays deferred. Original callable metadata and non-constructibility,
classic admission, whole-unit admission, all loop handoff and thresholds stay
unchanged. No AST environment bridge is introduced.

The finite milestone targets the measured `Wu` hotspot without modifying the
captured original module or the online pages. Focused checks cover original
arrow source/identity/name/length, receiver-independent calls and construction
refusal, polymorphic arguments/coercions/property callbacks/throws, live module
reads/writes/callees, excluded activation/capture/pattern/callable shapes and
forced GC/poisoning. Next are three paired original CSS release runs and exact
original-page watchdog replays. Retain only useful release improvement without
regression, qualified by the required native/GC/optimization/Test262 and
Radiant/Lambda gates. Artifacts: `temp/radiant-online-oct24-module-arrows/`.


The v39 experiment passes 12 focused checks normally and with forced GC/freed
memory poisoning. Three original large-CSS pairs preserve checksum
`1891615 302068 1876230`, but median execution is14,158 versus13,727ms
control (wall14.614 versus14.075s). The six-pass workload is also slower,
8,701/8,582 versus8,249/8,315ms with its original checksum. The broader
constant-template restoration adds cold work without a useful overall gain.

The prior target classification was wrong: `Wu` has a module-level hoisted
binding, but its arrow value is assigned inside a module initialization arrow.
The canonical AST shows precisely one wrapper; generic admission tracing reports
byte 103929, parentFunctionId 590, capture_count0, observations0 and the existing
closure/enclosing-function rejection. Thus **D8.1.3v39** correctly excludes it.
This is a structural policy boundary, not an AST environment transport failure.
The trace implementation uses existing compiler-timing controls and records
original source/ownership facts for any definition; it contains no site-specific
condition. The experimental source/patch and diagnostic evidence are preserved
in `temp/radiant-online-oct24-module-arrows/`. Original page replays are running
before restoring the qualified v38 source and release.


Original v39 candidate replays confirm overview still hits the 120-second watchdog (127.866s
total wall) and Atlas passes in 42.454 s, also known to pass control. Pricing
instead receives a completed HTTP403 response and renders the server body in
0.632s under the already approved navigation policy. That does not exercise
the original pricing script workload or establish a watchdog repair. All three
exit normally with zero tracked live allocations and no outer timeout. A
qualified-control pricing check distinguishes the server response from a
runtime change before restoring the v38 source/release.


The qualified control receives normal pricing HTML and again hits the 120-second watchdog
(136.574 s total wall), confirming the candidate's HTTP403 completion was not
a script-loading repair. The v39 runtime/test experiment and all diagnostics are
removed from the active source; the qualified v38 release/modules are restored
at SHA-256 `fb6b46192427e7f09c6f71ad53a96f54b64f1bb18407dbcd42ac184adf9277f0`.
The first restoration check exposed stale make objects because source copying
preserved older mtimes: that binary passed 244 candidate native tests, not the
restored 242. Fresh source mtimes force recompilation of both JS runtime files
and the test; all 242 restored native checks and 10 forced-GC checks then pass.
Source bytes exactly match the prior qualified method-only snapshot. The prior
native/GC/optimization/Test262/Radiant qualifications and known Lambda PDF
failure remain applicable; no slower candidate is retained.

The latest overview download cache contains the captured original module body
byte-for-byte, independently confirming the corrected `Wu` source classification
against current page code. The checked one-wrapper standalone-arrow proposal is
concrete in `temp/radiant-online-oct24-module-arrows/contained-module-arrow-proposal.txt`.
It requires a new user decision under **D8.1.3v39** and `doc/Doc_Convention.md` §2;
no such admission is implemented. Full artifact summary:
`temp/radiant-online-oct24-module-arrows/status-final.json`.


### Checked one-wrapper module arrows (2026-10-24)

The user approved **D8.1.3v40**. The finite milestone admits the original `Wu`
arrow inside its single synchronous module initialization wrapper, using the
existing checked body and live module slab. Share the method wrapper structural
check; retain zero external captures and activation observations, simple
parameters, ordinary unresolved globals, original identity/metadata and no
pre-existing AST environment bridge. Other callable/body restrictions,
thresholds and loop handoff remain unchanged.

Acceptance: focused auto/AST and forced-GC/poison checks for sibling identity,
older callable lifetime, imported-name parameter shadowing, live module
reads/writes/callees and excluded capture/wrapper/activation sites. Confirm
original `Wu` admission and compilation with generic source tracing before
three paired original release CSS runs and the original page replays. Keep
only useful improvement after native/GC/optimization/Test262 and Radiant/Lambda
gates; no source rewrites, timeout increases or harness masking. Artifacts:
`temp/radiant-online-oct24-contained-arrows/`.


The first v40 candidate admits and compiles the exact original `Wu` source
span (103929–104012), with parentFunctionId 590, zero captures and zero
activation observations. All 14 focused checks pass normally and with forced
GC/freed-memory poisoning. The class-site exclusion fixture deliberately keeps
its enclosing method interpreted; otherwise the already approved v38 method
subtree produces an ordinary compiled child and does not exercise this boundary.

Three original release CSS pairs keep checksum `1891615 302068 1876230`,
but median execution is 14,105 versus 13,801 ms control (wall14.471 versus
14.159s). Overview still hits the unchanged 120s watchdog (129.199s total);
Atlas passes in44.888s, also known to pass control. Pricing replay is pending.
No root-source or fixture rewrites were used. The next bounded comparison
isolates method/arrow constant-template admission from the separately deferred
ordinary-function template restoration. Original source profiling is retained
for diagnosis of any remaining runtime cost. No slower candidate is accepted.

Pricing also exercises the original script workload and hits 120 s (138.822s
total). All three replays exit normally, with zero tracked live allocations;
there are no outer timeouts or native crashes. This candidate does not repair
the two remaining script watchdogs. Exact replay telemetry and one-second stack
samples at 10/50/100 s are preserved beside the frozen candidate release.


The narrower v40 slice retains constant templates for methods/arrows only;
ordinary-function template/nested-declaration/switch promotion remains deferred.
All 246 native checks and 14 forced-GC checks pass. Three original large-CSS
pairs have median 13,772 versus 13,941 ms control (wall14.106 versus14.208s).
Two pairs of the repeated smaller workload give median 8,122.5 versus 8,237 ms
(wall8.2665 versus8.3905s). Both original checksums are unchanged. This is a
small consistent improvement, not a repair of the watchdogs. The frozen release
SHA-256 is `2e1c7cde80cae33b42c7c02f5469662e5265ed7248309af55d8c29106a775fd0`.
Original overview still hits 120 s (127.823s total); Atlas passes in 41.123 s, as it
does with the qualified control. Pricing replay and aggregate gates are pending.

Generic stack profiling uses a separately linked map whose native text section
matches the measured first candidate byte-for-byte. Main-thread sample counts
show distributed interpreter/property/side-root/GC cost, with no new isolated
crash or corrupt lifetime. The retained candidate adds no profiling counters.
The returned function expression at byte 44722 reads wrapper-local t from Qr;
it is outside the approved capture boundary. A concrete read-only capture-bridge
proposal is preserved separately; no such bridge or admission is implemented.


Final v40 qualification passes 246 native JS, 92 GC, 14 forced-GC/poison
boundary checks, 121 optimization contracts and all 40,261 Test262 baseline
cases (2,652 known skips, zero regressions). Radiant passes with 8,980 passed,
143 partial and two skipped. Lambda has 8,156 passing checks and the identical
pre-existing `PdfFileOutput.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`
image golden mismatch: object 9 differs at byte 760 in both tiers, matching the
qualified control exactly. No golden or harness is changed. The three previously
recorded abort-test hangs remain excluded under the same qualification policy.

Retained original-page replays: overview 127.823 s and pricing 136.671 s still hit
the unchanged 120 s watchdog; Atlas passes in 41.123 s, also known to pass control.
All exit normally with zero tracked live allocations and no outer timeout or
native crash. Thus the approved one-wrapper arrow implementation is qualified
and its small original-CSS improvement is retained; online loading remains
incomplete for those two pages. The first broader ordinary-template candidate
is not retained.

The root release and four modules/manifest match the frozen qualified package,
and the three implementation/test source files match the native qualification
snapshot. Full evidence: `temp/radiant-online-oct24-contained-arrows/status-final.json`.
The read-only wrapper-body capture proposal is concrete in
`temp/radiant-online-oct24-contained-arrows/captured-function-proposal.txt`.
It changes **D8.1.3v40**'s explicit capture exclusion and needs the user decision
required by `doc/Doc_Convention.md` §2. No such bridge or admission is implemented.


## Approved live capture reads — D8.1.3v41 (2026-10-24)

The user approved the bounded proposal in
`temp/radiant-online-oct24-contained-arrows/captured-function-proposal.txt`.
The selected original CSS predicate at byte 44722 reads wrapper-local `t`.
The existing callable retains a precisely traced AST environment after
promotion; shared code must use the current callable on every read.

Finite acceptance: distinct sibling Sets, live replacement and member calls,
TDZ/const behavior, scalar ownership under forced GC/freed poisoning, callback
order/thrown identity, original metadata/construction and exclusion checks.
Then prove compilation of the unchanged original source, run three paired
release CSS measurements and the three original page replays, retaining only
a useful measured gain. Qualify native JS, GC, optimization contracts,
Test262, Radiant and Lambda at the milestone boundary. Watchdog/harness
thresholds stay unchanged. Artifacts live under
`temp/radiant-online-oct24-capture-reads/`; qualification is pending.


Boundary review found that object methods use the same FUNC_EXPR node kind
as ordinary function expressions. The shared one-wrapper proof now checks
its property site and excludes method/getter/setter wrappers, preserving the
approved D8.1.3v41 method-site boundary. An explicit returned-function case
pins the exclusion. Initial measurements are retained separately; the final
candidate is rebuilt and its focused, performance and qualification checks
are repeated after this correction.


The corrected final candidate passes 249 native JS and 17 forced-GC/poisoning
checks. Exact original spans 44722–44867 and 103929–104012 both compile.
Three final paired large-CSS runs preserve checksum 1891615/302068/1876230:
median 13,604 versus 13,805 ms control (wall 13.833 versus 14.088 s).
The printing phase improves consistently; total execution improves about 1.5%.
These replace the initial candidate's 13,555/13,914 ms qualification figures.
Final page replays and aggregate gates remain pending.


### Live capture read milestone qualified

D8.1.3v41's bounded read bridge is retained after complete qualification:
249 native JS, 92 GC, 17 forced-GC/freed-poisoning checks, 121 optimization
contracts and all 40,261 Test262 baseline cases (2,652 known skips, zero
regressions). Radiant passes with 8,980 passing, 143 partial and two skipped
checks. Lambda has 8,159 passes and only the known
PdfFileOutput.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers failure;
both image-object-9/byte-760 assertion strings exactly match the qualified
control. No golden was refreshed.

Final release replays: overview 127.303 s and pricing 135.451 s retain the
120 s script watchdog; Atlas passes in 41.357 s, also with control. All three
exit normally with zero tracked live bytes/count. The readonly bridge's
original-CSS improvement is retained, but the online-loading objective is
incomplete. The retained source is unchanged since native checks. The restored
root release and four modules/manifest match the frozen package (host SHA256
e65662d29bfa94a16fa94c810e25c384bf6555a3a812c0e8a8ccb96c2974e92e).
Full evidence: `temp/radiant-online-oct24-capture-reads/status-final.json`.

The isolated AST diagnostic leaves production source/build artifacts unchanged.
It records the method-contained callback at 91846–91994 as substantial
remaining work, with only immediate-method-body captures r/i/a/o/s/c and
lexical this. Live writes and that receiver/method-site bridge exceed the
approved D8.1.3v41 boundary. The proposal, subsequently approved as v42 below, is:
`temp/radiant-online-oct24-capture-reads/method-callback-capture-proposal.txt`.
The broader online sweep remains deferred while overview/pricing still fail.


## Approved checked method callback bridge — D8.1.3v42

The user approved `method-callback-capture-proposal.txt` from the qualified v41
artifact directory. The exact original arrow at 91846–91994 reads r/i and
reads/writes a/o/s/c from the immediate setSource body, plus lexical this.
The parent must pass the entire existing method proof. No captured parameter,
other outer binding, threshold change or loop handoff is admitted.

Finite acceptance: original exact-source compilation; sibling cells/receivers,
compound/update ordering and reentry, TDZ/const, wide scalars under forced GC
and poisoning, original metadata/non-constructibility and mixed AST/MIR-parent
callback lifetimes; all unsupported sites stay interpreted. Compare three
unchanged original release CSS pairs and the three original pages before
retaining a useful gain, then qualify native/GC/optimization/Test262/Radiant/
Lambda. Production watchdogs and harness semantics stay unchanged.

The candidate is **not retained** after this finite experiment. The original
callback compiles, and 252 native JS plus 20 forced-GC/freed-poisoning checks
pass. The exclusions include arrows in method parameter defaults; approved
arrows must be inside the method body. Live writes, reentrant evaluation order,
TDZ reads/writes, scalar ownership, lexical receivers and escaped callbacks
from both AST and later MIR parents pass the focused checks.

Three release CSS pairs preserve checksum 1891615/302068/1876230 but give median
13,605 versus 13,542 ms control, wall 13.835 versus 13.766 s: no useful gain.
Overview 128.776 s and pricing 135.621 s still hit the 120 s script watchdog;
Atlas passes in 41.803 s, also with control. Each exits normally, with zero
tracked live bytes/count and no outer timeout. The page-loading objective
remains incomplete.

Per the proposal's acceptance criterion, the four engine/test source files and
release/modules are restored to qualified D8.1.3v41. Host SHA256 remains
e65662d29bfa94a16fa94c810e25c384bf6555a3a812c0e8a8ccb96c2974e92e.
The D8.1.3v42 permission remains approved, with its implementation deferred.
No broad qualification ran for the rejected candidate; the byte-identical v41
source/package retains its prior qualification and known PDF-only failure.
After restoration, the rebuilt test executable passes all 249 native JS and
17 focused forced-GC/freed-poisoning checks. Formal-index and diff checks pass.
The candidate patch, source snapshot, binary, tests and replay/benchmark logs
remain under `temp/radiant-online-oct24-method-callbacks/`; `status-final.json`
records the decision. The broader online sweep remains deferred while the
selected watchdogs fail.
