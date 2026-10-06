# Lambda Slide Presentation — Implementation

> **Status:** in progress, 2026-10-07.
> **Authorized scope:** implement [the package design](../Lambda_Pkg_Slide_Presentation.md);
> no PPTX import/export.
> **Contracts:** D7.2.4/D7.5.3 package and host boundaries; S12.1.1v2/S12.1.3
> pure transformations and procedural event handlers; S2.6.3 element splicing;
> D4.5.1v4/D5.3.3 ownership and precise roots; D7.2.2 boxed import entries; D2.1.3 tagged string leaves.

## Delivered package

The explicit entry module is `lmd/package/slide.ls`, resolved as `lambda.slide`.
The supporting modules under `lmd/package/slide/` contain the implementation in
Lambda: checked scene normalization, themes, HTML/CSS/SVG lowering, cue
compilation, easing, typed keyframes, deterministic sampling, state reduction,
transitions, color interpolation, paths, Morph, paragraph builds, controls and
live frame orchestration. There is no dedicated native slide object or parser.

[The public reference](../../doc/Lambda_Slide.md) describes the implemented API.
[The introductory deck](../../examples/slide_presentation.ls) covers entry/click
builds and slide transitions. [The package-content deck](../../examples/slide_package_content.ls)
embeds a chart and uses the paragraph helper.

| Milestone | Status |
|---|---|
| Checked scene model, themes, static snapshots and public module | Implemented |
| Pure cue compiler, keyframes, deterministic sampler and reducer | Implemented |
| Generic Lambda frame delivery and realm-neutral style writes | Implemented; lifetime checks continuing |
| Live player, controls, viewport fitting, transitions and error recovery | Implemented |
| Wipe, authored paths, paragraph builds and explicit-ID Morph | Implemented within documented limits |
| Release profiling, retention and final baseline verification | In progress |

Compilation prepares per-target track lists and initial visuals once. Presets
expand to typed tracks; the sampler shares interpolation with keyframes and
transitions. Parallel conflicting writers fail with source-path diagnostics;
ordered sequential writes retain continuity. Color tracks carry typed RGBA
values through intermediate samples and serialize only at the output boundary.

Live templates cache the compiled plan and initial rendered tree in source
attributes. They retain ordinary DOM nodes during a build and patch only changed
visual properties. Scene replacement occurs at slide/transition boundaries.
Each player owns its playback state, frame token, generation and last successful
ordinary visual sample. Frames compare against that retained GC value instead of
resampling the previous time. Scene replacement, Morph and failed commits invalidate
the comparison; the cache retains one sample rather than a frame history.
Each player retains its own sample independently. Waiting/paused
players stop requesting frames; stale deliveries are ignored. Failed commands or
refused requests pause and report an error, with Restart available for recovery.

Standalone pages fit logical coordinates into the current viewport and refit on
resize. Embedded players require an explicit unique instance ID. Hidden objects
and transitioning layers use pointer exclusion, `aria-hidden` and `inert`.
Keyboard routing preserves embedded controls and modified shortcuts.

## Shared host changes

- The existing DOM operation catalog adds `request_frame`, `cancel_frame` and
  `viewport_size`. The first two are procedural operations, with zero denoting a
  refused request. Viewport reads return null without a suitable top-level host.
- A document-owned frame queue stores generation-checked native node references,
  copied event names and reusable request slots. It retains no script `Item` or
  closure. Delivery is FIFO, requests made during a callback belong to the next
  frame, and cancellation can suppress a later callback in the current batch.
  Document teardown releases the queue; detached owners are pruned.
- Lambda-only documents bind the shared host context for load, resize, input and
  frame events. Virtual headless time and window-loop time use the same delivery
  path. The current JavaScript adapter remains separate from package policy.
- Realm-neutral `style_set_property` uses precise roots and the existing CSS
  mutation machinery rather than synthesizing a JavaScript style wrapper.
- Transformed SVG viewport clips retain the active CSS transform. CSS clip
  serialization preserves its inverse transform across display-list storage,
  replay offsets and raster clipping; focused pixel fixtures cover these paths.
- Imported MIR calls now pass `ITEM_MISSING_ARGUMENT` for omitted Lambda boxed
  arguments. Explicit null remains null, and cross-language calls retain their
  host ABI. This repairs the skipped-default bug exposed by `slide.compile(deck)`
  in eager JIT mode (D7.2.2); the package does not supply a workaround.

Behavior-handler model values now receive a precise root before the event record
is allocated (D5.3.3). A forced-GC double-click regression reproduced a collected
DOM wrapper with the previous binary; rooting the model fixes word selection and
subsequent caret collapse without a JavaScript runtime. Both canonical DOM and
projected selection assertions pass. The handler retains the rooted value through
its call and render-map dirty notification.

An opt-in `RADIANT_FRAME_PROFILE=1` trace measures each delivered turn and its
nested reconcile: script plus synchronous host writes, cascade, layout, repaint
request, event/mutation/commit counts, queue slots and live DOM/view pool bytes.
Paint execution is outside these turn timings. Script sampling and synchronous
DOM writes are currently combined; they must not be presented as separate
measurements. The trace is disabled by default.

A line shape now samples its authored stroke as its paint baseline, and static
SVG snapshots use the sampled stroke just as live patches do. The paint
continuity regression includes line highlighting. Eager JIT snapshots exposed
an unrelated markup guard that assumed eight-byte `String` alignment. It now
checks the actual `String` ABI alignment, with a focused four-byte-aligned string
escaping regression; valid static MIR strings are no longer discarded.

## Verification so far

These are observations of the named verification rounds; retention acceptance
is still open.

| Check | Recorded result |
|---|---|
| Ten slide Lambda regression scripts | All expected outputs match in eager JIT and interpreter modes with the character-backing release, including 14 helper assertions and 9 paint-continuity assertions |
| Focused live/pixel fixtures | All eight pass with the handler-allocation release binary: presentation, two players, motion/highlight/Morph, reduced motion, autostart/resize, FIFO cancellation, transformed SVG and CSS clipping, and failure/restart. Features and frame delivery also pass under forced GC. The mouse-root fixture passes 2 assertions. Handler allocation passes 10/10 with explicit forced GC and poisoning, including a retained detached wrapper and an independent DOM attribute edit. All ten focused fixtures pass under explicit forced GC and poisoning with the character-backing release (66 assertions). |
| Native regressions | AVL 42/42, retained-view/ownership 165/165, GC 83/83, MarkEditor 55/55, markup 44/44, HTML parser 55/55; latest focus-boundary retained-view/ownership suite 166/166 and JS script suite 192/192; clip serialization/offset regression passed |
| Float/int-cast Radiant lint | Latest round passed |
| Latest Lambda baseline | Character-backing round: Input 2104/2104; Lambda 4167/4170 (2026-10-07). Slide regressions pass, including the full 1154-case Lambda executable and 296 forced-GC cases. The same three REPL prompt expectations fail. |
| Radiant baseline | Sequential character-backing round: all layout/render gates passed; 4010 pass, 350 partial, 14 fail (2026-10-07). UI automation passes 335/346, including the extended allocation fixture at 10/10 under forced GC and poisoning. The same 11 unrelated UI failures remain. The updated large-batch fixture preserves its eight focus/state assertions and passes on the incremental path. DOM integration passes 126/127; the existing context-menu mismatch remains. View command tests pass 65/67; the HTTP-image placeholder mismatch remains, and the LaTeX iframe navigation failure seen in earlier rounds recurred (it passed the preceding backing-refresh round). |

Earlier Lambda failures comprised three REPL prompt mismatches and LaTeX/math
cases. A focused rerun passed the seven slide cases then present and a LaTeX
case; the math HTML golden remained different. The workspace contains unrelated
LaTeX/TikZ work, which this implementation leaves intact. No golden or runner
was changed to suppress those failures.

Earlier Radiant failures were document-editor UI fixtures, Tetris smoke, Todo
file-switch/textarea, a context-menu snapshot, and an HTTP image fixture whose
server failed to start under the environment's socket restrictions. A focused context-menu replay shows stale state-dump paths: expected
`html.body...` versus actual `#document.html.1.body...`. The editor filter replay
updates the native input value but its reactive filter model does not reflect
the edit, so its clear button is never rendered. That handler reads `evt.char`;
the exact dispatch failure remains open. These findings identify failed
contracts without changing goldens or adding an input compatibility workaround.
The later round adds two LaTeX view failures caused by a parse error in the
concurrently edited `latex/packages/page_style.ls`, a LaTeX iframe navigation
failure and a Bootstrap page-load timeout. That timeout reproduces in the debug
page-load runner; a focused release view completes successfully. The relationship
of the remaining failures to the host changes still needs review.

`make build-test` also encountered an unrelated display-list test link failure:
`render_glyph_path.o` now references `layout_utf8_next_codepoint`, which that test
link does not supply. The changed glyph-path source is outside this package's
edits. Building the retained-view target separately succeeds; the latest 149 tests pass.

## Retained presentation values

`dom.presentation_style_set_property(node, property, value)` writes an owned CSS
value at animation origin, without replacing an authored Mark attribute.
`dom.presentation_style_clear(node)` removes the whole transient layer. Both are
shared, realm-neutral host operations (D7.5.3). Unknown properties, invalid
values, `!important` writes and values unsupported by the existing ownership
copier are rejected before changing the previous sample. Same-value writes are
no-ops. Author `!important` rules retain precedence. Recascade preserves this
layer and authored inline declarations; node removal/document teardown use the
existing owned declaration release path (D4.5.1v4).

Live sampled effects, layer transitions, Morph placement/background and SVG
fill/stroke now use this channel. Viewport sizing remains an authored style
update on load/resize. Host rejection propagates to the player's pause/error
recovery path. SVG paint reads the presentation winner alongside the existing
CSS cascade. Pure timing and interpolation remain Lambda code.

Two focused native tests cover unchanged authored attributes, cascade/recascade,
clear and rejected updates, plus 256 transform/opacity samples with stable Input
and node arenas and bounded pool storage. Live fixtures cover HTML opacity,
SVG paint, author `!important`, recascade and clear. `style_get_property` is now
published to Lambda and precisely roots its arguments. Its inline read excludes
presentation values; an extended regression exposes and checks stylesheet
`!important` precedence without losing the authored CSSOM value.

The channel exposed an incorrect presence scan: `style_tree_foreach` counts
callback results but does not stop on false. A later stylesheet declaration
could erase an earlier inline/presentation match. The scan now accumulates its
result, preventing canonical stylesheet binding from discarding authored Morph
insets. The mixed inline/stylesheet recascade regression passes; both normal
and reduced-motion Morph fixtures pass.

A separate release retention probe found that first buffers of temporary runtime
arrays used the document arena whenever UI mode was active. Allocation now
selects that arena only when it owns the collection header; explicit Input
construction retains its existing allocation path (D4.1.1v2, D4.3.1). Native
regressions verify a GC array survives collection with its heap-owned buffer and
an arena Element retains an arena buffer. All 83 GC tests pass.

Event source-position/selection snapshots now use ordinary GC maps and arrays,
with exact roots during construction (D4.1.1v2, D5.3.3). Existing source-schema
builders initialize private immutable seeds once per document. The delivery
fixture retains a selection snapshot across later frames and checks its schema
and values. All nine assertions pass normally and with `LAMBDA_GC_FORCE_EVERY=1`.

The reconcile journal keeps inline small-turn storage and grows reusable,
document-owned storage for larger turns. Growth preserves order and reconcile
pins; reset releases pins without discarding capacity; teardown frees the
buffer. Allocation failure retains the broad fallback. Mutation-root scratch
uses the actual record count. The retirement regression checks 256 ordered
records and storage reuse. The bounded repaint-region collector can still
request a full repaint safely; it does not truncate reconciliation.

The 70-row DOM mutation fixture now expects incremental reconciliation rather
than a record-overflow fallback. Its focus, retained state and identity assertions
remain intact; all eight pass. This expectation changes because the journal now
retains the whole batch, rather than to suppress a regression.

Repeated inline attribute add/remove operations exposed unpublished shape
drafts retained in Input's arena and transition steps retained in its pool.
MarkEditor now owns a lazy draft arena and releases it at destruction. Persistent
types and pooled keys retain their established owners (D3.4.3v3, D4.1.4v5).
A 256-cycle regression checks exact warm-state arena/pool bounds and surviving
attributes; all 55 MarkEditor tests pass. Reconcile selector matchers now use
caller-owned storage through the shared initializer; release profiling had
attributed 104 bytes per turn to their former retained allocation.

## Release probe and remaining work

Release executables are linked to separate paths under `temp/slide/` because
workspace debug/release builds share the root executable. A probe uses 100
simultaneous text/image/SVG targets, a one-second fly-in sampled 60 times, then an
idle second. The macOS/arm64 host's CPU model is unavailable under the sandbox.
The latest completed channel/array-ownership measurements are:

| Viewport | Turn p50 / p95 | Script + host p50 | Cascade p50 | Layout p50 |
|---|---|---|---|---|
| 1280 × 720 | 18.733 / 33.806 ms | 9.951 ms | 1.185 ms | 7.502 ms |
| 1920 × 1080 | 24.615 / 32.627 ms | 12.785 ms | 1.337 ms | 9.761 ms |

Percentiles use 60 sorted samples (p95 index floor(59 × 0.95)). These measure
Lambda plus synchronous writes, cascade and layout; paint is excluded. They are
not displayed frame rates and do not meet a 60 Hz budget of 16.67 ms including
paint. Both deliver 60 events/commits, reuse two frame slots, perform one full
reflow per turn and deliver zero events in the idle second. These measurements
precede the growing journal, which removes the 64-record overflow fallback.

| Storage | 1280 first → last sample | 1920 first → last sample |
|---|---|---|
| DOM pool live bytes | 1065754 → 1141998 | 1067034 → 1143278 |
| View pool live bytes | 388648 → 388022 | 388440 → 387814 |
| Input arena used bytes | 432000 → 469792 | 432000 → 469792 |
| Node arena used bytes | 496 → 496 | 496 → 496 |

Retention is still open. Before array ownership was fixed, Input grew by
roughly 57 KB per frame; transient event metadata then accounted for 640 bytes
per frame. Both ordinary-frame growth sources are fixed without omitting fields
or rewinding published data (D4.1.4v5, D5.3.3). The event-snapshot release probe
at 1280 × 720 records Input 431232 → 431264 bytes (the final parked diagnostic
write), node arena 496 → 496, and two reused frame slots. Turn p50/p95 is
18.711/20.330 ms, with paint excluded.

A 600-frame, ten-restart probe passes all ten assertions and delivers no idle
callbacks. Before the draft fix it records Input 431232 → 705440, DOM pool
1065760 → 2008495 and view pool 388648 → 480038. Those numbers do not establish
long-session bounds. The growing-journal probe removes full-layout fallback:
layout generation stays unchanged, with one incremental reconcile per frame.
Its 1280 × 720 turn p50/p95 is 18.775/21.042 ms; script/host, cascade and layout
p50 values are 9.451, 1.947 and 7.213 ms. Instrumentation attributed 1316
bytes per turn to reconciliation: 104 during cascade and 1212 afterward.
Caller-owned matchers remove the former. Positioned layout accounted for the
latter: its static-position direction query allocated initial-value strings and
cast them to `CssValue`. It now uses the existing typed keyword/inherited
resolver. Ordinary-frame commit growth is zero. The per-node audit logging was
removed after attribution.

DOM attribute writes now reuse equal immutable values in a document-owned index
whose strings stay in the same Input arena (D4.1.1v2, D4.1.4v5). This does not
free published strings or put arbitrary values into NamePool. The index is
released at document teardown and is keyed by the owning Input. A regression
checks 256 alternating visibility/inert writes, exact warm Input bounds and an
older returned string that remains unchanged. Storage is proportional to distinct
values, not a guarantee for applications continually writing unique strings.
All 150 retained-view tests pass.

The post-fix 1280 × 720 probe records DOM 1060698 → 1059298, view
388536 → 379558, Input 417024 → 417056 and node arena 496 → 496 bytes.
Turn p50/p95 is 19.506/26.323 ms; paint remains excluded. A 600-frame,
ten-restart run passes all ten assertions, records zero ordinary-frame commit
growth and two reused slots, but records DOM 1060704 → 1180283, view
388536 → 392032 and Input 417024 → 460832 bytes. Restart/command retention
remains open. The previous-sample cache experiment passed live and forced-GC
checks but did not demonstrate a release speedup, so the package keeps its
original sampling path. Native style-channel bounds alone cannot establish
whole-player retention.

With attribute reuse, the same 600-frame/ten-restart run passes ten assertions
and records Input 415392 → 415456 bytes, node arena 496 → 496 and zero ordinary
frame commit growth. DOM/view growth remains. A 100-restart run delivers 1400
callbacks and records DOM 1060704 → 1910183, view 388536 → 433972 and the same
Input bound. These measurements do not establish whole-player bounds.

An experiment keeping every scene mounted passes the existing feature fixture
and a 20-round two-slide navigation probe, but still grows the document pool by
about 5 MB. It also exceeds the proposal's active/partner subtree policy, so it
was removed. Style-cache counters remain at five canonical entries, zero cold
bytes and 11803 live bytes during that experiment; the growth is elsewhere.
The restored two-subtree implementation passes 40 navigation assertions, but its
20-round probe records Input 1858144 → 51155168 and DOM 2995118 → 32473247
bytes. Fragment parsing/source lifetime and boundary reconciliation are the next
ownership targets. Profiling now also reports journal capacity, canonical style
counts/live/cold bytes and evictions. None of these navigation numbers satisfy
the retention criterion.

Fragment parsing bypassed the private token arena already used by full-document
HTML parsing. It now uses the same begin/recycle/end helpers (D4.1.4v5), while
published Mark values keep their original owner. The new 1000-element fragment
regression checks a bounded token footprint, published attributes and continuation
after token scratch release. The 20-round navigation probe still passes 40
assertions and reduces its final Input footprint from 51155168 to 37505264 bytes.
Published source and document-pool growth remain open; this is not whole-session
acceptance.

The latest sequential attribute-reuse probes record turn p50/p95 of
19.176/21.777 ms at 1280 × 720 and 21.420/24.630 ms at 1920 × 1080. Both deliver
60 events/commits, keep journal capacity at 1024 and canonical style entries at
five, and deliver no idle callbacks. Input increases by only the final 32-byte
diagnostic write, node arena is unchanged, and ordinary-frame commit growth is
zero. An earlier 1920 run in this round was much slower (53.202/119.737 ms);
host-load variance is uncontrolled, so these are observations rather than a
stable hardware guarantee. Paint is excluded in every case; 60 Hz acceptance
remains unmet.

Earlier investigation fixed overwritten transform chains, unlinked cascade
records and AVL wrapper removal. Default motion values now use a per-document
immutable cache. Inline CSS parsing uses scratch plus the existing owned value
copier; rejected cascade copies and optional keyword spellings are released
correctly. Those checks and the layout sizing gate passed before the channel.
The simultaneous Lambda/Radiant channel baseline build shared the root binary;
its widespread Radiant failures are not accepted. The sequential rerun restored
every layout/render reference gate, with the 14 failures recorded above.
No tests or goldens were altered to suppress failures.

A Lambda-only reactive scene-template experiment passes the feature checks and
40 navigation assertions, but its 20-round probe still records Input
981728 → 19781232, DOM 3448417 → 36341140 and view 1089178 → 7122996 bytes.
It reduces published-source growth while increasing view retention. The experiment
was removed; only the active/transition-partner fragment implementation remains.
All ten package scripts match their expected outputs in both JIT and interpreter
modes with the clean handler-root release binary, and all eight existing fixtures
plus the two-assertion forced-GC mouse fixture pass. These functional results do
not close the retention or frame-time criteria.

A temporary release allocation-stack audit of one navigation round passes both
assertions. Between its first and last frame the live document pool increases
2995118 → 4106227 bytes. Major newly retained groups include cascade declaration
records, owned inline declarations and values, wrapper initialization, per-element
transition snapshots and style resolution. The trace begins after the first scene
replacement and ends before the final Previous command, so it cannot prove which
objects are leaks or establish a session bound. Stack tracing changes execution
cost drastically; its timings are excluded from performance evidence. All temporary
allocator/event tracing has been removed. Per-element extension teardown is the
next ownership check; canonical style-cache growth is not the observed driver.

The per-element transition snapshot was allocated in the document pool but was
not released when its extension retired. The new lifetime regression fails with
the old library and passes after the missing release. Pinned detached nodes keep
their snapshot; retirement releases it. A 1,024-retirement warm run preserves the
same live-pool footprint. All 152 retained-view cases pass. This changes ownership
at retirement only (D4.5.1v4); snapshots still survive relayout. A new release
20-round navigation run passes 40 assertions and records Input
1514032 → 36235088, DOM 2995118 → 25465587 and view 728664 → 385294 bytes.
Whole-session retention remains unbounded.

Handler execution previously enabled UI allocation for its entire duration, so
temporary elements constructed only to format HTML became fat nodes in the retained
Input arena. Allocation is now scoped: handlers use runtime GC ownership, and
template retransform enables the retained output arena only while producing its
render result (D4.1.1v2, D4.5.1v4). Nested scopes restore the prior context and
thread-local input owner. A four-callback probe constructs and formats 1,000 nested
elements per callback without inserting them. Previously Input grew by 1,056,000
bytes per callback; the changed release keeps it at 6,816 bytes across all four.
This is an Input-arena bound for that probe, not a bound on the whole document.
The new `lambda_handler_allocation` UI fixture passes its eight publication
assertions with forced GC and freed-memory poisoning. Its ninth assertion, a
retained detached wrapper’s old attribute after subsequent replacements, fails:
expected `value 1`, observed `value 3`. Earlier direct-run reports of nine forced-GC
passes were incorrect: the CLI ignores fixture `env`, while the baseline launcher
applies it. Explicit process environment variables reproduce the failure in both
the SVG-lazy and parser-work releases. A native DOM backing root fix is under
validation. Existing feature and frame-delivery fixtures pass with explicit forced GC.

The retained-wrapper failure comes from a plain GC element created by a handler
and then borrowed by a native DOM wrapper. After the model replaces the element,
the registry’s untraced backing pointer allowed GC to reclaim and reuse its
address. The registry now owns a stable precise Item root for managed backing
sources only (D4.1.1v2, D5.3.3). Attached and wrapper-pinned detached nodes retain
that root; retirement releases it. Runtime teardown releases all remaining roots
before destroying the heap. Parser/Input values remain outside GC (D4.1.3).
The root-only candidate passes all nine retained-wrapper assertions under explicit
forced GC and poisoning, plus a tenth assertion in an extra HTML serialization
probe. Three native tests pass: pinned survival/unpinned collection, 1,024
recycle/collect cycles with zero retained roots after each cycle, and idempotent
teardown. The full native ownership executable passes 155/155 with that candidate.

A separate native test then exposes a second ownership issue: the rooted source’s
content buffer moves during GC, while the copied DOM Element header still holds
its old pointer. The before-fix test fails with distinct source/borrower addresses.
The refresh fix follows D4.3.1’s single-owner compaction contract:
`dom_element_to_element()` reloads borrowed data/content pointers from the rooted
source on access. Per-buffer pointer identity detects when a DOM edit has installed
independent storage and ends that borrow. A node flag keeps ordinary Input-backed
access out of the registry path. No external native header participates in tracing
or receives collector fixups. Six native GC-backing cases now pass, including
content/attribute compaction and independent DOM buffers; the full ownership
executable passes 158/158. The permanent UI fixture passes 10/10 under explicit
forced GC and poisoning, including its independent-attribute mutation. All ten
focused fixtures pass under the same explicit stress environment. Ten package
scripts match expected outputs in both JIT and interpreter tiers; lint and diff
checks pass. Artifacts are `temp/slide/dom_backing_refresh_*`. Sequential full
baselines for this fix finished with the results in the table above: no new
allocation/UI failure remains. The historical parser-work round below records
the retained-wrapper failure before this fix.

The adoption audit reproduced three further failures: cross-runtime adoption
dropped the source heap's root, same-runtime adoption retained a stale borrowed
buffer after compaction, and document resource callbacks ran before backing roots
were withdrawn. Transfer now refreshes the native header before adopting it and
inherits the physical GC owner when registering the destination record. Returning
to an earlier owner also preserves that heap. Destruction withdraws roots before
resource callbacks, which can destroy adopted source documents and their heaps
(D4.5.1v4, D5.3.3). All three new cases pass; the ownership suite reached 161/161.

Three character-data cases then reproduced absent roots for detached GC-backed
text and comments. The shared registry now accepts a backing Item: a Text node
retains its String, and a Comment retains its source Element. Buffer refresh
applies only to copied Element headers; String object addresses and inline bytes
remain stable under D4.3.1/D4.3.2v2. Text mutation updates the retained value so
the old GC String becomes collectible. A fourth case verifies String adoption
between separate heaps and release at retirement. All 165 ownership cases pass.
The release candidate `temp/slide/lambda_release_character_backing` passes all
ten focused fixtures (66 assertions) with explicit forced collection and freed
memory poisoning, and all ten package scripts match expected outputs in both
JIT and interpreter modes. Radiant dimension lint and diff checks pass. Full
sequential baselines finished with the results in the table above: the same
three REPL and eleven UI failures remain, along with the context-menu and
HTTP-image mismatches; the previously observed LaTeX iframe navigation failure
recurred. All layout/render gates passed. No test expectation was weakened.
Artifacts are `temp/slide/*baseline_character_backing*` and
`temp/slide/character_backing_*`.

Published fragment storage remains an open ownership boundary (D4.1.3).
A private parser Input cannot be released merely when its DOM nodes retire:
MarkEditor inserts its values into other Mark trees, and scripts may retain raw
Mark values independently of DOM wrappers. Such a cohort would violate static
Input lifetime (D4.1.2). `fn_mutable_value` is also insufficient for an owned
transfer: it clones containers while borrowing types and scalar Strings. An
owned-GC transfer would need stable owned type/name metadata, owned text and
attributes, precise destination roots, and an audit of GC edges inserted into
non-managed parent trees (D4.1.1v2). Neither that transfer nor private-cohort
release is implemented. Parser work cleanup and native backing roots alone do
not establish a bound on repeated navigation.

The earlier two-round navigation audit passed four assertions but emitted two
`unknown map item type raw_pointer` diagnostics under forced GC and poisoning,
and ten focus-within diagnostics with or without stress. Passing UI assertions
were insufficient to close these diagnostic failures. The root-cause fixes and
latest replay are recorded below; the earlier storage observations remain in
`temp/slide/character_backing_navigation_*`.

A new 20-round navigation probe passes 40 assertions and records Input
942736 → 10873664, DOM 2995118 → 14765548 and view 728664 → 385294 bytes;
node storage stays at 496 bytes. Remaining fragment backing and document storage
still grow, so long-session acceptance is open.

HTML parser lifetime now separates work from published Mark data (D4.1.4v5).
Stacks, tokenizer buffers and error records use a parser-owned arena; stack
growth and adoption-agency insertion use that same arena. Parser destruction
releases the arena, builder headers and untransferred buffer storage, leaving
published strings and elements with their Input owner (D4.1.1v2, D4.1.3).
Full parses, DOM fragments and runtime fragment parsing use a shared scope guard.
The markup parser retains its fragment parser for continuation and destroys it on
reset/teardown. A foster-text merge also releases its temporary builder after
transferring or copying the published string.

The new native ownership test fails before the fix: 64 parser lifetimes grow
Input 704 → 794304 bytes and pool live storage 825 → 30009 bytes without publishing
any new nodes. After the fix, both return to their starting footprint. Continuation
and a 9,000-character attribute/RCDATA regression verify that published values
survive parser destruction, including a failed RCDATA end tag. All 55 HTML parser
cases pass. The changed release passes all focused fixtures, plus the feature and
frame-delivery fixtures under forced GC; ten package scripts match their expected
outputs in both tiers. Sequential full baselines have finished: the Lambda gate
retains its same three REPL prompt failures; Radiant includes the retained-wrapper
regression described above in addition to its previous failures.

A 20-round release navigation probe still passes 40 assertions and records Input
937088 → 10482544, DOM 2994662 → 14715868 and view 728664 → 385294 bytes; node
storage remains 496 bytes. Private parser work is bounded, but published fragment
backing remains retained. These results do not establish a whole-session bound.
Artifacts are `temp/slide/parser_work_*` and `profile_parser_work_navigation*`.

Release phase measurements now include paint, on Apple M4 / 16 GB / Mac16,10.
The 100-target mixed text/image/SVG fixture runs 60 explicit advance steps with
integer 16/17 ms intervals totaling 1,000 ms. Each step forces one paint. Existing
`--event-log` render statistics supply CPU paint record/replay/total times; the
frame profiler supplies cascade/layout and the combined Lambda/host turn. A
temporary copy of the Lambda package times old/new sampling and patch preparation
with synchronous host calls separately. It does not alter the shipped package.
The click and its initial input-drain callback are excluded; the 60 subsequent
callbacks are paired with the 60 explicit paints. Both viewport runs pass their
final-state assertion. Stdout and stderr are captured separately to preserve the
Lambda JSON timing records.

| Phase, p50 / p95 milliseconds | 1280 × 720 | 1920 × 1080 |
|---|---:|---:|
| Lambda old/new sampling | 5.774 / 14.241 | 5.916 / 7.686 |
| Lambda patch preparation + synchronous host writes | 4.174 / 7.134 | 4.268 / 4.934 |
| Cascade | 1.989 / 3.558 | 2.051 / 2.270 |
| Layout | 8.089 / 14.958 | 7.853 / 8.509 |
| Paint record | 23.577 / 49.711 | 23.436 / 24.964 |
| Paint replay | 2.806 / 11.558 | 3.112 / 3.432 |
| Paint total | 27.754 / 62.546 | 27.123 / 29.483 |
| Paired turn + paint total | 48.534 / 100.281 | 48.102 / 51.416 |

The operation counters identify 33 inline SVG paints taking about 23 ms of a
24 ms recording pass in the 1920 run. Each SVG indexed the whole host DOM and
parsed every node's inline CSS during style initialization. `SvgStyleEntry` now
parses its inline declarations once, when the shared entry lookup first queries
that node. The complete node index remains available for ancestors, custom
properties and references outside the current SVG; isolated image documents use
the same lookup. No retained cross-frame CSS cache is added (D4.5.1v4).

Sequential release comparisons with the handler-allocation build record:

| p50 / p95 milliseconds | 1280 before → after | 1920 before → after |
|---|---:|---:|
| Paint record | 23.963 / 25.373 → 5.739 / 6.205 | 24.290 / 25.913 → 5.924 / 6.390 |
| Inline SVG operations | 23.250 / 24.625 → 5.111 / 5.500 | 23.587 / 25.136 → 5.275 / 5.692 |
| Paint total | 28.042 / 29.966 → 9.352 / 10.341 | 28.685 / 31.027 → 10.462 / 11.193 |
| Paired turn + paint | 48.363 / 51.242 → 29.981 / 33.256 | 49.523 / 52.791 → 31.368 / 33.451 |

All four runs deliver the same 60 paired callback/paint samples and pass their
final-state assertion. Timing instrumentation and limitations match the method
above; the 16.67 ms target remains unmet. Artifacts are
`temp/slide/svg_lazy_{before,after}_{1280,1920}`. All focused fixtures pass with
the changed release. The native SVG paint gate passes 12 rows / 680 assertions
across layer-off/eager modes at 1×/2×. All 120 selected SVG UI fixtures pass,
including cascade mutations, inherited variables and use references. The render
reference gate also preserves all 211 recorded tests; it compares existing
reference captures rather than launching a fresh browser.
The browser
export gate cannot launch either default Chromium or bundled headless Chromium
in this sandbox: macOS denies its bootstrap port (1100). This prevents claiming
a fresh independent browser export comparison; the gate and goldens are unchanged.

The first phase table uses the transition-retirement release, before the handler
allocation scope change; the comparison table uses the two later builds stated
above. Host load is uncontrolled. Patch timing includes Lambda
formatting/query work and is not an exclusive native-commit measurement. Paint
total includes record/replay and surface work, but excludes window presentation
and event-loop overhead; the paired sum is not complete GUI latency. Lambda timing
output adds overhead to the combined turn. Individual phase quantiles must not be
summed to estimate a total quantile. Both runs exceed the chosen 16.67 ms target;
the result does not establish 60 Hz playback. Paint recording is the largest
measured phase. Artifacts are `temp/slide/profile_phases_separate_{1280,1920}`
logs, event JSONL, result JSON and aligned summaries.

The focus-event GC diagnostic was an unrooted `relatedTarget` wrapper while
native FocusEvent construction allocated its init record. The trusted-event
factory also left init/type/event values unrooted across allocating calls, and
the FocusEvent constructor did not retain its inputs and result while adding
`relatedTarget`. Each scope now uses precise `RootFrame`/`Rooted` ownership
(D5.3.3). A native test forces collection at every allocation, drops the caller's
related-target root, then verifies identity and a marker before and after another
collection. It fails before the fix and passes after; all 192 JS script cases pass.

Focus pruning uses the owned document root, while validation formerly walked
above it to the physical synthetic `#document` ancestor. Focus validation now
shares the existing owned-root selection with hover and active validation
(D4.5.1v4). Its native test fails before the fix and passes after, and also checks
that corrupt ancestry inside the owned root is still rejected. All 166 retained
view/ownership cases pass. The release candidate
`temp/slide/lambda_release_focus_boundary` replays the two-round navigation audit
under explicit forced GC and freed-memory poisoning: four assertions pass with
zero raw-pointer or focus ancestry diagnostics. This closes those two diagnostic
failures, not the independent navigation-storage acceptance.

The live player now retains the last successful ordinary sample in Lambda view
state and samples only the new frame. It discards that comparison on layer
replacement, Morph and failed commits. The procedure result is captured through
local assignments inside the statement handler (S7.6.7v4); assigning the handler
expression itself silently produced null and lost both cached success values and
host errors. A host-rejected raw CSS color now pauses and reports the rejection;
that regression fails all three assertions with the old expression handling and
passes after the correction. The existing multi-player fixture also checks
Previous, Restart and immediate build completion after Morph. Eleven focused
fixtures pass 74 assertions with explicit forced GC and poisoning. Ten package
scripts match their expected outputs in both execution tiers. The float/int-cast
lint and diff checks pass. Artifacts are `temp/slide/frame_cache_gc_*`,
`focus_boundary_*`, and `host_rejection_before*`.

A paired release comparison uses the same focus-boundary executable and separate
module copies before/after sample caching. Both viewports deliver 60 measured
callback/paint pairs and pass the final-state assertion. The initial click/drain
callback and first two paints are excluded as in the earlier method; idle time
emits no frame callbacks. The package is uninstrumented in this comparison, so
sampling and synchronous host work remain a combined measurement.

| p50 / p95 milliseconds | 1280 before → after | 1920 before → after |
|---|---:|---:|
| Script + synchronous host | 8.710 / 17.736 → 6.768 / 8.270 | 8.659 / 10.097 → 6.805 / 8.257 |
| Cascade | 1.847 / 3.556 → 1.890 / 2.109 | 1.847 / 1.947 → 1.934 / 2.215 |
| Layout | 6.924 / 7.528 → 7.163 / 8.865 | 6.937 / 7.294 → 7.229 / 7.879 |
| Paint total | 8.740 / 9.507 → 9.025 / 10.215 | 9.186 / 9.484 → 9.429 / 10.154 |
| Paired turn + paint | 26.340 / 38.594 → 25.262 / 28.501 | 26.780 / 28.266 → 25.258 / 27.510 |

Across the 60 sampled callbacks, changed Input grows by 32 bytes at the completion
boundary, node storage stays at 496 bytes, and DOM live storage grows about 42 KB
in both versions. Initial forced visual publication adds about 83 KB to the
changed version's warm footprint. These observations do not prove a long-session
bound; navigation's published-fragment growth remains open. Two request slots
are reused. Host load is uncontrolled, and paired turn plus paint still excludes
presentation/event-pump latency; 16.67 ms remains unmet. An initial cache trial
with incorrect statement-handler capture was rejected because its comparison was
always null and it repeated writes; only the corrected runs above describe the
shipped package. Artifacts are `temp/slide/frame_cache_{before,after}_{1280,1920}`
and `frame_cache_summary.json`.

Outstanding acceptance work:

1. Audit remaining reconcile/view pool growth and repeat long-session/navigation
   probes after the journal, matcher and draft-lifetime fixes.
2. Reduce measured frame cost through generic mechanisms where justified;
   repeat both release viewports after ownership changes, including measured paint.
3. Complete the latest native/UI checks, lint and sequential full baselines;
   investigate every regression attributable to these changes.
4. Finish the implementation/validation report against the proposal.

The package excludes arbitrary masks, SVG path deformation, text reshaping,
automatic paragraph measurement, PPTX and new PDF pagination. Morph requires
explicit IDs on flat compatible participants. These are documented scope limits.
