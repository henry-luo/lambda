# Lambda Slide Presentations

`lambda.slide` authors decks as Lambda elements, compiles their cues, and displays
them in Radiant. The package is source Lambda under D7.2.4; rendering uses the
shared host boundary under D7.5.3. Compilation and sampling are pure functions;
live templates perform DOM writes and request frames under S12.1.1v2/S12.1.3.

## Quick start

```lambda
import slide: lambda.slide

let deck = <presentation title: "My deck", width: 1280.0, height: 720.0,
    <slide id: "hello", layout: 'title-body',
        <text id: "title", role: 'title', "Hello, Lambda">
        <text id: "body", role: 'body', "Click Next to reveal this paragraph.">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 400.0>>
        <cue <effect target: "body", kind: 'fly-in', from: 'left', duration: 500.0>>
        <notes "Introduce the package before revealing the paragraph.">
    >
    <slide transition: 'push', <text "The next slide">>
>
slide.page(deck, {instance: "demo", autostart: true})^
```

Save it as a `.ls` script and open it with `./lambda.exe view deck.ls`.
Runnable examples are [slide_presentation.ls](../examples/slide_presentation.ls)
and [slide_package_content.ls](../examples/slide_package_content.ls). The latter
embeds a chart produced once by `lambda.chart.chart` and builds paragraphs.

The standalone page fits the logical canvas into the viewer, with letterboxing,
and refits on window resize. Explicit `width`/`height` options override those
dimensions. Authored object coordinates stay in logical slide units.

## Public API

| Call | Result |
|---|---|
| `compile(deck, options = {})^` | Validated immutable scene/cue plan. |
| `page(deck, options = {})^` | Complete live document, stylesheet and controls. |
| `player(deck, options)^` | Embedded live player; requires a unique `options.instance`. Include `lambda.slide.html.css` in the containing document. |
| `snapshot(deck, address = {}, options = {})^` | Static HTML document at a deterministic cue/time address. |
| `slides(deck, options = {})^` | Array of final-state stage elements; supply the shared stylesheet when embedding. |
| `handout(deck, options = {})^` | Final slides with speaker notes, as ordinary HTML with page-break hints. |
| `paragraphs(id, content_blocks, options = {})^` | Array containing a group of paragraph boxes and one click cue per paragraph. Splice this array into a slide. |
| `initial_state(plan, options = {})` | Initial playback state. |
| `reduce(plan, playback, event)^` | Updated playback state for a command or frame. |
| `sample(plan, playback)^` | Deterministic object visuals for the selected slide/build. |
| `needs_frame(playback)` | Whether an unpaused cue/transition needs another frame. |
| `diagnostic(plan, playback, frame_token = 0)` | IDs, time, phase, generation, active cue tracks and frame token. |

`options` accepts `instance`, `width`, `height`, `base_uri`, `reduced_motion` and
`autostart`. Dimensions are positive finite numbers; flags are booleans. Instance
IDs are nonempty ASCII letters/digits/underscore/hyphen. Embedded players use
their configured dimensions; automatic viewport fitting belongs to `page`.

The compiler rejects unknown attributes, duplicate IDs, missing effect targets,
invalid numeric values, unsupported effects, and overlapping parallel writers
to the same target/channel. Diagnostics name the slide/cue/source path.

## Authoring elements

| Element | Attributes and behavior |
|---|---|
| `presentation` | `id`, `title`, `width`, `height`, `theme: 'light'/'dark'`, `base_uri`. Defaults to 1280 × 720. |
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

Play starts entry cues; Next finishes a running build or starts the next build,
then advances slides. Previous rewinds a build and crosses a slide boundary at
its final state. Pause/Resume, Restart, Home/End, jump and seek are also available
through the reducer. On a focused player root, arrows, Space, Page Up/Down and
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

Each live player parks when waiting or paused. DOM handles and frame requests
use generation-checked document ownership (D4.5.1v4), and host values use precise
roots (D5.3.3). A failed command, rejected live style value or refused frame request pauses the
affected player and reports a message in its status. Restart retries playback;
an invalid authored value must be corrected before that retry can succeed.
`data-slide-index/cue/phase/paused` describe discrete live state; `data-slide-time`
is updated at command/park boundaries. Use the pure diagnostic function for an
exact supplied playback state.

No PPTX import/export, browser animation runtime, video export, editor, arbitrary
shape reveal, or 3D transition catalog is included. Detailed validation and
release measurement results are in the [implementation report](../vibe/impl/Lambda_Impl_Slide_Presentation.md).
