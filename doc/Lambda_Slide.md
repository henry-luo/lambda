# Lambda Slide Presentations

`lambda.slide` authors decks as Lambda elements, compiles their cues, and displays
them in Radiant. The package is source Lambda under D7.2.4; rendering uses the
shared host boundary under D7.5.3. Compilation and sampling are pure functions;
live templates perform DOM writes and request frames under S12.1.1v2/S12.1.3.

## Quick start

A deck is a `.slides` file: Mark data (D2.9.3) whose root is `<presentation>`.

```mark
// hello.slides
<presentation id: "demo", title: "My deck", width: 1280, height: 720, autostart: true,
    <slide id: "hello", layout: 'title-body',
        <text id: "title", role: 'title', "Hello, Lambda">
        <text id: "body", role: 'body', "Click Next to reveal this paragraph.">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 400>>
        <cue <effect target: "body", kind: 'fly-in', from: 'left', duration: 500>>
        <notes "Introduce the package before revealing the paragraph.">
    >
    <slide transition: 'push', <text "The next slide">>
>
```

Open it with `./lambda.exe view hello.slides`. The viewer runs the presenter
`lambda.slide.present.present(deck, {source_path})` as a document transform:
the deck ID names the player (default `"slides"`), `autostart`,
`reduced_motion` and `autoplay_dwell_ms` on `<presentation>` are presenter
defaults, and relative resources resolve against the deck's directory.
Runnable decks are [slide_presentation.slides](../test/demo/slides/slide_presentation.slides)
and [slide_package_content.slides](../test/demo/slides/slide_package_content.slides);
the latter carries a chart produced by `lambda.chart.chart` and paragraph builds.
For a larger presentation, open
[northstar.slides](../test/demo/slides/northstar.slides): an 18-slide fictional
strategy briefing with charts, diagrams, a roadmap, tables and staged reveals.
Its charts use separate slide targets for clockwise donut segments, line-series
reveals and staggered bar fills; axes stay steady and labels appear afterward.
See the [demo guide](../test/demo/slides/README.md) for its contents and controls.

Scripts load a deck with `input("hello.slides", 'slides')^` (an explicit format,
D2.9.2) and can embed it with `player`/`page` below; a deck built by Lambda code
is saved for presenting with `output(deck, "hello.slides", 'mark')^`.

The standalone page fits the logical canvas into the viewer, with letterboxing,
and refits on window resize. Explicit `width`/`height` options override those
dimensions. Authored object coordinates stay in logical slide units.

A live player renders a slide the first time it is shown and keeps it mounted;
moving between slides afterwards changes only state — which layer is displayed,
its transition transform and the sampled object visuals — never slide markup.
The active layer carries `data-slide-active`. Static exports (`snapshot`,
`slides`, `handout`) render every requested slide at once.

## Public API

| Call | Result |
|---|---|
| `compile(deck, options = {})^` | Validated immutable scene/cue plan. |
| `page(deck, options = {})^` | Complete live document, stylesheet and controls. |
| `lambda.slide.present.present(deck, options = {})^` | The `.slides` document transform: `page` with presenter defaults from the deck and `base_uri` from `options.source_path`. |
| `player(deck, options)^` | Embedded live player; requires a unique `options.instance`. Include `lambda.slide.html.css` in the containing document. |
| `snapshot(deck, address = {}, options = {})^` | Static HTML document at a deterministic cue/time address. |
| `slides(deck, options = {})^` | Array of final-state stage elements; supply the shared stylesheet when embedding. |
| `handout(deck, options = {})^` | Final slides with speaker notes, as ordinary HTML with page-break hints. |
| `paragraphs(id, content_blocks, options = {})^` | Array containing a group of paragraph boxes and one click cue per paragraph. Splice this array into a slide. |
| `initial_state(plan, options = {})` | Initial playback state. |
| `reduce(plan, playback, event)^` | Updated playback state for a command or frame. |
| `sample(plan, playback)^` | Deterministic object visuals for the selected slide/build. |
| `needs_frame(playback)` | Whether an unpaused cue, transition or autoplay dwell needs another frame. |
| `diagnostic(plan, playback, frame_token = 0)` | IDs, time, phase, generation, active cue tracks and frame token. |

`options` accepts `instance`, `width`, `height`, `base_uri`, `reduced_motion` and
`autostart`, plus `autoplay_dwell_ms` (default 3000). Dimensions and the dwell
interval are positive finite numbers; flags are booleans. Instance
IDs are nonempty ASCII letters/digits/underscore/hyphen. Embedded players use
their configured dimensions; automatic viewport fitting belongs to `page`.

The compiler rejects unknown attributes, duplicate IDs, missing effect targets,
invalid numeric values, unsupported effects, and overlapping parallel writers
to the same target/channel. Diagnostics name the slide/cue/source path.

## Authoring elements

| Element | Attributes and behavior |
|---|---|
| `presentation` | `id`, `title`, `width`, `height`, `theme: 'light'/'dark'`, `base_uri`; presenter defaults `autostart`, `reduced_motion`, `autoplay_dwell_ms`. Defaults to 1280 × 720. |
| `slide` | `id`, `title`, `background`, `layout`, `transition`, `transition_duration`, `direction`. |
| `text` | Rich content, `font_size`, `color`, and common object attributes. |
| `shape` | `kind: 'rect'/'circle'/'ellipse'/'line'`, `fill`, `stroke`, `stroke_width`, `radius`; lowered to SVG. |
| `image` | `src`, `fit: 'contain'/'cover'/'fill'`. |
| `content` | Ordinary HTML/SVG or another package's element output, inside an authored box. |
| `group` | Nested slide objects with local coordinates. |
| `notes` | Speaker content excluded from the audience canvas; included in `handout`. |
| `cue` | `id`, `start: 'entry'/'click'`; contains effects, `parallel`, or `sequence`. |

Common object attributes are `id`, `x`, `y`, `width`, `height`, `rotation`, `scale`,
`opacity`, `role`, `class`, `style`, and `morph_id`. Width/height/scale are positive;
opacity is in [0,1]. Layouts are `'blank'`, `'title'`, `'title-body'`, and
`'two-column'`; roles are `'title'`, `'body'`, `'left'`, and `'right'`. Explicit
bounds override layout defaults. Flow content uses normal Radiant layout inside
its box. CSS/SVG feature support remains Radiant's support matrix.

Effect targets are author IDs, distinct from generated DOM IDs. Generated keys
include player instance, slide index and object position, so independent players
can share an authored deck. Element array content is spliced with `*` (S2.6.3).

```lambda
let builds = slide.paragraphs("points", ["One", <p "Two">],
    {x: 80.0, y: 180.0, width: 900.0, line_height: 100.0})^;
<presentation <slide *builds>>
```

Paragraph boxes have explicit height (`line_height`, default 90), width (1000),
font size (32), color, x/y, effect `kind`, `duration` (300), and `easing`. Set
dimensions appropriate to wrapping content; the helper does not measure text.

## Effects and transitions

Effects accept `target`, `kind`, `duration` in milliseconds, `delay`, `easing`, and
positive integer `repeat`. Supported easing names are `'linear'`, `'ease'`,
`'ease-in'`, `'ease-out'`, `'ease-in-out'`, `'ease-in-cubic'`, `'ease-out-cubic'`,
and `'ease-in-out-cubic'`; four-number cubic Bézier arrays are also accepted.

| Kind | Additional attributes |
|---|---|
| `appear`, `disappear`, `fade-in`, `fade-out` | None. |
| `fly-in`, `fly-out` | `from: 'left'/'right'/'top'/'bottom'`, `distance`. |
| `zoom-in`, `zoom-out`, `pulse` | `scale`. |
| `spin` | `angle`. |
| `wipe-in`, `wipe-out` | `from`; rectangular CSS inset clipping. |
| `motion` | `path`, optional `orient: 'auto'`. |
| `highlight` | `color`; preceding highlights supply the next starting color. |
| `keyframes` | `channel`, `stops`, optional `from` for clip direction. |

Parallel duration is the longest child span; sequence duration is their sum.
Entry cues must precede click cues. Entrances establish their initial hidden
state before the first build. Completed tracks retain their final values.
Hidden effect wrappers use visibility, pointer exclusion, `aria-hidden` and
`inert`; each player's controls remain outside the animated canvas.

Motion paths contain continuous segments `[x0,y0,x1,y1]` or cubic segments
`[x0,y0,c1x,c1y,c2x,c2y,x1,y1]`. Coordinates are offsets from the authored box.
Cubic paths use a bounded arc-length table; sampling is distance-based, with
optional tangent orientation. A zero-length path is stationary.

Keyframe channels are `'tx'`, `'ty'`, `'sx'`, `'sy'`, `'rotation'`, `'opacity'`,
`'clip'`, and `'paint'`. Stops are `[fraction,value]` pairs beginning at 0 and
ending at 1, with strictly increasing fractions. Colors accept hex
`#RGB[A]`/`#RRGGBB[AA]`, `"transparent"`, or normalized RGBA arrays. Interpolation
uses sRGB channels with premultiplied alpha; CSS serialization follows sampling.
Static CSS paint values may use other supported CSS color syntax.

Slide transitions are `'cut'`, `'fade'`, `'push'`, `'slide'`, and `'morph'`.
`direction` is `'left'`, `'right'`, `'top'`, or `'bottom'`; default duration is
300 ms. Fade keeps the outgoing layer opaque while the incoming layer fades in.
Transitions exclude pointer/focus input on both layers until completion.

Morph matches explicit, unique `morph_id` values on flat objects with explicit
author IDs. It interpolates authored bounds, rotation, scale, sampled endpoint
visuals, background and compatible colors. Changed text/image/content and
incompatible shapes crossfade. Nested participants and marked groups are
rejected; arbitrary SVG path deformation and text reshaping are unsupported.
`lambda.slide.morph.sample(plan, playback)` exposes the transition patches.

## Navigation, sampling and output

Play runs the deck automatically, waiting `autoplay_dwell_ms` before each click
build or slide advance. It resumes paused playback and replays from slide 1
when pressed at the end. Pause/Resume freezes/continues animations and dwell
time. Restart returns to slide 1 and replays its entry cues; `restart-slide`
replays the current slide through the reducer. `autostart: true` runs only the
opening entry cues; the `activate` command exposes that same manual activation.

The toolbar's **Speed** slider selects 0.5×–4× in 0.25× steps, with a live rate
readout. It scales animations, transitions and autoplay dwell time; the default
three-second dwell therefore takes 1.5 seconds at 2×. Changes preserve current
progress and pause state, and the selected speed survives Restart and manual
navigation. Focus the slider and use arrows or Home/End for keyboard control.
The reducer also accepts `{command: 'speed', rate: 1.5, time_ms: ...}` with any
positive finite rate.

Next finishes a running build or starts the next build, then advances slides.
Previous rewinds a build and crosses a slide boundary at its final state.
Manual navigation, Restart, Home/End, jump and seek stop automatic playback.
On a focused player root, arrows, Space, Page Up/Down and
Home/End navigate. Modified keys and embedded controls retain normal behavior.
`reduced_motion: true` snaps each build to its final state and uses cut transitions.

Reducer events use `{command: 'next', time_ms: ...}`, `{command: 'jump', slide: ...}`,
or `{command: 'seek', address: ...}`. Frame times are monotonic milliseconds.
Addresses accept slide/cue index or ID, `'initial'`/`'final'` cues, and `time_ms`.
For example, `snapshot(deck, {slide: 0, cue: 0, time_ms: 250.0})^`.

Formatting a live tree as HTML produces a static projection. Templates and
Lambda handlers are retained by the Lambda host, not serialized into HTML.
Use `snapshot` or `handout` with existing HTML/SVG/PDF/PNG paths where supported;
handout page-break hints are not a promise of new PDF pagination behavior.
Resolve assets relative to the source document or provide `base_uri` explicitly.

Each live player parks when waiting for manual input, paused, or finished.
Autoplay dwell requests frames but keeps the completed cue's visual time fixed.
DOM handles and frame requests
use generation-checked document ownership (D4.5.1v4), and host values use precise
roots (D5.3.3). A failed command, rejected live style value or refused frame request pauses the
affected player and reports a message in its status. Restart retries playback;
an invalid authored value must be corrected before that retry can succeed.
`data-slide-index/cue/phase/paused/autoplay/rate` describe discrete live state; `data-slide-time`
is updated at command/park boundaries. Use the pure diagnostic function for an
exact supplied playback state.

No PPTX import/export, browser animation runtime, video export, editor, arbitrary
shape reveal, or 3D transition catalog is included. Detailed validation and
release measurement results are in the [implementation report](../vibe/impl/Lambda_Impl_Slide_Presentation.md).
