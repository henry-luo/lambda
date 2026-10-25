# Lambda Slide Presentations

`lambda.slide` authors decks as Lambda elements, compiles their cues, and displays
them in Radiant. The package is source Lambda under D7.2.4; rendering uses the
shared host boundary under D7.5.3. Compilation and sampling are pure functions;
live templates perform DOM writes and request frames under S12.1.1v3/S12.1.3.

## Quick start

For interactive authoring, run `./lambda.exe demo slides`. The editor supports
slide and object editing, undo/redo, `.slides` open/save, HTML handout export,
speaker notes and presentation of the current deck. Choose Northstar and the
other examples from **Samples**. See the [editor guide](../test/demo/slides/README.md)
for controls and the current feature scope. It uses the existing package and
DOM boundary under D7.2.4/D7.5.3 and procedural handlers under S12.1.3.

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
| `needs_frame(playback)` | Whether an unpaused cue, transition, autoplay dwell or authored After timer needs another frame. |
| `diagnostic(plan, playback, frame_token = 0)` | IDs, time, phase, generation, active cue tracks and frame token. |
| `timeline(deck, options = {})^` | The deck clock under autoplay: `{duration_ms, slides: [{id, start_ms, end_ms}]}` (see [Deck clock and host control](#deck-clock-and-host-control)). |
| `position(plan, playback)` | The deck time a playback state shows, in milliseconds. |

`options` accepts `instance`, `width`, `height`, `base_uri`, `reduced_motion`,
`autostart`, `controls`, `start_slide` and `ui` (see [Player chrome](#player-chrome)), plus
`autoplay_dwell_ms` (default 3000). Dimensions and the dwell
interval are positive finite numbers; flags are booleans. Instance
IDs are nonempty ASCII letters/digits/underscore/hyphen. Embedded players use
their configured dimensions; automatic viewport fitting belongs to `page`.
`start_slide` is a zero-based integer within the deck (default 0). It initializes
the rendered layer and playback position before activation, including media.

The compiler rejects unknown attributes, duplicate IDs, missing effect targets,
invalid numeric values, unsupported effects, and overlapping parallel writers
to the same target/channel. Diagnostics name the slide/cue/source path.

### Player chrome

The controls, presenter console, overview and page background read their colors,
font and corner radius from `--slide-ui-*` CSS custom properties. Unset tokens
keep the built-in look. A host can set them in its own stylesheet, or pass a
`ui` map in `options`; a `ui` attribute on `<presentation>` is the deck's
default, so a `.slides` file keeps its look under `lambda view`. Option values
win over deck values, token by token.

```lambda
slide.player(deck, {instance: "show", ui: {accent: "#0a84ff", background: "#1b1c1f",
    control: "rgba(255,255,255,0.08)", font: "-apple-system, Arial, sans-serif", radius: "7px"}})^
```

| Token (`ui` key) | Styles |
|---|---|
| `page` | Standalone page background |
| `background`, `border` | Control bar, console and overview surface; their borders |
| `text`, `muted` | Primary and secondary chrome text |
| `control`, `control_border`, `control_hover`, `control_hover_border`, `control_active` | Buttons, slide picker and their states |
| `accent`, `accent_hover`, `accent_active`, `accent_text` | Next button, focus rings, speed value, timer and headings; text on the accent |
| `shadow` | Button `box-shadow` value |
| `font`, `radius` | Chrome font family; button and box corner radius |

Key `control_border` sets `--slide-ui-control-border`. Values are single CSS
values: empty strings and values containing `;`, `{` or `}` are rejected, as are
unknown keys. Slide content keeps using deck themes; `ui` styles only the chrome.

### Deck clock and host control

Autoplay lays a deck out as one clock. Each slide runs its incoming transition,
its entry cues, then a dwell before every click build and before the next slide;
the last slide ends with its final build. `timeline(deck, options)` returns that
layout, with reduced motion snapping builds and skipping transitions, and
`position(plan, playback)` reads a playback state's place on it. The reducer's
`{command: 'seek_time', position_ms: ...}` moves to a deck time and keeps
autoplay, pause and speed as they were; past the end it parks on the final build.

An embedded host can run the player without its own controls and drive it:

```lambda no-run
// no-run: a fragment of a host view; `deck` and `player_node` come from the host
slide.player(deck, {instance: "film", width: 960.0, height: 540.0, controls: false})^

// later, from any handler in the host
dom.dispatch(player_node, {type: "slide_command", bubbles: false,
    detail: {command: "seek_time", position_ms: 12000.0}})
```

- `controls: false` keeps the control bar mounted but hidden, omits its hide
  arrow and the bottom-right reveal corner, and ignores the H key. A click on the stage
  belongs to the host and does not advance the deck.
- A `slide_command` event's `detail` is a reducer event without `time_ms`; the
  event's own time stamp is used. Accepted commands are `activate`, `play`, `pause`,
  `resume`, `next`, `previous`, `restart`, `end`, `jump`, `seek`, `seek_time`,
  `speed`, `stop`, `restart-slide` and `trigger`. `trigger` requires `target`,
  the explicit ID of an object owning a triggered cue.
  `activate` starts entry builds and authored After timing;
  `stop` parks playback, cancels automatic progression and closes its cue sound.
  Hosts should stop an embedded player before removing it.
- The player root publishes `data-slide-duration`, and, with `data-slide-time`
  at each command or build boundary, `data-slide-position` and
  `data-slide-position-at`: the deck time and the frame time it was sampled at.
  While `data-slide-autoplay` is `true` and `data-slide-paused` is not, a host
  extrapolates `position + (now − position-at) × data-slide-rate` on its own
  frame timestamps, so a scrubber needs no per-frame attribute writes.

## Authoring elements

| Element | Attributes and behavior |
|---|---|
| `presentation` | `id`, `title`, `width`, `height`, `theme`, default `master`, `base_uri`; presenter defaults `autostart`, `reduced_motion`, `autoplay_dwell_ms`. Defaults to 1280 × 720. |
| `slide` | `id`, `title`, `background`, `layout`, `theme`, `master`, `transition`, `transition_duration`, `direction`, `transition_variant`, `transition_sound`, `advance_on_click`, `advance_after`. |
| `master` | Reusable slide attributes and objects; requires `id`; optional `extends` selects a parent master. |
| `layout` | Named object bounds; requires `id` and one or more `placeholder` children. |
| `placeholder` | Layout slot with `role`, `x`, `y`, `width`, `height`, in logical slide units. |
| `text` | Rich content, `font_size`, `color`, and common object attributes. |
| `shape` | `kind: 'rect'/'circle'/'ellipse'/'line'`, `fill`, `stroke`, `stroke_width`, `radius`; lowered to SVG. |
| `image` | `src`, `fit: 'contain'/'cover'/'fill'`. |
| `content` | Ordinary HTML/SVG or another package's element output, inside an authored box. |
| `group` | Nested slide objects with local coordinates. |
| `notes` | Rich speaker content excluded from the audience canvas; shown in the presenter console and included in `handout`. |
| `cue` | `id`, `start: 'entry'/'click'/'with-previous'/'after-previous'`, optional `trigger` object ID; contains effects, `parallel`, or `sequence`. |

Common object attributes are `id`, `x`, `y`, `width`, `height`, `rotation`, `scale`,
`opacity`, `role`, `class`, `style`, `morph_id`, and `metadata`. Optional
`metadata` is a map of application annotations, retained in `source` and ignored
by placement and paint (S1.4, D7.5.3). Width/height/scale are positive;
opacity is in [0,1]. Built-in layouts are `'blank'`, `'title'`, `'title-body'`,
`'two-column'`, `'three-column'`, `'section'`, `'quote'`, `'image-left'`, and
`'image-right'`; roles are `'title'`, `'body'`, `'left'`, `'center'`, `'right'`,
and `'footer'`. In image layouts, unassigned images occupy the image side and
`'body'` uses the opposite side. Named layouts can define additional roles. Explicit
bounds override layout defaults. Flow content uses normal Radiant layout inside
its box. A `content` object containing a single SVG root fits that SVG's viewport
to the object box by default. Supply a `viewBox` to scale its geometry; authored
`preserveAspectRatio` still controls fitting. SVGs nested in ordinary HTML keep
their own layout sizing. This is a rendering projection; the authored SVG subtree
is preserved (S1.4). CSS/SVG feature support remains Radiant's support matrix.

### Themes, layouts and masters

Themes are `'light'`, `'dark'`, `'corporate'`, `'midnight'`, and `'paper'`.
A theme may also be a map: `{base: 'midnight', accent: "#ffaa00",
font_family: "Arial, sans-serif", title_size: 48, body_size: 28}`.
Optional `background` and `foreground` override the palette. Sizes are positive
finite numbers; colors and font families are nonempty CSS strings. The deck
theme supplies defaults; a slide or master can select its own theme. Explicit
object paint and font sizes, and slide backgrounds, override theme defaults.

Define named layouts and masters directly inside `<presentation>`:

```mark
<presentation theme: 'midnight', master: "brand",
    <layout id: "briefing",
        <placeholder role: 'title', x: 80, y: 70, width: 1120, height: 110>
        <placeholder role: 'body', x: 80, y: 230, width: 1120, height: 350>
    >
    <master id: "brand", layout: "briefing",
        <text id: "title", role: 'title', "Default title">
        <text id: "footer", role: 'footer', font_size: 18, "Example Corp">
    >
    <slide id: "opening",
        <text id: "title", role: 'title', "Welcome">
        <text id: "body", role: 'body', "Shared layout and footer">
    >
    <slide master: false, <text "A slide without the master">>
>
```

A master contains slide objects, including groups and boxed content. It can
inherit another master with `extends: "base"`. A slide chooses `master: "brand"`,
uses the deck default, or opts out with `master: false`. A local top-level object
with the same explicit ID replaces the inherited object in place; other objects
append. Local slide attributes override inherited attributes. Cues on the slide
can target inherited objects. Master cycles, duplicate definitions, invalid
objects and unknown references fail compilation, including unused definitions.
Unspecified built-in roles retain their title/body/footer bounds. Named layouts
cannot shadow built-in names. Composition runs before scene/cue
compilation within the source package (D7.2.4).

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

Effects accept an optional scene-unique `id`, `target`, `kind`, `duration` in milliseconds, `delay`, `easing`, and
positive integer `repeat`. Supported easing names are `'linear'`, `'ease'`,
`'ease-in'`, `'ease-out'`, `'ease-in-out'`, `'ease-in-cubic'`, `'ease-out-cubic'`,
and `'ease-in-out-cubic'`; four-number cubic Bézier arrays are also accepted.

| Kind | Additional attributes |
|---|---|
| `appear`, `disappear`, `fade-in`, `fade-out` | None. |
| `fly-in`, `fly-out` | `from: 'left'/'right'/'top'/'bottom'`, `distance`. |
| `zoom-in`, `zoom-out`, `pulse`, `grow` | `scale`; `grow` retains its final scale, while `pulse` returns. |
| `spin` | `angle`. |
| `wipe-in`, `wipe-out` | `from`; rectangular CSS inset clipping. The revealed area grows toward the `from` edge: `from: 'right'` draws left to right, `from: 'top'` rises from the bottom. |
| `motion` | `path`, optional `orient: 'auto'`. |
| `highlight` | `color`; preceding highlights supply the next starting color. |
| `line-color` | `color`; independently animates a native shape's visible outline. |
| `transparency` | `opacity` in [0,1]; retains its final opacity. |
| `blinds-in`, `checkerboard-in`, `dissolve-in` | Deterministic SVG path masks; the first two accept `from`. |
| `keyframes` | `channel`, `stops`, optional `from` for clip direction. |

Parallel duration is the longest child span; sequence duration is their sum.
Entry cues must precede click cues. Entrances establish their initial hidden
state before the first build. Completed tracks retain their final values.
Hidden effect wrappers use visibility, pointer exclusion, `aria-hidden` and
`inert`; each player's controls remain outside the animated canvas. Shown
objects inherit visibility and pointer events, so a hidden group hides the
objects nested in it.

`with-previous` and `after-previous` merge into the preceding navigable build.
They start at the immediately preceding cue's beginning or end, respectively,
and require an earlier ordinary cue. The merged build keeps source cue IDs as
address aliases. Channel conflicts are validated after scheduling; color
continuity follows chronological track order. Entry builds still precede click
builds. Authoring remains a pure source transformation (S1.4/S12.1.1v3).

A cue with `trigger: "object-id"` and `start: 'click'` runs outside the main
navigation timeline. Clicking that object's descendants also fires it, including
in an embedded player with hidden controls or a slide with clicking disabled.
The host may dispatch `{command: "trigger", target: "object-id"}`. Independently
fired cues share pause and playback speed; firing again restarts the matching
cues. Fired tracks overlay ordinary visuals in firing order, retaining their
completed values. Unfired entrances remain hidden. Trigger playback cancels Play
and defers authored After timing until active builds finish. Navigation to a
different slide, Restart, Previous and seeking discard trigger activations;
outgoing transitions preserve the fired visuals. Main-timeline snapshots and
handouts do not fire triggers. Fill/line color effects apply to native shapes
and text as appropriate; embedded HTML/SVG remains an opaque content object.

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

Slide transitions are `'none'`, `'cut'`, `'fade'`, `'push'`, `'wipe'`, `'split'`,
`'reveal'`, `'random-bars'`, `'shape'`, `'uncover'`, `'cover'` and `'morph'`.
The original `'slide'` spelling remains an alias for cover behavior.
`direction` is `'left'`, `'right'`, `'top'` or `'bottom'`, describing travel;
for example `'left'` brings incoming content from the right. Default duration is
300 ms. None and normal Cut switch immediately. Fade overlays the outgoing layer;
Push moves both layers, Cover moves the incoming layer and Uncover the outgoing
layer. Wipe clips the incoming layer; Reveal clips and moves the outgoing layer.
Random Bars uses a deterministic staggered strip clip. These are package samples
of supported CSS geometry under S12.1.1v3/D7.5.3, rather than a separate animation
engine. Pointer/focus input on both layers is excluded during a transition.

`transition_variant` selects `'normal'`/`'through-black'` for Fade and Cut,
`'out'`/`'in'` for Split, and `'circle'`/`'diamond'`/`'box'` for Shape. Other
transitions accept `'normal'`. Split and Bars use left/right for horizontal
geometry and top/bottom for vertical geometry. Through-black Cut holds a black
interval between its two abrupt switches. Zero-duration and reduced-motion
playback snap to the destination.

`advance_on_click: false` disables ordinary stage-click advancement. Explicit
buttons, navigation links and keyboard commands still work. `advance_after`
is an optional nonnegative number of milliseconds: its timer begins when the
slide's final build completes, and works without Play. Zero advances on the next
frame. Omit it or use `null` to require manual navigation. During Play, it replaces
the default dwell before the next slide; click builds keep `autoplay_dwell_ms`.
Pause/Resume and speed changes retain timer progress. The final slide stays parked.
In a standalone page, an initial After timer or sound cue requests entry activation
on load, even when `autostart` is omitted. Embedded hosts send `activate`.

`transition_sound: {name: "Chime", src: "cue.wav"}` plays a single cue on entry.
Sources may be local/remote URLs or base64 data URIs; relative sources use
`base_uri`. Acquisition stays procedural, and the existing native audio interface
owns playback under S12.1.3/D7.5.3/D7.5.4. Pause pauses the cue; leaving, stopping or
closing the player releases it. Audio errors appear in the status and
`data-slide-sound-error` without stopping the visual timeline. `data-slide-sound`
reports `playing`, `paused`, `silent` or `error`. Static output does not play sounds.

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

The player toolbar is a single row of icon controls with tooltips and accessible
names, including Previous/Next, Play/Pause/Resume, Restart and presenter tools.
It reuses the packaged icon catalog (D7.2.5). Narrow windows scroll the items
horizontally; the hide arrow remains visible at the right edge. Full-screen
changes update the icon and accessible action name without replacing the button.

The toolbar's **Speed** slider selects 0.5×–4× in 0.25× steps, with a live rate
readout. It scales animations, transitions, autoplay dwell and authored After timers; the default
three-second dwell therefore takes 1.5 seconds at 2×. Changes preserve current
progress and pause state, and the selected speed survives Restart and manual
navigation. Focus the slider and use arrows or Home/End for keyboard control.
The reducer also accepts `{command: 'speed', rate: 1.5, time_ms: ...}` with any
positive finite rate.

Next finishes a running build or starts the next build, then advances slides.
Previous rewinds a build and crosses a slide boundary at its final state.
Manual navigation, Restart, Home/End, jump and seek stop automatic playback.
When the player root or any of its buttons has focus (a clicked control keeps
focus), arrows, Page Up/Down and Home/End navigate; Space navigates from the root
and activates a focused button. Modified keys, the slide picker, the speed slider
and other embedded inputs retain normal behavior.
`reduced_motion: true` snaps each build to its final state and uses cut transitions.

The **Presenter** button opens a docked console in the same viewer, containing
the current slide's rich notes, a next-slide preview at its final build, and an
elapsed timer. Narrow players stack the console below the audience canvas.
The timer starts on first opening; Pause/Start and Reset control it separately
from animation playback and speed. Closing the console keeps elapsed time
running but parks its frame requests until reopened. At the last slide the
preview reads “End of presentation”. The console is visible in the viewer;
this is not a separate speaker window for a second display.

**Overview** shows numbered slide titles; selecting one jumps to that slide and
closes the overview. The slide field and **Go** icon accept a one-based slide
number or an exact slide ID; Enter submits. Invalid input reports an error without navigation.
**Blackout** covers the audience canvas in black. **Pointer** enables a red laser
that follows the mouse over the canvas. **Full screen** (or F) toggles the
viewer window's full screen; its tooltip reads **Exit full screen** while active. Entering
full screen (by button, F, F11 or `--fullscreen`) hides the toolbar and leaving it
shows the toolbar again. The button shows
only when the player's window can go full screen, not in an embed or a headless
run. The **⌄** arrow at the control bar's
bottom-right hides the toolbar; moving the mouse into the player's bottom-right
corner, or pressing H, brings it back. The arrow sits inside that corner, so after a
click the pointer must leave the corner before re-entering reopens the toolbar. Tool shortcuts work within the player; text inputs retain
ordinary typing.

| Key | Tool |
|---|---|
| P | Toggle presenter console |
| O | Toggle overview |
| H | Hide/show toolbar |
| B or . | Toggle blackout |
| L | Toggle laser pointer |
| F | Toggle full screen |
| Escape | Close overview and clear blackout/pointer; with none active, the viewer's Escape applies (leave full screen, or close the window) |

Overview uses titles without rendering every slide. Next-slide previews are
rendered lazily into a separate cache with instance-specific IDs; revisiting a
preview reuses its markup, and audience layers retain their existing cache
(SLD3/SLD4). Preview content is inert. Presenter state and event handling stay
in Lambda under D7.5.3 and S12.1.3.

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
`data-slide-index/cue/phase/paused/autoplay/timed/rate` describe discrete live state; `data-slide-time`
is updated at command/park boundaries. Use the pure diagnostic function for an
exact supplied playback state.

The slide editor is available through `lambda demo slides`. PPTX interchange,
video export, arbitrary masks/path deformation and 3D transitions remain outside
the delivered catalog. Detailed validation and
release measurement results are in the [implementation report](../vibe/impl/Lambda_Impl_Slide_Presentation.md).
