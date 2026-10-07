# Lambda Slide Presentation Package — Proposal

> **Status:** implementation in progress, 2026-10-06; package behavior authorized by the user.
> **Implementation:** [progress and verification](impl/Lambda_Impl_Slide_Presentation.md);
> [current public API](../doc/Lambda_Slide.md). Static/live playback and the effect
> catalog are implemented; release profiling and native lifetime checks remain open.
> **Scope:** a presentation package written primarily in Lambda Script, with
> slide elements lowered to HTML/CSS/SVG and playback orchestrated in Lambda.
> PPTX import and export are explicitly outside this phase.
> **Spec linkage:** D7.2.4 (shipped package namespace), D7.5.3 (Lambda/Radiant
> boundary), S12.1.1v2 and S12.1.3 (effects and reactive templates), S2.6.3
> (element content construction), D4.5.1v4 (Radiant ownership), D5.3.3 (precise
> script roots), D4.6.1v3 (property identity).
> **Authority:** this document proposes package behavior and additive host
> mechanisms. It does not revise any formal semantics or design ruling.

## 1. Purpose

Provide `lambda.slide`, a source package for authoring and playing slide decks.
A deck is an ordinary Lambda element tree. Slides contain text, images, shapes,
HTML, SVG, and elements produced by existing Lambda packages. A transformation
turns that tree into Radiant-compatible HTML/CSS/SVG; Lambda procedures control
navigation, animation cues, timing, and interaction.

The package should support familiar presentation behavior: advance a build on
click, reveal bullet points, animate objects together or in sequence, transition
between slides, pause, replay, and navigate backward. The first deliverable is a
useful player, not an attempt to reproduce every PowerPoint effect.

The implementation should keep the following in Lambda:

- Source validation, normalization, theme resolution and slide composition.
- HTML/CSS/SVG generation and integration with chart, graph, math and LaTeX output.
- Cue compilation, effect presets, easing and interpolation.
- The player state machine, keyboard/pointer policy and progress controls.
- Deterministic sampling, rewind, skip and replay behavior.
- Authored motion paths and, subsequently, explicit-ID object matching for Morph.

Radiant supplies layout, text shaping, image/vector painting, hit testing, input,
frame delivery and correct visual invalidation. Native code must not contain a
slide-specific state machine, effect catalog, cue scheduler or presentation AST.

## 2. Scope and delivery targets

### 2.1 Initial scope

- A fixed logical slide canvas, with configurable dimensions and aspect ratio.
- A theme and reusable layout conventions: title, section, content and blank slides.
- Positioned objects, flow-layout content and nested groups.
- Next/previous, jump-to-slide, restart, pause/resume and skip-current-build.
- Click-triggered builds, parallel effects, sequences and slide-entry cues.
- Appear/disappear, fade, fly, zoom, spin and basic emphasis effects.
- Cut, fade and push/slide transitions between two live slide subtrees.
- Reduced-motion mode, readable static output and deterministic frame snapshots.
- A Lambda-only live player running in `lambda view`.

### 2.2 Follow-on work within the package direction

- Rectangular and shape reveals using supported clipping primitives.
- Authored motion paths, paragraph-level builds and presenter controls.
- Morph for objects with explicit identities and compatible geometry.
- Optional performance mechanisms justified by release measurements.

### 2.3 Outside this phase

- PPTX parsing, serialization, compatibility and effect-ID translation.
- A visual slide editor, Office-compatible masters and Office layout fidelity.
- Video encoding, narration synchronization and recording workflows.
- Full 3D transition catalogs, page curls, arbitrary mesh deformation and particles.
- A standalone interactive browser export or a JavaScript presentation runtime.

The live artifact is a Lambda script plus package/assets, opened by Radiant.
Serialized HTML/CSS/SVG is a static projection unless a Lambda-capable host loads
the associated package and script. Formatting a live element tree as HTML does
not serialize its template registry, handler closures or player state.

## 3. Existing foundations and concrete gaps

The following inventory reflects source inspection on 2026-10-06, not a new
conformance or performance measurement.

| Foundation | Available today | Consequence for this package |
|---|---|---|
| Source packages | `lmd/package/`, canonical `lambda.*` imports | Use `lambda.slide` under D7.2.4. |
| Reactive templates | `view`, instance `state`, `on` handlers, `emit`, `apply` | Reuse for the player shell and discrete controls; S12.1.3. |
| DOM operations | Selection, attributes, events and geometry; per-property style writes exist in the catalog | Reuse `dom`; the style setter is not currently published to its realm-neutral Lambda face. |
| Rendering | HTML/CSS, SVG, opacity, clipping and transforms | Slides need no separate renderer. |
| Native animation | Frame clock, CSS keyframes, easing, scheduler, pause/resume | Reuse frame delivery; share lower-level consumers when needed. |
| CSS transitions | Implemented for opacity, colors and selected dimensions | Useful, but incomplete transform/reversal support cannot own player timing. |
| SVG animation | Document time, pause/seek and substantial SMIL support | Useful for embedded content; no need to make SMIL the deck representation. |
| Web Animations facade | Explicit `currentTime` sampling exists | `play`/`pause` are no-ops and `reverse` does not implement playback. |
| Lambda frame delivery | No request/cancel frame operation found in `dom_api.def` | A generic host seam is needed for the preferred live sampling path. |

The animation scheduler is a mechanism, not a presentation timeline. A deck
needs cue boundaries, object identity, reset rules and navigation policy above it.

The older RAD_16 statement that CSS transitions are unwired is stale relative to
`css_transition_start` and `css_transition_resolve`. This proposal uses the
current code inventory and the newer support matrix; it does not treat stale
implementation documentation as a semantics ruling.

There is no requirement to finish the entire Web Animations API before shipping
slides. Explicit Lambda sampling can drive existing style/attribute mechanisms
directly. Likewise, there is no requirement to add new language syntax or a
presentation CLI command.

## 4. Package architecture

```text
Authored Lambda presentation elements
                |
                v
      lambda.slide.compile                 pure Lambda
      validation + normalized scene + cues
                |
                +----------------------+
                |                      |
                v                      v
      lambda.slide.page         lambda.slide.snapshot
      templates + HTML          deterministic static HTML/SVG
                |
      controls and frame events
                |
                v
      Lambda player reducer + sampler
                |
      per-target visual patches
                |
                v
      dom / radiant-dom host boundary
                |
                v
      Radiant cascade, layout, paint and hit testing
```

The normalized scene and cue plan are immutable values. Mutable playback state
belongs to one player instance. Compilation and sampling are `fn`; frame
requests, DOM commits and event handling are `pn`, following S12.1.1v2.

The package is an author package selected by its own presentation templates. It
must not register broad behavior templates that change unrelated HTML elements
or interfere with `lambda.dom`'s user-agent behavior.

### 4.1 Suggested package structure

```text
lmd/package/slide.ls      public entry points (explicit lambda.slide module)
lmd/package/slide/
  common.ls              validation and scalar formatting helpers
  normalize.ls           checked compilation and source schema
  scene.ls               scene construction and source-to-render mapping
  html.ls                HTML/CSS/SVG lowering and static projections
  theme.ls               themes and reusable layouts
  timeline.ls            cue dependency compilation and target preparation
  effects.ls             declarative preset expansion
  easing.ls              pure easing functions
  sample.ls              deterministic track and scene sampling
  player.ls              pure command/event reducer
  live.ls                frame requests, DOM writes, templates and input policy
  transitions.ls         shared slide transition sampling
  keyframes.ls           typed channel/keyframe compilation and sampling
  color.ls               accepted color forms and alpha-aware interpolation
  motion.ls              authored line/cubic path sampling
  morph.ls               explicit-ID compatible-object matching
  paragraphs.ls          paragraph boxes and click-build helpers
```

This is a responsibility map, not a requirement to create every file immediately.
Keep a single shared track sampler for object effects and slide transitions.
Effect presets should expand into data rather than duplicate interpolation code.

## 5. Authoring surface

### 5.1 Element vocabulary

| Element | Purpose |
|---|---|
| `<presentation>` | Deck metadata, canvas dimensions, theme and slide content. |
| `<slide>` | A slide, stable source ID, background, transition and advance policy. |
| `<text>` | Rich text in a positioned or flow-layout content box. |
| `<image>` | Image source, fit mode and logical bounds. |
| `<shape>` | Package convenience shapes lowered to SVG. |
| `<group>` | A local coordinate system and shared effect target. |
| `<content>` | An arbitrary HTML/SVG/package-produced subtree in a slide box. |
| `<cue>` | One entry-triggered or click-triggered build group. |
| `<parallel>` | Child effects start together. |
| `<sequence>` | Child effects run in order. |
| `<effect>` | Target, preset or typed tracks, duration, delay and easing. |
| `<notes>` | Speaker notes, excluded from the audience canvas. |

These names are interpreted within the presentation source tree. A raw SVG
`<text>` nested inside `<content>` remains an SVG element; the slide normalizer
does not recursively reinterpret arbitrary foreign content as package syntax.

Slides accept ordinary elements through `<content>`, so charts, diagrams,
equations and custom Lambda transformations require no new native object kind.
All animated content boxes get stable package wrappers, even when their child
tree is produced by another package.

### 5.2 Example deck

The following illustrates the proposed package API. The package does not exist
yet; the vocabulary uses current Lambda element/import syntax.

```lambda no-run
// no-run: depends on the proposed lambda.slide package.
import slide: lambda.slide

let deck = <presentation title: "Quarterly review", width: 1280.0,
    height: 720.0, theme: 'light',
    <slide id: "opening", transition: 'fade', transition_duration: 300.0,
        <text id: "title", x: 80.0, y: 100.0, width: 1120.0, height: 100.0,
            role: 'title', "Quarterly review">
        <text id: "subtitle", x: 80.0, y: 220.0, width: 1120.0, height: 80.0,
            "Revenue, customers and next steps">
        <cue id: "opening-title", start: 'entry',
            <effect target: "title", kind: 'fade-in', duration: 400.0>
        >
        <cue id: "opening-subtitle", start: 'click',
            <effect target: "subtitle", kind: 'fly-in', from: 'bottom',
                distance: 40.0, duration: 350.0, easing: 'ease-out'>
        >
    >
    <slide id: "results", transition: 'push', direction: 'left',
        transition_duration: 350.0,
        <text id: "heading", x: 80.0, y: 60.0, width: 1120.0, height: 90.0,
            role: 'title', "Results">
        <shape id: "revenue", kind: 'rect', x: 100.0, y: 220.0,
            width: 400.0, height: 280.0, fill: "#3b82f6">
        <text id: "explanation", x: 580.0, y: 220.0, width: 600.0,
            height: 280.0, "Revenue increased this quarter.">
        <cue id: "result-build", start: 'click',
            <parallel
                <effect target: "revenue", kind: 'zoom-in', duration: 450.0>
                <sequence
                    <effect target: "explanation", kind: 'fade-in',
                        duration: 300.0, delay: 150.0>
                    <effect target: "explanation", kind: 'pulse',
                        duration: 250.0>
                >
            >
        >
        <notes "Explain the customer mix before advancing.">
    >
>

slide.page(deck)
```

The live usage is an ordinary source file:

```sh
./lambda.exe view examples/review.ls
```

No syntax changes, generated parser edits or new built-in slide types are needed.

### 5.3 Public API proposal

| Entry point | Contract |
|---|---|
| `compile(deck, options)` | Pure checked compilation into scene, cue plan and source mapping. |
| `page(deck, options)` | Pure construction of the live player document and template application. |
| `snapshot(deck, address, options)` | Pure static projection at a slide/cue/local-time address. |
| `slides(deck, options)` | Pure array of static slide elements for handouts or existing output tools. |
| `initial_state(plan, options)` | Pure construction of one player state. |
| `reduce(plan, state, event)` | Pure updated playback state; the live adapter derives commits from state changes. |
| `sample(plan, state)` | Pure visual patch array; `needs_frame(state)` supplies the active/waiting wake decision. |
| `needs_frame(state)` | Whether an unpaused cue or transition needs another frame. |
| `player(deck, options)` | Embedded player with an explicit unique instance ID. |
| `paragraphs(id, content_blocks, options)` | Authored paragraph group plus click cues. |
| `handout(deck, options)` | Static final-state slides with speaker notes. |
| `diagnostic(plan, state, frame_token)` | Pure supplied-state diagnostics. |

`page`, `snapshot` and `compile` report checked validation failures rather than
producing a partly playable document. Exact result/error type signatures should
be fixed with the implementation, using the existing S7 error conventions.
No unchecked map masquerading as a native player handle is required.

There should be one source model. Constructor helpers may return the same
elements, but must not introduce a second, independently interpreted deck schema.

## 6. Source model and lowering

### 6.1 Logical canvas and layout

Canvas positions and dimensions are floating-point values in logical CSS pixels.
The default can be 1280 × 720; the deck may specify another positive size.
Radiant layout code remains float-based throughout.

The outer stage fits the slide uniformly into the available viewport:

```text
scale = min(viewport_width / slide_width, viewport_height / slide_height)
offset_x = (viewport_width - slide_width * scale) / 2
offset_y = (viewport_height - slide_height * scale) / 2
```

The scene is laid out at its logical size. The stage applies one enclosing
translate/scale transform. Resizing changes the stage transform, not the
authored coordinates or a track's endpoints. Interactive controls stay outside
that transformed canvas. Pointer hit testing must use the same transform as paint.

Positioned source objects lower to ordinary absolute-positioned boxes. Flow
content uses Radiant flex/grid/block layout inside an explicitly sized box. The
first phase does not animate line breaking, font size or arbitrary reflow.

### 6.2 Wrapper composition

Separate the following transform responsibilities with ordinary nested elements:

1. Stage fit transform.
2. Slide transition transform and opacity.
3. Object authored placement/base transform.
4. Object effect transform, opacity and supported clip.
5. Content subtree.

This is the package's scene representation: a parent/group transform composes
with a child transform using normal renderer semantics. It avoids overwriting an
author's transform when applying a fly-in or a zoom. It is not a substitute for
missing renderer support; any incorrect nesting, opacity or hit testing is a
Radiant defect to investigate at its root.

Store the baseline style and visual state in the compiled scene. Sampling derives
the complete package-owned visual values from that baseline; it does not read
last frame's inline style as the next frame's input.

### 6.3 IDs and target mapping

- Slide IDs are unique within the deck; object IDs are unique within a slide.
- Cue IDs are unique within a slide, and all effect targets must resolve there.
- Explicit IDs are required for animated targets and Morph participants.
- Unanimated source objects may receive deterministic structural IDs at compile time.
- DOM IDs are namespaced by player instance, slide and object, so two decks can
  coexist without collisions. Generated IDs remain implementation details.
- Object IDs identify package model objects, not equality of DOM wrapper values.
  Live node identity uses the existing `dom.same_node` operation when needed.

Morph identity may intentionally repeat across slides. It is separate from the
DOM ID of either rendered instance. SVG resources also need namespacing so masks,
clip paths, gradients and references do not leak between slides.

### 6.4 Content and styles

Keep theme styles in scoped CSS. Use CSS for typography and layout, SVG for
shapes and diagram content, and ordinary element attributes for resources.
Do not emit a JavaScript effect runtime.

Array-backed children are explicitly spliced into element content; ordinary
for-expression lists already spread there, under S2.6.3. Do not accidentally
place an array as a single content item when constructing slides.

Every animation property has one owner. The package controls its wrappers;
embedded content may control its descendants. Running CSS/SMIL effects on the
same package-owned transform or opacity is rejected or explicitly opted out of
package sampling. Initial support for embedded autonomous animation may let it
run independently; it must be documented as outside deck seek/replay guarantees.
Synchronized embedded animation needs an explicit timeline adapter.

### 6.5 Assets and readiness

Resolve image, font and stylesheet paths against the source deck's base URI,
not the process working directory. Reuse the existing resource pipeline.

Entry animations begin after the selected slide has a committed layout and its
required resources are ready. A missing image/font must produce an actionable
diagnostic; geometry-dependent effects cannot capture an arbitrary zero box and
quietly proceed. The adapter should expose readiness through existing document
events where sufficient, extending generic resource/layout notifications only
if those hooks cannot serve a Lambda-only document.

## 7. Timeline and cue semantics

### 7.1 Explicit cue groups

The source structure makes interactive pauses explicit:

- `start: 'entry'`: automatic group after slide activation and readiness.
- `start: 'click'`: group waits for an advance command.
- `<parallel>`: children share a start boundary; the group's duration is the
  maximum child completion time.
- `<sequence>`: each child starts after the previous child finishes, including
  its delay and finite repeats.

An effect's duration, delay and all API time addresses are in milliseconds.
The host's monotonic seconds are converted once at the adapter boundary.
Durations/delays are nonnegative finite values in the initial source vocabulary;
negative delays are not imported from CSS animation semantics.

`repeat` defaults to one and accepts positive finite integer counts. Indefinite
decorative loops must live outside a blocking cue, so they cannot prevent the
presenter from advancing. Entry cues form an initial prefix of the slide's cue
list; subsequent cues are click-triggered. Group children may themselves be
groups. Validate these constraints and unsupported attributes during compilation.

Convenience authoring for `on-click`, `with-previous` and `after-previous` may be
added by normalizing to these groups. The explicit group form is authoritative
for this package and avoids ambiguous references to an implicit previous effect.

The compiler emits a dependency plan and tracks with resolved local start/end
times. It checks missing targets, cycles, unsupported easing, invalid lengths and
ambiguous property writers. The runtime never discovers these errors halfway
through a click sequence.

### 7.2 Visibility, fill and reset

An entrance effect declares an initially hidden target; an exit effect ends in a
hidden target. Fade-hidden objects retain layout and are not interactive. The
package must enforce focus and hit-test eligibility, not only paint opacity.

Completed cues hold their resulting state until another cue changes it or the
slide is reset. Rewind derives state from the source baseline and the preceding
cue boundaries; it does not reverse a series of imperative style writes.

For multiple tracks affecting one property:

- Sequential replacement is allowed with explicit resolved endpoints.
- Parallel replacement writers are rejected as ambiguous.
- Translate/scale/rotate components may compose where the effect schema defines
  distinct channels and a fixed transform order.
- General additive CSS compositing is not part of the first phase.

An emphasis effect such as pulse returns to its baseline by definition. It does
not permanently change the next effect's starting scale.

### 7.3 Stable playback addresses

A playback address is `{slide, cue, time_ms}`; a cue identifies a compiled build
boundary, not a count of wall-clock clicks. A slide's initial state and final
state have named addresses. This makes test frames, seek and replay reproducible.

The state stores a monotonic anchor and logical time. While running:

```text
logical_time = anchor_logical_time + (frame_time - anchor_frame_time) * rate
```

When paused, logical time stays fixed. Seek changes the logical anchor and
resamples immediately. Missed frames sample the current logical time; there is
no assumed 16.67 ms increment, incremental drift or backlog of obsolete frames.

Clicks introduce untimed barriers. A timed capture or whole-deck autoplay must
provide an explicit dwell schedule for those barriers; there is no intrinsic
wall-clock time for a presenter deciding when to click.

The shipped Play control supplies that schedule through the positive finite
`autoplay_dwell_ms` player option (default 3000 ms before each click build or
slide advance). `autostart` activates only entry cues. Dwell time is separate
from sampled cue time so completed visuals remain fixed while waiting. This
policy stays in the Lambda reducer and procedural frame handlers
(S12.1.1v2/S12.1.3, D7.5.3).

The toolbar provides a native range input for 0.5×–4× playback in 0.25× steps,
with an accessible label and a live multiplier readout. A positive finite rate
scales cues, transitions and dwell on the same logical clock. On a speed event,
sample using the old rate, then reanchor at that progress before applying the
new rate. Pause state and the selected rate survive speed changes and Restart;
manual navigation retains the rate. Missed-frame boundary timestamps divide
remaining logical duration by the rate before carrying time to the next phase.

## 8. Player behavior and lifecycle

One player instance owns its active slide, cue cursor, play state, transition
state, time anchor, pending frame token and session generation. The reducer
returns actions; a Lambda procedure performs them and records their results.

| Situation / command | Proposed behavior |
|---|---|
| Activate a slide | Establish initial state, commit layout, then run entry cues. |
| Play | Start automatic playback or resume a paused cue/transition/dwell; replay from slide 1 if finished. Repeated Play preserves the active clock. |
| Advance while awaiting a click | Start the next click cue. |
| Advance while a cue runs | Finish that cue immediately; require another command to start the next. |
| Advance after the last cue | Start the next slide transition. |
| Advance during a transition | Finish the transition; do not skip another slide on that command. |
| Previous during a cue | Restore that cue's entry boundary and wait. |
| Previous at a completed cue boundary | Restore the preceding cue boundary. |
| Previous at a slide's initial boundary | Activate the preceding slide at its final build state, without an entry replay. |
| Pause / resume | Freeze / continue logical time for the current cue or transition. |
| Jump to a slide | Cancel current work and activate the selected slide at its initial boundary. |
| Restart slide / deck | Reset to the corresponding initial state and replay entry cues. |
| Seek | Sample an explicit address without replaying historical input events. |

The first version does not require continuous negative-rate playback. Backward
navigation is deterministic boundary reconstruction. A later reverse command
can share the sampler once its event-crossing policy is defined.

The toolbar's Restart command resets the deck; `restart-slide` resets only the
current slide. Manual navigation cancels autoplay. Automatic playback stops at
the final build and releases its frame request; reduced motion snaps animations
while preserving the dwell schedule.

Initial activation must occur in a procedure, never as a hidden effect of
`page()`'s pure template body. An explicit Play control can bootstrap the first
frame from its click handler. Default autostart additionally needs a readiness
event delivered to the author template; audit that existing hook and add generic
delivery if missing, as described in section 11.4. Do not perform frame requests
inside a supposedly pure element transformation.

### 8.1 Input policy

Space, Right, PageDown and an explicit Next control advance. Left/PageUp go
backward; Home/End select deck boundaries. Escape leaves presentation/fullscreen
mode when available. Keyboard shortcuts do not intercept an editable field,
embedded form control or an application shortcut with modifiers.

Pointer advance applies to blank stage content. Links, buttons and interactive
embedded content keep their own default action. Provide explicit navigation
controls so touch or pointer use does not require clicking through a chart.

Input and frame events both go through the same Lambda reducer. A frame callback
does not introduce a second copy of player state in native code.

### 8.2 Discrete state versus frame updates

Use reactive templates for structural changes, slide selection and control text.
Use narrow DOM visual writes for per-frame object values. Do not rebuild the
whole deck or rerun expensive chart/math transformations on each frame.

Frame-request bookkeeping may change in a handler without requiring replacement
of the rendered slide subtree. If the existing template update path always
rebuilds or relays out that subtree for an otherwise unchanged output, investigate
that path's root cause. A paint patch API is not permission to duplicate a second
DOM or bypass the engine's authoritative state.

### 8.3 Cancellation and disposal

Every request is bound to a player root and session generation. On navigation,
source replacement or disposal, cancel its frame token and invalidate the old
generation. Late delivery is ignored before resolving target nodes.

Active targets are stable DOM handles, not cached raw layout `View*` pointers.
DOM handle generations are checked after relayout/removal, under D4.5.1v4.
Native queued callbacks/events retain script values through precise traced roots
under D5.3.3; conservative native-stack scanning is not an option.

The host automatically cancels document-owned requests on destruction. This must
not depend on a user script remembering to run cleanup after the document dies.

## 9. Lambda sampling and effect implementation

### 9.1 Shared track representation

Each preset expands to typed tracks with target ID, property/channel, begin time,
duration, easing, from/to values and fill behavior. Scalar tracks use numbers;
transform tracks use numeric components; color tracks use channel values.
CSS/SVG serialization happens after interpolation, not by parsing strings every
frame. Colors need one documented interpolation space and alpha convention.

For the initial package contract, interpolate sRGB channel values with
premultiplied alpha, then unpremultiply for CSS serialization; zero alpha yields
zero color channels. This is a package sampling choice, not a change to CSS's
native interpolation rules. Tests must cover transparent endpoints.

The sampler is a pure function of the plan, baseline and playback address.
The same function serves live frames, seek, screenshots and tests.

A small part of that sampler is ordinary Lambda code:

```lambda
fn clamp01(t) => max(0.0, min(1.0, t))

fn progress(time_ms, begin_ms, duration_ms) {
    if (time_ms < begin_ms) 0.0
    else if (duration_ms <= 0.0) 1.0
    else clamp01((time_ms - begin_ms) / duration_ms)
}

fn lerp(a, b, t) => a + (b - a) * t

fn ease_out_cubic(t) => 1.0 - (1.0 - t) ** 3

fn sample_scalar(track, time_ms) {
    let t = progress(time_ms, track.begin_ms, track.duration_ms)
    let eased = if (track.easing == 'ease-out-cubic') ease_out_cubic(t) else t
    lerp(track.from, track.to, eased)
}

sample_scalar({begin_ms: 100.0, duration_ms: 200.0,
    from: 0.0, to: 1.0, easing: 'linear'}, 200.0)
```

This deliberately illustrates only a validated scalar track. The full easing
dispatcher rejects unknown values during compilation. Cubic-bezier inversion,
steps and reusable color/transform interpolation can also be implemented in
Lambda; none requires an effect-specific native function.

### 9.2 Initial effect catalog

| Effect | Lambda track expansion |
|---|---|
| Appear / disappear | Discrete visual visibility at a cue boundary. |
| Fade in / out | Opacity from 0 to 1 / 1 to 0. |
| Fly in / out | Translation between an authored offset and zero, optionally with opacity. |
| Zoom in / out | Scale plus optional opacity, with a declared origin. |
| Spin | Rotation from/to authored angles. |
| Pulse | Piecewise scale track returning to baseline. |
| Highlight | Color/background track on a package-owned surface. |
| Fade transition | Incoming slide opacity over an outgoing slide at full opacity. |
| Push transition | Coordinated outgoing and incoming slide translations. |
| Slide transition | Incoming slide translation over a stationary outgoing slide. |

Fade compositing must include each slide's own background. Fading both slides
down simultaneously can expose the stage backdrop and produce a different
visual result; that must be an explicit effect variant rather than an accident.

Use arbitrary keyframe track stops for more complex effects. Presets are data
generators, not separately coded tick loops. Initially support linear, selected
named easing and cubic-bezier; expand steps after boundary tests exist.

### 9.3 CSS keyframes as an optional execution adapter

Generating CSS keyframes is useful for autonomous decorative content and may
later optimize suitable tracks. It is not the primary deck clock: independent
CSS start times, fill, cancellation and layout-dependent restarts cannot silently
change the package's seek/cue semantics.

An optimized adapter must match the pure sampler at fixed timestamps and expose
enough control to pause, seek, cancel and synchronize. Do not use the current
no-op Web Animations methods or a sleep-based approximation as its controller.

## 10. Slide transitions and richer animation

### 10.1 Two live slide subtrees

During a transition, retain the outgoing and incoming slide under a clipped
stage. Their transition wrappers receive tracks from the same sampler. Only the
destination becomes active for input after the transition completes; disable
audience-content input during the overlap while keeping navigation controls live.

The incoming slide starts in its initial build state. Entry cues start after
transition completion and readiness in the first version. This order prevents
an entrance effect from finishing while its slide is still offscreen.

Initially keep only the active slide and an adjacent transition partner mounted.
Source scenes remain immutable package values. Resource caching stays with the
engine; this package should not retain every slide DOM and animation instance
indefinitely.

### 10.2 Reveals and motion paths

Implement rectangular/shape reveals by sampling supported `clip-path` or SVG
clip geometry. Existing static clip support does not prove live updates have
correct dirty bounds; verify them before advertising a reveal preset.

For authored motion, parse or accept numeric line/bezier segments in Lambda.
Lambda samples position and optionally the tangent angle. Constant-distance
motion can use an arc-length table computed once. It does not require SVG
`animateMotion` to exist. Use the same transform channels as fly effects.

Paragraph builds should start with explicitly authored paragraph/bullet groups.
Glyph/word builds require measured text segmentation and are later work; do not
split shaped text into guessed character boxes.

### 10.3 Morph

The first Morph design matches explicit `morph_id` values across two slides and
interpolates authored position, size, rotation, scale, opacity and supported colors.
Unmatched outgoing/incoming objects fade out/in. Duplicate matches are errors.

Morph must define the content model: compatible content keeps one transition
representation; changed text or incompatible shape geometry crossfades. It must
not claim arbitrary path or text morphing based on matching IDs alone.

Start with flat, authored-bounds objects. A later layout-measured variant must
convert committed geometry into logical slide coordinates once before playback,
after fonts/resources settle, and account for the fit and parent transforms.
Do not treat viewport bounding boxes as untransformed object geometry.

## 11. Required and conditional Lambda/Radiant extensions

All names below are proposed API sketches, not implemented operations. Add
shared operations through the existing DOM catalog/module contracts rather than
hand-maintaining parallel Lambda and JS bindings. Preserve D7.5.3: `lambda-rt`
must not link Radiant.

### 11.1 Required: Lambda-callable frame request and cancellation

Preferred sketch:

```text
dom.request_frame(owner, event_name) -> request token / checked error
dom.cancel_frame(owner, token) -> success / checked error
```

Request one event at the next frame opportunity for a live owner. The event
contains a monotonic timestamp in milliseconds and the request token. It is
delivered through existing Lambda event/template dispatch; the `on` handler
samples and requests another frame only while time-dependent visuals need it.

This reuses the host frame clock without requiring a JavaScript realm. The host
owns token allocation and owner validity; Lambda owns the pending-token field and
all playback decisions. The exact operation names and event payload need an API
review, but the requirement is concrete: frame-paced Lambda delivery with cancel.

Frame callbacks run on the UI thread, outside layout/paint. Define the ordering
against input, pending style work, layout and existing CSS/SVG animations, so the
Lambda patch lands before the next paint and completion events do not reenter
layout. Coalesce wakeups; an idle deck parks rather than running a periodic timer.
Cancellation during delivery and document destruction must be safe.

Headless operation needs the same delivery path with an explicitly supplied
virtual timestamp. Existing JS virtual frame advancement does not establish a
Lambda-only frame API. Pure snapshot tests remain usable before that host seam
is added.

### 11.2 Required: realm-neutral property writes and correct invalidation

The existing `style_set_property` catalog row has no `DOM_F_NEUTRAL` flag, and
`dom_module.cpp` only publishes realm-neutral rows. Consequently this proposal
cannot assume `dom.style_set_property` is already callable from Lambda. Extend
the existing setter/bridge to work without a JS realm, exercise it from a
Lambda-only document, then publish that same operation. Do not add a duplicate
slide-specific setter or mark the row neutral without proving the dependency
has been removed. Ordinary SVG attributes already have the neutral
`dom.set_attribute` surface.

Verify transform, opacity, supported clip and SVG geometry changes through those
shared operations. They must update authoritative state, invalidate old and new
visual bounds, and keep hit testing consistent with paint.

If a setter invokes JS realm machinery, loses a value after restyle, misses
transformed descendants or leaves stale retained paint, fix that shared consumer
at its root. Do not write raw `View` properties from the slide package or rebuild
the whole document to hide the defect.

Transforms and opacity should request repaint without unnecessary layout;
layout-affecting changes must request actual reflow. This distinction needs
verification in the mutation/dirty pipeline; it is not assumed from property names.
The initial catalog excludes layout-changing animation to keep that work bounded.

### 11.3 Conditional: batched visual patch commits

If measured frame cost is dominated by per-property native calls or repeated
cascade/invalidation, propose a generic operation such as:

```text
dom.commit_visual_patches(owner, patches) -> success / checked error
```

Each patch names a generation-checked target and typed supported properties.
Validate the batch before committing, coalesce change notifications, then settle
style/layout/paint once at the existing boundary. Reuse existing property
metadata, parsers and consumers; D4.6.1v3 keeps canonical property identity.
This is a mechanism for any Lambda UI animation, not a native slide-effect API.

Do not introduce a second style store to accelerate slides. If an animated-value
layer is ultimately necessary, it needs a shared cascade/ownership design and
must be consumed by paint, geometry and hit testing consistently. That is a
separate extension, not an unreviewed optimization hidden in this package.

### 11.4 Conditional: readiness, resize and lifecycle notification

The adapter needs document readiness, committed layout, stage resize and owner
disposal. Reuse existing events/listeners wherever they are available to Lambda.
Add generic hooks only after a live probe identifies a missing delivery path.
Do not give the package a native polling loop over document internals.

### 11.5 Later: synchronized SVG time and render surfaces

Expose the existing SVG pause/seek mechanism through the shared catalog only
when embedded SVG needs to follow the deck clock. Script-side numeric SVG
attribute sampling needs no SMIL control extension.

A reusable offscreen subtree/surface mechanism may help expensive transitions,
complex masks or large scenes after profiling. Basic cut/fade/push effects do not
require it. Preserve current PaintIR/display-list abstractions; do not create a
second renderer or flatten all slide text into textures by default.

### 11.6 Language/runtime changes

No new parser construct, scalar/container type, MIR backend or GC strategy is
proposed. Existing functions, procedures, elements, arrays, templates and modules
are expressive enough. Runtime work is limited to any missing callback/event
bridge required for the generic host mechanisms above.

Full Web Animations playback, animated 3D matrix decomposition, animated filters
and global CSS transition conformance are independent improvements. They are
not prerequisites for the initial Lambda-sampled catalog.

## 12. Accessibility, static output and observability

Reduced-motion mode applies each cue's final state immediately and uses cut
transitions; it preserves click/build structure unless the user selects a
separate all-content mode. Keep visible Previous/Next controls, keyboard access,
slide titles/counts and an optional outline. Hidden/inactive slide content must
not retain focus or intercept pointer input.

The initial reduced-motion flag is a player option. Reading a system preference
is a later generic host integration if not already available.

Static projection supports initial, final and explicit cue/time addresses.
Existing HTML/SVG/PNG/PDF paths may consume the projected result where supported;
this is not a new PDF pagination or animation-export promise. Multi-slide handout
layout is package-generated ordinary HTML. Resources must be resolved or packaged
explicitly rather than silently depending on the authoring machine's directory.

Expose a player diagnostic snapshot: deck/slide/cue ID, logical time, play state,
active tracks and outstanding frame token. Log with distinct package/host prefixes
using existing logging facilities. Runtime errors pause the affected player and
report a source target/cue location; invalid effects are never silently dropped.

## 13. Delivery milestones and acceptance criteria

### 13.1 Static package

Deliver source schema, themes, content lowering, `page`'s static structure,
`snapshot` and a demo deck with text, SVG and an existing package's output.
Acceptance: deterministic dimensions/IDs; two independent decks; missing-target
diagnostics; correct scaling; initial/final static frames.

### 13.2 Live player and essential host seam

Deliver the generic frame request/cancel operation, realm-neutral property writes,
Lambda reducer/sampler,
click/entry cues, pause/resume, boundary navigation and disposal.
Acceptance: live Lambda-only document; no JS runtime dependency; no idle frame
wakes; deterministic skip/replay; navigation while a callback is pending; safe
close during playback; correct old/new dirty bounds and hit testing.

### 13.3 Basic visual catalog and slide transitions

Deliver appear/fade/fly/zoom/spin/pulse and cut/fade/push/slide transitions.
Acceptance: parallel/sequence timing, zero duration, groups, themed backgrounds,
rapid advance commands, reduced motion and replay after resize.

### 13.4 Richer package effects

Deliver supported reveals, authored paths, paragraph builds and explicit-ID Morph.
Acceptance: truthful advertised geometry/content limits; compatible-object
interpolation; crossfade of incompatible content; duplicate-ID diagnostics.

### 13.5 Performance-informed host work

Measure a release build on text/image/SVG-heavy decks at 1280 × 720 and 1920 × 1080,
including a scene with roughly 100 simultaneously animated wrapper targets.
Report frame-time percentiles, allocation growth, dirty/reflow counts and idle
wake behavior, separating Lambda sampling, host commits, layout and paint.
Choose an explicit target such as a 60 Hz frame budget on named hardware; do not
claim it from existing scheduler availability.

Add batch commits, persistent scratch reuse or retained surfaces only where the
measured bottleneck warrants them. A debug build is never performance evidence.
Bound object/DOM/resource retention during repeated navigation and long sessions.

Implementation measurements now justify a retained presentation-style channel
(D7.5.3, D4.5.1v4). Replacing a `style` attribute every sample also replaces its
immutable Mark string; reclaiming parser scratch alone cannot bound the Input
arena. The channel should store owned CSS values against generation-checked DOM
nodes, keep authored attributes intact, preserve cascade precedence and reapply
the sampled values after relayout. Clear, owner removal and document teardown
must release the layer. A generic batch commit can validate all patches before
publication and record each changed node once, avoiding false attribute-selector
invalidation. Timing, easing, interpolation and effect presets remain in Lambda.
This is host mechanism, not a native slide model. The retained style layer is
implemented. Transient source-position/selection event snapshots also use
precisely rooted GC values. The reconcile journal grows reusable document-owned
storage rather than falling back at 64 records; record order and node pins remain
intact. Handler temporaries use GC, while template bodies retain UI allocation.
DOM records retain managed backing values until retirement and reload their
borrowed buffers through the backing accessor after compaction
(D4.1.1v2, D4.1.3, D4.3.1, D5.3.3).
Text and comment nodes retain their own backing values after detachment; adoption
preserves the physical GC owner across runtimes. Roots are withdrawn before
document resource destruction can tear down an adopted source heap.
These changes still require long-session validation alongside the
remaining reconcile/view-pool ownership audit and measured frame-cost work.
Ordinary published arena blocks cannot be discarded individually (D4.1.4v5).

## 14. Validation strategy

- Pure Lambda tests for normalization, target resolution, cue compilation,
  sequence/parallel durations, conflicting writers and deterministic sampling.
- Fixed-time expectations for delay, zero duration, easing boundaries, completed
  fill, pause, seek and resize without coordinate drift.
- Reducer tests for advance during cues/transitions, previous, jump and restart.
- Rendering references for nested transforms, opacity/background compositing,
  clipping, SVG resources and text/image/package content.
- Live Lambda-only event tests for frame delivery, cancellation, owner removal,
  multiple players, input routing and stale generations after navigation.
- Mutation tests that force restyle/relayout during playback and then remove a
  target; precise ownership and dirty tracking must remain correct.
- Accessibility/input tests for hidden objects, focus, embedded controls and
  reduced motion.

Every new Lambda `*.ls` regression fixture needs its corresponding expected
`*.txt` file. Host/runtime changes require the Lambda baseline; Radiant changes
require the Radiant baseline. Native layout changes also run the float/int-cast
lint rule. Store temporary captures and probes under `./temp/`.

Implementation status and measured results are recorded separately in
[the implementation report](impl/Lambda_Impl_Slide_Presentation.md). This design
does not establish that the frame budget or retention acceptance criteria pass.

## Appendix A. Implementation touchpoints

| Source | Existing symbol/surface to inspect before implementation |
|---|---|
| `lambda/dom/dom_api.def` | Canonical operation catalog; `bounding_box`, `computed_style`, `style_set_property`, event operations. |
| `lambda/dom/dom_module.cpp` | Lambda-facing catalog registration. |
| `lambda/module/radiant/` | Versioned module/host boundary and Lambda DOM wrappers. |
| `lambda/runtime/radiant_event_hook.cpp` | Native-to-Lambda template/event dispatch. |
| `radiant/frame_clock.cpp`, `radiant/window.cpp` | Frame wake source, UI loop and event/layout/paint ordering. |
| `radiant/animation.cpp` | Shared native scheduler and ownership/cancellation behavior. |
| `radiant/css_animation.cpp` | `css_transition_start`, `css_transition_resolve`, interpolation/property consumers. |
| `lambda/dom/dom.cpp` | `js_web_animation_pause`, `js_web_animation_reverse`, explicit current-time sampling; current facade limitations. |
| `radiant/svg_animation.hpp` | `svg_animation_pause`, `svg_animation_set_time`, document-time controls. |
| `radiant/render.hpp`, retained rendering implementation | Shared PaintIR/display-list, visual invalidation and possible later surface mechanisms. |
| `lmd/package/edit/shell.ls`, `test/lambda/ui/todo.ls` | Existing package player-like shells, templates, instance state and controls. |

Before adding any native helper, search for and reuse an existing implementation.
Promote a reusable static helper through its module header rather than copying it.
Vendor code is outside the implementation scope; any genuinely upstream defect
must follow the project's approval and recorded-patch process.

## Appendix B. Related sources

- [Formal semantics](../doc/Lambda_Formal_Semantics.md): S12.1.1v2,
  S12.1.3 and S2.6.3.
- [Formal design](../doc/Lambda_Formal_Design.md): D7.2.4, D7.5.3,
  D4.5.1v4, D5.3.3 and D4.6.1v3.
- [Documentation convention](../doc/Doc_Convention.md): proposal status,
  authority and code-fence conventions.
- [Lambda packages](../doc/Lambda_Packages.md) and
  [reactive UI](../doc/Reactive_UI.md): package/template usage.
- [HTML/CSS/SVG support](../doc/HTML_CSS_SVG_Support.md#13-transitions-and-animations):
  current animation inventory and limits.
- [Animation/frame scheduling](../doc/dev/radiant/RAD_16_Animation_Frame_Scheduling.md):
  timing architecture; the transition-creation inventory is dated.
- [PaintIR/display lists](../doc/dev/radiant/RAD_12_Paint_IR_Display_List.md):
  shared rendering abstractions.
- [DOM host API design](Lambda_Design_DOM_Host_API.md): extend the shared catalog,
  rather than creating slide-specific bridge code.
