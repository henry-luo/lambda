# Lambda Timeline Chart Package — Proposal

> **Status:** partially implemented; scope revised 2026-10-12. The user selected the package namespace,
> comprehensive feature scope, and absorption of dtna Timeline; the schema and
> detailed contracts below are proposals, not claims of implemented support.
> **Package:** `lambda.chart.timeline`.
> **Scope:** historical and event timelines, from a short sequence of milestones
> to a zoomable chronology, multimedia narrative, or curved timeline chart.
> **Relationship:** extends the [chart design](Lambda_Pkg_Chart.md); replaces the
> independent timeline implementation in [dtna](Lambda_Pkg_UI.md).

| Subject | Formal linkage | Consequence |
|---|---|---|
| Shipped source package and import path | **D7.2.1–D7.2.4, D7.2.6** | Use ordinary modules under `lmd/package/chart/`; no resolver exception or native timeline widget. |
| Logical elements and presentation | **S2.4.3v3, S12.1.3** | Qualified timeline elements are ordinary data, presented by imported views. |
| Pure rendering and procedural effects | **S12.1.1v2–S12.1.3** | Rendering consumes explicit inputs; handlers own input and playback effects. |
| State, captures, and value semantics | **S9.1.4, S9.1.7, S9.2.4v2, S9.3.1; D6.2.3v2** | State belongs to each live source/template instance, with immutable specifications and no mutable package singleton. |
| Ordering and errors | **S6.2.2v3–S6.2.3, S7.4.1–S7.4.2** | Preserve exact comparisons and stable ties; diagnose invalid specifications through the documented error channel. |
| Native ownership and host boundary | **D4.5.1v4, D5.3.3, D7.5.3** | Reuse general Radiant mechanisms through the established boundary; precisely root retained values. |
| Execution tiers | **D8.1.1v17** | Package behavior must agree in T0, AUTO, and MIR Direct execution. |

The formal specifications remain authoritative. This proposal introduces package
contracts within those rulings; it does not ratify a new language, identity,
calendar or host-capability contract.

## 1. Product contract

Provide one timeline package that combines the timeline capabilities of
**React Chrono, TimelineJS, vis-timeline, and amCharts 5 Timeline**, expressed as
Lambda elements and functions. The target is their combined user-visible timeline
feature set within the scope exclusions below, including its configuration controls. JavaScript object
models, React hooks, framework packaging, and vendor-specific method spellings
receive Lambda equivalents rather than becoming new language requirements.

The primary application is history: dates, events, people, eras, illustrations,
and explanatory stories. An event can also have a duration, but task dependencies,
critical-path computation, resource scheduling, and a Gantt-specific authoring
model are outside this proposal. Duration events remain first-class timeline data.

The package supports three presentations over the same event model:

1. **Cards:** vertical, horizontal, alternating, or all visible; suitable for
   company histories, biographies, release histories, and activity sequences.
2. **Axis:** events positioned on a navigable time scale, with groups, lanes,
   eras, clustering, and optional quantitative series.
3. **Story:** an active multimedia event with synchronized timeline navigation,
   sequential controls, bookmarks, and slideshow playback.

Presentation, spacing, and path shape are independent choices. A sequence of
equally spaced cards must never imply that equal distances represent equal
elapsed time. A time-scaled timeline keeps its date mapping while labels and
cards move to avoid collisions.

The live host is **native Radiant**. On 2026-10-12 the user removed interactive
browser delivery, automatic resource lifecycle management, export, and
accessibility/localization requirements from this proposal. Existing helpers and
compatibility fields for these facilities may remain; they do not gate acceptance.
Application procedures may supply already loaded data and media. Playback and
immutable source replacement remain in scope (S12.1.1v2/S12.1.3, S9.1.4/S9.3.1).

The active implementation tranche is scalability and keyed animation. Other
retained feature families keep their existing open acceptance records.

## 2. Proposed element schema

Existing `locale`, `messages`, resource-loading, and `export` fields/APIs are
optional compatibility facilities, outside this proposal's acceptance requirements.

### 2.1 Authoring example

The public root is **`<timeline.chart>`**. Importing `lambda.chart.timeline`
installs its view templates, following **S2.4.3v3/S12.1.3**. The spelling avoids
the chart grammar's existing, contextual `<timeline><keyframe ...>` playback
declaration. History events and animation keyframes have different vocabularies.

```lambda no-run
// no-run: proposed timeline elements and views are not implemented yet.
import timeline: lambda.chart.timeline

let history = <timeline.chart id: "project-history", width: 960,
    view: 'cards', spacing: 'sequence', orientation: 'vertical',
    placement: 'alternate', order: 'chronological', label: "Project history",
    <timeline.title "Every milestone has a story">
    <timeline.event id: "idea", at: t'2019-03-01', title: "An idea takes shape",
        color: "#1677ff",
        <timeline.content "A small team sketches a simpler way to work.">
    >
    <timeline.event id: "prototype", at: t'2020-06-01', title: "First prototype",
        <timeline.content "Early users help shape the direction.">
    >
    <timeline.event id: "launch", at: t'2022-09-01', title: "Public release",
        color: "#52c41a",
        <timeline.content "The first release is ready for everyone.">
        <timeline.media kind: 'image', src: "launch.jpg",
            alt: "The first release announcement", caption: "Launch day">
    >
    <timeline.pending "The story continues...">
>

apply(history)
```

The same events can be presented on a date scale:

```lambda no-run
// no-run: proposed timeline elements and views are not implemented yet.
import timeline: lambda.chart.timeline

apply(<timeline.chart id: "science", width: 1000, height: 540,
    view: 'axis', spacing: 'time', orientation: 'horizontal',
    order: 'chronological', label: "A sample science chronology",
    <timeline.scale kind: 'calendar', timezone: "UTC">
    <timeline.axis position: 'both', major: true, minor: true>
    <timeline.window from: t'1900-01-01', to: t'2000-01-01'>
    <timeline.navigator visible: true>
    <timeline.interaction pan: true, zoom: true, keyboard: true>
    <timeline.group id: "discovery", title: "Discoveries">
    <timeline.event id: "a", at: t'1910-01-01', group: "discovery",
        title: "First discovery">
    <timeline.event id: "b", at: t'1955-01-01', group: "discovery",
        title: "Second discovery">
    <timeline.era id: "early", from: t'1900-01-01', to: t'1940-01-01',
        title: "Early period", color: "#e6f4ff">
>)
```

These are fictional examples. Proposed element names use existing Lambda syntax;
the proposal needs no lexer, parser, or grammar extension.

### 2.2 Root attributes and defaults

| Attribute | Proposed contract |
|---|---|
| `id` | Nonempty authored chart name; required for linked views and bookmarks. It namespaces event addresses, not runtime template ownership. |
| `version` | Schema version, initially `1`. Unknown versions are errors. |
| `view` | `'cards'`, `'axis'`, or `'story'`; default `'cards'`. |
| `spacing` | `'sequence'` or `'time'`; default `'sequence'` for cards and `'time'` for axis/story. |
| `orientation` | `'vertical'` or `'horizontal'`; default vertical for cards, horizontal for axis/story. |
| `placement` | `'start'`, `'end'`, `'alternate'`, or `'auto'`; default `'alternate'` for cards and `'auto'` otherwise. Start/end are relative to writing direction and path normal. |
| `order` | `'source'`, `'chronological'`, or `'reverse_chronological'`; default `'source'` for sequence spacing and `'chronological'` for time spacing. Stable ties preserve source order. |
| `width`, `height` | Positive finite CSS-pixel numbers or `'auto'`; default auto. `viewport` supplies resolved dimensions. Headless export requires a resolved finite box. |
| `min_width`, `max_width`, `min_height`, `max_height` | Optional finite layout constraints. Contradictory constraints are errors. |
| `padding`, `gap` | Nonnegative pixels; padding also accepts the chart package's four-side map. |
| `time_scale` | Primary chronology scale ID; default `"time"`. |
| `value`, `default_value` | Controlled or initial interaction-state projection (§4). Supplying both is an error; absence means instance-owned state. |
| `theme`, `parts` | Immutable visual tokens and semantic-part recipes (§2.10). The core has a neutral default skin. |
| `locale`, `direction` | Locale identifier and `'ltr'`/`'rtl'`; default locale `"en-US"`. An omitted direction follows the language's writing direction; an explicit direction overrides it. Both remain independent of machine settings. |
| `motion` | `'full'`, `'reduced'`, or `'none'`; host preference can reduce, never increase, authored motion. |
| `label`, `description` | Accessible chart name and description; a visible title may provide the default name. |
| `class`, `style`, `aria-*`, `data-*` | Ordinary presentation attributes; apply at the root unless a semantic part is explicitly targeted. |

Axis presentation requires time spacing. Cards support either spacing. Story
presentation may use sequence spacing for an intentionally undated narrative;
its navigator then represents event order and is labelled accordingly. Curved
coordinates require time spacing and an axis or story navigator. Unsupported
combinations are diagnosed, never silently coerced into another layout.

### 2.3 Child vocabulary and cardinality

Every schema-owned attribute is validated. Unknown qualified children, duplicate
singleton children, invalid references, and incompatible declarations are errors.
Arbitrary HTML/SVG belongs inside a declared content slot, not beside configuration
children. Configuration children can be provided as equivalent map properties.

| Child | Count | Purpose |
|---|---|---|
| `<timeline.title>`, `<timeline.description>` | Each 0–1 | Chart heading and introductory content. |
| `<timeline.event>` | 0–many | Point, duration, or background event. |
| `<timeline.era>` | 0–many | Labelled historical period, with independent overlapping bands. |
| `<timeline.group>` | 0–many | Named lanes, subgroups, and nested group hierarchy. |
| `<timeline.data>` | 0–1 | Record array or named immutable dataset, before encoding. |
| `<timeline.encoding>` | 0–1 | Maps record fields to the canonical event model. |
| `<timeline.transform>` | 0–1 | Existing chart transform grammar over supplied records. |
| `<timeline.source>` | 0–1 | Declarative resource request, resolved outside pure rendering. |
| `<timeline.scale>` | 0–many | Named chronology or quantitative scales. |
| `<timeline.axis>` | 0–many | Major/minor date labels and optional quantitative axes. |
| `<timeline.window>` | 0–1 | Initial visible domain, bounds, and zoom constraints. |
| `<timeline.coordinate>` | 0–1 | Straight, serpentine, spiral, or custom curve. |
| `<timeline.navigator>` | 0–1 | Overview, thumbnails, labels, and draggable viewport window. |
| `<timeline.cards>` | 0–1 | Card sizing, visibility, text layout, expansion, and responsive modes. |
| `<timeline.story>` | 0–1 | Title slide, active-event stage, bookmarks, and navigation order. |
| `<timeline.markers>` | 0–1 | Marker/connector defaults and label placement. |
| `<timeline.interaction>` | 0–1 | Navigation, selection, gestures, cursors, and linked parameters. |
| `<timeline.edit>` | 0–1 | Event/group creation, moving, resizing, deletion, and validation requests. |
| `<timeline.cluster>` | 0–1 | Event aggregation and expansion policy. |
| `<timeline.search>` | 0–1 | Search fields, matching, highlighting, result navigation, and filters. |
| `<timeline.slideshow>` | 0–1 | Story playback, timing, transitions, repeat, and progress. |
| `<timeline.media_options>` | 0–1 | Media sizing, loading, playback, providers, and fallback policy. |
| `<timeline.toolbar>` | 0–1 | Controls, quick jump, layout/theme switching, fullscreen, and export. |
| `<timeline.pending>` | 0–1 | Pending/loading continuation, independent of dated events. |
| `<timeline.empty>`, `<timeline.loading>`, `<timeline.error>` | Each 0–1 | Application-supplied status presentations. |
| `<timeline.annotation>` | 0–many | Notes, rules, current/custom times, and reference bands. |
| `<timeline.series>` | 0–many | Additional chart series sharing the timeline coordinate. |
| `<timeline.messages>` | 0–1 | Locale strings and pure formatting functions. |
| `<timeline.export>` | 0–1 | Snapshot, resource, expansion, pagination, and unsupported-content policy. |

Direct events and encoded records may coexist; the direct events precede encoded
records in source order, and IDs must be unique across both. Data and source are
mutually exclusive ways to supply the encoded record collection. Group children
may contain group definitions, but events refer to groups by ID so each event
has one canonical owner. Nested timelines are event content, not implicit group
definitions.

### 2.4 Event, era, and group records

An event has the same canonical fields whether authored as an element, decoded
from a map, or generated by an adapter.

String/symbol IDs normalize to nonempty text before references are resolved;
duplicate IDs after normalization are errors. Nested charts provide an explicit
namespace boundary, so an event address is a chart path plus an event ID.

| Event attribute | Meaning |
|---|---|
| `id` | Stable nonempty string or symbol, unique within the chart. Required for interaction, nesting, dynamic updates, and animation. Static-only inputs may use deterministic positional IDs, explicitly marked unstable under reordering. |
| `kind` | `'point'`, `'range'`, or `'background'`; infer point from `at`, range from `from`/`to`. Background events require a range. |
| `at` | Point date. Mutually exclusive with `from`/`to`. Optional only in sequence spacing. |
| `from`, `to` | Range endpoints. `to >= from`; equal endpoints render as a point. Open ends require `open_start`/`open_end`, never an implicit current date. |
| `title`, `subtitle`, `display_date` | Headline, subordinate heading, and an explicit date label. Custom display text never changes date positioning. |
| `group`, `subgroup`, `tags` | One lane, optional subgroup, and independent filter/category tags. Missing referenced groups are errors. |
| `placement`, `align`, `priority` | Side/alignment override and collision priority; priority does not change chronology. |
| `color`, `marker`, `connector`, `appearance` | Visual overrides, including shape, size, icon/thumbnail, line, fill, opacity, border, and state styles. |
| `link`, `target` | Optional title/card navigation destination and target policy. Activation uses the host's ordinary navigation behavior. |
| `visible`, `selectable`, `editable` | Explicit visibility, selection, and per-event edit permissions. |
| `uncertainty` | `{earliest, latest, label}` around a point, or separate start/end uncertainty around a duration. Bounds must enclose the authored date. |
| `metadata` | Application data preserved through selection, search, edits, clustering, and export. |
| `class`, `style`, `aria-*`, `data-*` | Attributes for the event's semantic presentation part. |

Event slots are `<timeline.content>` for rich body content,
`<timeline.media>` for one or more media records, `<timeline.marker>` for an
arbitrary marker element, `<timeline.tooltip>` for tooltip content, and
`<timeline.children>` containing a nested `<timeline.chart>`. Heading, date,
card, marker, and tooltip can alternatively use pure render functions receiving
`(event, context)` and returning Lambda content. Templates cannot perform I/O
or mutate state (**S12.1.1v2**).

`<timeline.era id, from, to, title, group?, color?, placement?, appearance?>`
labels a period; unlike an event, it does not become a story slide unless
explicitly requested. Overlapping eras occupy separate bands or use the selected
overlap policy; labels are measured independently. An era may be open-ended only
with an explicit open flag and visible-window clipping.

`<timeline.group id, title, parent?, order?, visible?, collapsed?, editable?,
height?, min_height?, max_height?, appearance?>` declares a lane. IDs and parent
references form an acyclic forest. Group titles accept rich content or a pure
renderer. Subgroups have their own order, visibility, stacking, and height policy.
Lane height supports `'auto'`, `'fixed'`, and `'fit_items'`; group reordering emits
an application change request rather than mutating the supplied dataset.

Nested charts have their own ID scope and state. Their dates are not clamped to
the parent event's interval unless the author asks for that constraint. Depth,
total events, media resources, and layout work have explicit configurable limits;
exceeding a limit produces a diagnostic instead of truncating the source silently.

### 2.5 Dates, precision, and time scales

`DateSpec` accepts a Lambda date/datetime, a documented ISO date string, or a
package-owned record. A bare numeric value is accepted only with an explicit
numeric/cosmological scale and unit. Milliseconds, seconds, and years are never
guessed from magnitude. Time-only values require an explicit reference date.

Calendar records use `{year, month?, day?, hour?, minute?, second?,
millisecond?, timezone?, precision?, display_date?}`. Canonical signed years use
**astronomical numbering**: year `0` means 1 BCE; year `-43` means 44 BCE.
Adapters convert human BCE/CE numbering explicitly. This is a package record
convention, not a change to Lambda's datetime type or literal interpretation.
Calendar years are integral; fractional years belong to an explicitly selected
cosmological scale. For example, a TimelineJS year `-44` (44 BCE) normalizes to
astronomical year `-43`, preserving the human era label.

Missing components retain their declared precision. A year-only event is sorted
and positioned at the beginning of that year, labelled as a year, and is not
automatically turned into a year-long duration. Authors use an explicit range or
uncertainty bounds when that is what the data means. Calendar validity and
leap-year rules are checked before ordering.

| Scale property | Proposed contract |
|---|---|
| `id`, `kind` | Default primary ID `"time"`; chronology kinds `'calendar'`, `'cosmological'`, `'numeric'`, and `'ordinal'`. Quantitative overlay scales reuse existing chart scale kinds. |
| `domain`, `unit`, `origin` | Optional bounds; numeric time requires an explicit unit. Cosmological time is measured in exact years relative to an explicit origin, default astronomical year zero. |
| `timezone` | Default `"UTC"`; support fixed offsets and named IANA zones through the chart timezone module. No implicit host timezone. |
| `calendar`, `week_start` | Proleptic Gregorian initially, with configurable week start. Alternate calendars require an explicit adapter; never relabel Gregorian dates as another calendar. |
| `numbering`, `era_labels` | Display conversion for astronomical versus human BCE/CE years; locale supplies era labels. Adapters convert human input into canonical astronomical records. |
| `ambiguous_time`, `nonexistent_time` | Explicit fold/gap policies; default reject ambiguous or nonexistent local timestamps unless the input carries an offset. |
| `hidden_intervals` | Excluded/compressed spans, optionally recurring by day/week/month/year. Preserve a reversible mapping outside those spans and disclose events inside a hidden interval. |
| `interval_limit` | Positive integral bound on expanded interval occurrences per visible window; default 10,000. Exceeding it returns a diagnostic. |
| `format`, `formatter` | Existing chart date formatting or a pure formatter. Formatting never participates in comparisons. |
| `locale` | Optional scale-specific locale overriding the root locale for labels; chronology and timezone stay unchanged. |

Deep time uses exact integer/decimal year coordinates, without forcing them into
the native datetime range or JavaScript Date limits. Compare exact values first,
subtract an explicit local origin, and only then convert bounded screen distances
to float. Calendar conversion outside supported ranges returns a diagnostic;
cosmological ticks use year units rather than fabricated month/day values.
Floating-point zoom must not reorder near-coincident events at very large dates.

Each `hidden_intervals` entry is `{from, to, repeat?, compression?}`. Endpoints
use the primary chronology's date schema, with `from < to`. `compression` is a
ratio in `[0, 1]`, default `0`: zero excludes elapsed distance, one preserves it.
Overlaps use the smallest active ratio, rather than subtracting a span twice.
`repeat` is `'day'`, `'week'`, `'month'`, or `'year'` and requires a calendar
scale. Recurrence retains the authored local clock and timezone; monthly/yearly
recurrence clamps an unavailable day to that month's final day. Fold/gap policies
remain explicit. A fully hidden visible window is an error. Picking at an
excluded seam accepts an explicit forward/backward bias, default forward.

`<timeline.axis>` selects a named scale and supports `position` (start/end/both/
none), `major`, `minor`, `grid`, `week_numbers`, `tick_unit`, `tick_step`,
`tick_count`, label formats/renderers, rotation, title, label bounds, and measured
collision handling. Multiple axes can expose the same date scale at different
levels or quantitative scales used by an overlay series. In sequence spacing,
optional labels describe events/order; they are not elapsed-time ticks.

### 2.6 Coordinates, window, and layout

`<timeline.coordinate kind: ...>` supports `'straight'` (default),
`'serpentine'`, `'spiral'`, and `'curve'`.

| Coordinate | Additional properties |
|---|---|
| Straight | Orientation, lane direction, optional reversal of visual direction. |
| Serpentine | `levels`, `turn_radius`, `start_location`, `end_location`, and horizontal/vertical progression. |
| Spiral | `turns`, `start_angle`, `end_angle`, `inner_radius`, `outer_radius`, and lane width. |
| Curve | `points` or `path`, smoothing, sampling precision, optional pure point function, and lane offsets. Paths may be imported from an SVG path string. |

Time maps to distance along a curve, not to control-point index or uncorrected
Bezier parameter. Labels/markers may follow the tangent, remain upright, or use a
fixed angle. Lane widths and self-intersection policies are explicit. Projection,
inverse mapping, drag snapping, selection, clipping, and picking share the same
geometry; equally timed events cannot drift between the rendered and picked path.

`<timeline.window>` supports `from`, `to`, `min`, `max`, `min_span`, `max_span`,
`fit`, `anchor`, and `follow`. Bounds use the primary scale's units. `fit` selects
all data, a selection, or a named group; `follow` keeps an explicitly supplied
current-time cursor in view. Current time comes from state or handler input,
never from a wall-clock read during rendering.

`<timeline.cards>` defines `visibility: 'all'|'active'|'viewport'`, width/height,
min/max sizes, date/title/body alignment, borderless mode, text overlay, semantic
heading levels, compact/detailed variants, read-more expansion, marker visibility,
scrollbars, and responsive breakpoints. Horizontal-all is horizontal orientation
with visibility all; it does not require a fourth unrelated rendering engine.
Responsive changes preserve active event, selected IDs, and expanded content.

Stacking uses measured bounds with explicit gaps. Optional labels can be omitted
with an accessible overflow summary, but event identity and the event's exact
time position are retained. Equal-time events use stable source order. Overflow
can stack, cluster, scroll, or expand; it must not silently drop records.

### 2.7 Interaction, controls, and editing

| Child | Main options |
|---|---|
| `interaction` | `enabled`, `click_to_use`, `pan`, `zoom`, wheel modifier/preference, pinch, horizontal/vertical scrolling, keyboard navigation, point/card activation, hover feedback, focus-on-load, auto-scroll, selectable/multiselect, contiguous/per-group selection, linked chart parameters. |
| `navigator` | Visibility, position, height, date/ordinal axis, labels/thumbnails, overview grouping, viewport handles, and independent overview styling. |
| `search` | Fields, query, case/diacritic policy, highlight versus filter, next/previous match, count/no-results text, tag/group/date filters, and pure match predicate. |
| `cluster` | Enabled, minimum overlapping count, screen-distance threshold, grouping criteria, title/renderer, count badge, max expansion, and click/double-click expand-to-fit. |
| `edit` | Add/update/remove, move-time, move-group, range resize, subgroup/group edits, drag-and-drop, per-item permissions, snap unit/step/function, validation and confirmation requests. Default disabled. |
| `toolbar` | Ordered buttons for first/previous/next/last, fit/zoom/reset, search, quick-jump, layout, compact/details, theme, fullscreen, play/pause, export, and keyboard help; top/bottom placement, sticky mode, custom slots and localized labels. |
| `story` | Title slide, initial active ID, start-at-end, panel order, explicit event order, hash bookmarks, active media behavior, and narrative metadata. |
| `slideshow` | Enabled/autoplay, per-event duration, progress/overall progress, fade/slide/reveal transition, delay, easing, repeat, direction, pause/resume/seek/stop, and media-end synchronization. |

`story.title_slide` is false/absent, true (chart title/description), or a canonical
event record with rich content and media. Its default ID is `"__title__"`, distinct
from all event IDs; an explicit ID resolves an authored name collision.
Introductions join narrative navigation/playback but have no chronology position
and are excluded from exported event records. `story.order` lists unique known
event IDs after the introduction. `story.panel_order` orders the `"text"`,
`"media"`, and `"children"` sections. Initialization, manual navigation,
quick jump, keyboard movement, and playback consume this same sequence.

`story.bookmarks: true` requires authored chart and event IDs. The address is
`#timeline=<form-encoded-chart-id>/<form-encoded-event-id>`, including introduction
IDs. `{type:"bookmark", fragment:...}` restores the addressed activation through
the ordinary controlled-state reducer. Unrelated chart fragments are ignored;
an owned address with an unknown event reports a value error. Native window
subscriptions restore hash changes and back/forward traversal without polling.

Clicking a point can activate its event without making selection and active-slide
state synonymous. Pan/zoom never edits dates. Editing is an explicit mode with
independent permissions, visible previews, cancellation, and application commits.
Proposed edit requests carry original values, proposed values, event IDs, and a
request token. A pending asynchronous validation result is applied only if the
token and source revision still match; errors restore the committed presentation.

`{type:"source_update",spec:next_spec,expected_revision:old_revision}` replaces
the immutable source through both the pure reducer API and the retained native
model (S9.1.4/S9.3.1). An accepted pure result includes `source:next_spec` for
subsequent calls. Stale expected revisions are ignored before validating the
replacement. Activation retains its ID or chooses the nearest surviving event
in the previous chronology, preferring the following event on ties. Selection
prunes removed IDs with normal notifications. Focus follows a removed focused
event without taking focus from external controls. Playing/paused stories retain
the current slide fraction and repeat cycle when source order/durations change.

Controls and external calls use the same reducer. Pointer, keyboard, touch,
programmatic commands, and navigator gestures must not maintain separate versions
of the visible window or selected events. Pointer capture and default text/control
editing reuse generic DOM mechanisms; DOM editing policy remains where **D7.2.5**
places it.

`search.controls` is `false`/absent, `true` (all group, tag, from, and to
controls), or an ordered array of `"groups"`, `"tags"`, `"from"`, and `"to"`.
Checkbox filters preserve authored tag values; date inputs accept exact numeric
coordinates or ISO calendar dates/timestamps. Empty dates clear their constraint.
Clear filters writes explicit nulls, overriding configured search defaults.

Search and highlighting use one rendered text stream across adjacent inline
children, rich arrays, numbers, and booleans. Tags remain independent labels;
script, style, and template bodies do not contribute text. Field render callbacks
receive the card's chart/locale context. Callback errors remain value errors and
match predicates must return booleans. A marked typed element is presented as a
new structural value with the same qualified tag and attributes. The original
nominal value and its declared content pattern remain unchanged (S2.1.4,
S2.6.2–S2.6.4, S17.4.1/S17.7.1, D4.5.2).
Controlled `value.filters` and `value.query` retain authoritative control values
while emitting ordinary state-change requests. Invalid date commits restore the
accepted value and report `timeline_error` (S7.4.1/S7.4.2).

Slideshow elapsed milliseconds and historical date coordinates are distinct.
Seeking to a slideshow time does not mean seeking to a historical year. Media
loading may delay a slide only under a declared policy with a bounded timeout;
an unavailable player cannot deadlock story advancement.
`slideshow.delay`, `transition_duration`, and `media_timeout` use milliseconds.
With `media_end: true`, a slide waits for every audio/video player to end or fail,
up to the positive `media_timeout` (default 30,000). Pausing freezes that wait
budget. Player commands and completions carry the event/media IDs, source
revision, and a session token; restarting, seeking, source replacement, and a new
repeat cycle retire the old session. Pure reduction returns commands for the
host/application to execute (**S12.1.1v2/S12.1.3, S13.1.1v2–S13.1.5**).

### 2.8 Media and application-supplied data

`<timeline.media>` accepts `id`, `kind`, `src`, `mime`, `provider`, `poster`,
`thumbnail`, `alt`, `caption`, `credit`, `link`, `target`, dimensions, fit/alignment,
controls, muted/autoplay/loop, and start/end offsets. Its body may provide an
already constructed element or a fallback. Kinds are `'image'`, `'video'`,
`'audio'`, `'document'`, `'map'`, `'embed'`, and `'element'`.

`<timeline.media_options>` configures active-event playback, pause-on-deactivate,
aspect ratio reservation, fullscreen/lightbox behavior, providers, and fallback
presentations. Automatic fetch, prefetch, retry, timeout and disposal are outside
the revised requirements. Providers normalize resource descriptions; they do not inject
JavaScript into pure Lambda evaluation. Images, self-hosted audio/video, video
services, audio services, document services, maps, encyclopedic content, social
embeds, and generic hosted HTML have an adapter/capability inventory.

Remote provider availability is not inferred from an old feature list. An extinct
service, restricted API, or host-unsupported player produces an explicit status
and authored fallback. Source/caption/credit/link metadata remains available even
when the media cannot play. A provider poster is a valid static snapshot, but
does not count as live-player parity.

`<timeline.source format, url, name?, mapping?, refresh?, revision?>` describes
JSON, CSV/TSV, a published Google Sheet, or a provider-specific source. Loading
runs in an application procedure or live handler, producing immutable records
and a revision. `timeline.convert(data, format, options)` converts already loaded
data, including TimelineJS JSON/spreadsheet columns and documented React Chrono
or vis item/group records. No browser authentication or online account is required
for local/native timelines.

### 2.9 Records, maps, and chart composition

```lambda no-run
// no-run: proposed timeline.render_spec API and field encodings are not implemented.
import timeline: lambda.chart.timeline

let events = [
    {key: "a", date: t'2019-03-01', heading: "The idea", body: "A starting point"},
    {key: "b", date: t'2022-09-01', heading: "Launch", body: "The public release"}
]

timeline.render_spec({
    id: "history", view: "cards", spacing: "sequence", placement: "alternate",
    data: events,
    encoding: {
        id: {field: "key"}, at: {field: "date", dtype: "temporal"},
        title: {field: "heading"}, content: {field: "body"}
    }
})
```

Maps and elements normalize to the same model and default rules. Encoding channels
include `id`, `at`, `from`, `to`, `title`, `subtitle`, `content`, `display_date`,
`group`, `subgroup`, `tags`, `color`, `marker`, `size`, `opacity`, `tooltip`,
`media`, `link`, `priority`, and `metadata`. Channel definitions reuse chart
`field`, `value`, `datum`, formatting, conditions, and pure Lambda callbacks;
no second expression language is introduced.

`<timeline.series>` supplies `id`, chronology/value-axis references, visibility,
legend settings, and native chart `data`, `mark`, and `encoding` children. Point,
line, step, area, column, and range representations share timeline coordinates;
stacking and clustering of quantitative series reuse existing chart behavior.
Authoring quantitative overlays does not require converting history events into
fake numeric records.

The chart facade will also accept a native `<chart><mark type: "timeline">...`
specification through a thin adapter into this canonical model. Its timeline
options live under `timeline_options`, **not** the chart's existing `timeline`
animation property. SVG-only chart layers/facets/concat/repeat call the explicit
SVG presenter; a rich interactive story is composed as a view/panel alongside a
chart, rather than being hidden inside an SVG layer with unsupported live content.
Linked selections and windows use existing chart parameters and events.

### 2.10 Theme and extension contract

The core supplies neutral typography and presentation. A skin may set semantic
tokens for track/connector, marker, event date, card title/subtitle/body, active/
selected/disabled state, group header, era band, navigator, toolbar, tooltip,
cluster, media, pending state, and focus ring. Support light, dark, high-contrast,
compact, and presentation presets; per-part fonts, weights, spacing, radii,
shadows, alignment, marker shapes, and pure renderers remain customizable.

`parts` maps semantic names to `{class, style, attributes, render?}` recipes.
Core layout owns geometry and accessible relationships; skins cannot replace
those contracts through arbitrary positioning. A structural renderer receives
semantic slots/context and must return the required landmark/control relationships.
Fonts may be bundled or loaded as declared resources, including a Google Fonts
adapter. Font changes invalidate measurements; loading them cannot mutate module
globals or produce host-dependent export without a recorded font choice.

The dtna wrapper supplies Ant-inspired tokens, icons, spacing, and semantic part
classes through this same interface. The core has no dependency on dtna.

## 3. Feature inventory

Every row below is in the revised **in-scope target**. The references identify
the origin of a capability; the proposed API is Lambda's own. Delivery stages
sequence the work without discarding difficult rows. Historical uncertainty and deterministic native presentation additionally strengthen
the combined design.

Reference abbreviations: **RC** React Chrono, **KL** Knight Lab TimelineJS,
**VT** vis-timeline, **AC** amCharts 5 Timeline, **DT** current dtna Timeline.
They are source labels, not a new design-ruling ledger.

| Feature family | Required capabilities | Schema / API | Reference |
|---|---|---|---|
| Basic layouts | Vertical, horizontal, alternating, all-cards-visible, flipped direction, scrolling, automatic sizing, responsive switching. | Root, `cards` | RC, DT |
| Scaled chronology | Proportional dates, major/minor axes, configurable tick intervals/format, independent axis/item placement, grid and week numbers. | `scale`, `axis` | VT, AC |
| Historical dating | BCE/CE, partial dates, explicit date labels, eras, date ranges, very distant past/future. | DateSpec, `era` | KL |
| Time handling | Fixed/named zones, calendar boundaries, zoom bounds, hidden recurring intervals, current/custom times, follow-current mode. | `scale`, `window`, `annotation` | VT |
| Rich event cards | Headline/subtitle/body, semantic heading tags, links, rich elements, compact/details, read more, overlays, alignment, borderless mode. | `event`, `content`, `cards` | RC, KL |
| Narrative stage | Title/introduction, active slide, synchronized navigator, first/previous/next/last, start-at-end, slide IDs and deep links. | `story`, `navigator`, `toolbar` | KL, RC |
| Markers | Circle/square/diamond, arbitrary icons/elements, image thumbnails, per-event sizes/colors, hide points/cards separately, state feedback. | `markers`, event slots | RC, KL, DT |
| Continuation | Loading/pending marker and content, custom pending icon, reverse sequence behavior. | `pending` | DT |
| Groups | Lanes, subgroups, nesting, collapse/visibility, stable order, group labels/templates, height modes and group editing. | `group`, `edit` | VT, KL |
| Nested histories | Child timeline cards, independent navigation, inherited visual defaults, expand/collapse. | `children`, nested chart | RC |
| Overlap | Stable stacking, subgroup stacking, collision-aware labels, overflow handling, clustering and expansion. | `cards`, `cluster` | VT, AC |
| Navigation | Drag pan, wheel/pinch zoom, bounds, modifiers, click-to-activate, horizontal/vertical scroll, fit/focus and viewport notifications. | `interaction`, `window` | VT |
| Selection | Single/multiple, modifier/range/contiguous/per-group selection, deselection, hover, tooltips, item visibility and disablement. | `interaction`, event flags | VT, RC |
| Search and filters | Search across configured fields, highlighting, match navigation/counts, empty results, tags/groups/time filtering, quick jump. | `search`, `toolbar` | RC; Lambda extension |
| Editing | Create/update/delete, move date/group, resize duration, group changes, snapping, drag/drop, permission overrides, confirmation and validation. | `edit`, requests | VT |
| Live data | Add/remove/reorder/update by ID, immutable source revisions, retain state, reject stale edit validation. | `convert`, `update`, resource state | VT, RC |
| Multimedia | Images, video/audio, captions/credits/alt text, media links, poster/thumbnail, lazy loading, fit/alignment, active-player handling. | `media`, `media_options` | RC, KL |
| Hosted content | Video/audio/document/map/social providers, generic embeds, custom content, per-provider status and fallback. | Provider adapters | KL, RC |
| Presentation controls | Toolbar order/position/sticky sizing, custom labels/icons, quick-jump, fullscreen, lightbox, layout/theme switches. | `toolbar`, `media_options` | RC |
| Slideshow | Per-slide timing, autoplay, play/pause/resume/seek/stop, repeat/direction, progress, transition effects, media-aware advancement. | `slideshow`, `render_frame` | RC; chart reuse |
| Motion | Keyed enter/update/exit, animated windows/guides, interruption from visible state, reduced motion, explicit frozen samples. | Chart animation helpers | RC, AC; chart reuse |
| Shape vocabulary | Straight, serpentine, spiral, arbitrary path/points, smoothing, levels/angles/radii, tangent labels, lane offsets. | `coordinate` | AC |
| Quantitative overlays | Multiple scales/axes/series; line/step/area/column/point, fills, stacking, legend toggles, cursors and annotations. | `series`, `axis` | AC |
| Visual customization | Theme presets, dark/high-contrast, semantic classes/styles/renderers, per-element typography, custom fonts, visual states. | `theme`, `parts` | RC, KL, VT, AC |
| Authoring/import | Element/map forms, records/encoding, JSON, CSV/TSV, Google Sheets, named datasets, supported upstream data adapters. | `data`, `source`, `convert` | KL, VT, RC |
| Composition | Shared chart parameters, linked views, annotations, dashboard panels, chart grammar SVG adapter, reusable render callbacks. | `series`, chart adapter | AC, VT; chart reuse |
| Scale and diagnostics | Indexed window queries, cached visibility, clustering before rich-card measurement, bounded work, release benchmarks and capability reports. | `cluster`, resources, layout | VT, RC; Lambda requirement |

### 3.1 Scalability and animation contract

Retained preparation builds an immutable exact interval index and caches visibility
by source, query, filters, and collapsed groups. Window updates query that index;
selection and playback do not renormalize the source. Clustering occurs before
rich-card measurement. A layout reports examined candidates and measured cards so
bounded-work assertions accompany release timing and memory measurements.
Exact interval ordering and window endpoints follow S6.2.2v3/S6.2.3.

`animate: true` enables keyed enter/update/exit. A map configures nonnegative
`duration` (default 300 ms), `delay` (default 0), `easing` (shared chart easing or a
pure callback), and boolean `enter`, `update`, `exit`, `guides` switches. Omission
or false disables these transitions. `motion: reduced/none` settles immediately.
The retained displayed geometry and SVG are the source of an interrupted
transition. Exiting cards are passive and retire at completion. Frame work stops
when animation and playback settle. `render_frame` accepts `{time_ms, previous}`,
where `previous` is `{plan, svg}`, for deterministic explicit sampling
(S12.1.1v2/S12.1.3, S9.1.4/S9.3.1, D7.5.3).

### 3.2 Parity accounting

Before implementation, freeze each upstream reference to a release/commit and
capture its complete timeline option and example inventory. Each public option,
documented behavior, and relevant example maps to a row above and then to a Lambda
schema field, equivalent generic facility, and acceptance fixture. Fine controls
such as tooltip overflow, label limits, scroll direction, toolbar search sizing,
per-item edit override, and provider variants cannot disappear inside a broad
"interaction supported" checkmark.

The manifest records `planned`, `partial`, `blocked`, or `complete` for each
feature/host pair. An inapplicable framework mechanism needs a documented Lambda
equivalent; a missing user-visible capability stays open. A static image does not
complete video support, and a working small demo does not complete dense-timeline
or keyboard support. Later upstream additions require an explicit scope update;
"all features" refers to the pinned inventory, not an unbounded moving target.

## 4. Public API and state model

| Proposed entry point | Result / responsibility |
|---|---|
| `timeline.render(spec, viewport = null, snapshot = null)` | Pure HTML/SVG element presentation selected by `view`, or a value error. Does not start timers, load URLs, or write output. |
| `timeline.render_spec(map, viewport = null, snapshot = null)` | Map counterpart with identical normalization and defaults. |
| `timeline.render_svg(spec, viewport, snapshot = null, options = {})` | Pure standalone SVG presentation with explicit rich-content/resource policy. |
| `timeline.render_frame(spec, frame, viewport = null, snapshot = null)` | Frozen presentation at explicit elapsed milliseconds; historical window/cursors remain separate state fields. |
| `timeline.model(spec, viewport = null, options = {})` | Validated logical source for a live instance; retain it across parent rerenders. |
| `timeline.interactive(spec, viewport = null, options = {})` | Convenience application of a model; same lifecycle caveat as existing charts. |
| `timeline.validate(spec)` | Normalized specification or a value error naming the invalid field/path. |
| `timeline.layout(spec, viewport, snapshot = null, resources = {})` | Pure geometry and semantic layout model for testing, presentation, and picking. |
| `timeline.initial_state(spec)` | Initial history interaction state; the distinct name preserves the existing animation helper `timeline.initial` (Appendix A). |
| `timeline.update(state, action, context)` | Pure reduction to `{state, requests, notifications}`; context includes source revision, geometry, resources, and explicit time. |
| `timeline.apply_edit(spec, request)` | Revalidate a proposed immutable edit against the current source revision, stable IDs, permissions, and schema; return the candidate source or a value error. |
| `timeline.bookmark(spec, id)` | Encode a stable chart/event URL fragment, including an authored introduction. |
| `timeline.convert(data, format, options = {})` | Pure adapter into canonical events/groups/configuration. |
| `timeline.capabilities(host)` | Explicit native capability description for providers, playback and fullscreen. |

All new chart-facing functions use **value errors (S7.4.1)**, matching the chart
facade. The dtna constructor retains its existing raised-error API and converts a
core diagnostic at that boundary (**S7.4.2**). Procedural application operations engage their own raised errors normally; no error is swallowed
to make an unsupported feature appear successful.

The controllable state projection contains `active_id`, `selected_ids`, `window`,
`collapsed_groups`, `expanded_events`, `query`, `filters`, `view`, and `theme`.
Pending gestures, measured geometry, resource status, slideshow phase, request
tokens, and focus bookkeeping are instance internals, not serialized source data.
Controlled fields emit change requests; uncontrolled fields commit through the
same reducer. Explicit frame sampling accepts a plain immutable projection.
Control is determined by field presence, not truthiness: `active_id:null` clears a
controlled active event and `selected_ids:[]` clears selection. Omitted fields
remain instance-owned. This preserves Lambda's null/empty-value distinctions.

Public actions include activate/focus, first/previous/next/last, select/toggle/clear,
set-window/pan/zoom/fit, search/filter, collapse/expand, set-view/set-theme,
play/pause/resume/seek/stop and edit-request/accept/reject/cancel.
The live template emits `timeline_activate`, `timeline_select`,
`timeline_window_change`, `timeline_search`, `timeline_edit_request`,
`timeline_edit_commit`, `timeline_playback`, `timeline_media`, and
`timeline_error`. Each payload includes chart ID, affected stable IDs, source
revision, action origin, and the relevant old/new values. Per-frame animation
notifications are opt-in so they cannot flood normal change listeners.

`timeline_media` reports accepted player completions with `chart_id`, `event_id`,
media `id`, `token`, `source_revision`, `status`, and `origin`; duplicate or retired
completions emit nothing. A live native model executes built-in local audio/video
requests after rendering. `model` option `media:false` selects application-owned
controllers, which answer the same token/revision contract. Provider controllers
stay outside pure rendering (**S12.1.1v2/S12.1.3, D7.5.3**).

Event IDs relate data across updates; they do not create a new runtime identity
mechanism. Stable source/template ownership follows **S9.1.4/S12.1.3** and the
existing chart model contract. Deleted records lose active picking targets;
retained selection is reconciled by an explicit policy. A filtered active event
moves to a deterministic visible neighbor or an explicit empty stage. Collapsing
a group moves focus to its header and preserves child selection unless configured
otherwise. Removing an instance cancels its scheduling, captures, and outstanding
resource/application requests.

## 5. Absorb dtna Timeline

`lambda.chart.timeline` becomes the **sole owner of timeline data normalization,
layout, connectors, markers, ordering, nesting, selection, interaction, playback,
and accessibility behavior**. `lambda.ui.dtna.timeline` remains a compatibility
and skin adapter; `ui.timeline()` and `<dtna.timeline>` remain available.

| Existing dtna surface | Mapping to the new package |
|---|---|
| `items` | Canonical events; deterministic positional IDs for legacy static rows, explicit `key`/`id` for stateful updates. |
| `title` / `label` | Event display-date/label slot; explicit modern name takes precedence over the legacy alias. No parsing a displayed date into a time coordinate. |
| `content` / `children` | Rich event body; explicit modern name takes precedence. |
| `orientation` | Canonical vertical/horizontal orientation. |
| `mode: 'start'\|'end'\|'alternate'` | Canonical placement; legacy left/right spellings keep their existing compatibility conversion. |
| Per-item `placement` / `position` | Event placement; compute alternating placement before applying reverse, preserving current dtna behavior. |
| `reverse` | Reverse the supplied display sequence and pending placement with the existing semantics; do not force chronological sorting. |
| `color` | dtna semantic color token/explicit color mapped to the core appearance. |
| `icon` / `dot` | Core marker slot using dtna icon rendering. |
| `loading`, `pending`, `pending_icon` | Core loading/pending model, with dtna marker treatment and connector style. |
| `class`, `style`, supported attributes | Root/event semantic-part attributes; preserve documented selectors and native test targets. |

Legacy dtna defaults remain `view:'cards'`, `spacing:'sequence'`, `order:'source'`,
vertical orientation, and start placement. Undated items remain valid. Existing
dtna validation and alias precedence are captured before changing implementation;
new canonical props do not silently change the legacy prop contract. An optional
`timeline_options` map can expose new capabilities without colliding with existing
dtna names, and it cannot override compatibility-critical mappings unnoticed.

The dtna skin owns its `dtna-` classes and `--dtna-` token references, Ant-inspired
spacing and focus treatment, status palette, icons, and semantic-part recipes.
Core semantic structure must permit those classes without retaining a second
dtna layout or CSS connector algorithm. The existing timeline CSS moves into skin
recipes where it is appearance, while shared structure/geometry moves to the core.

The wrapper must preserve the original source/template identity when delegating
to the shared live presenter. Rebuilding a new stateful `<timeline.chart>` child
on every wrapper render is insufficient. The implementation must prove
source-preserving delegation using the existing template mechanism, or document
and settle a general composition gap before claiming migration complete. There
must be one shared event/state implementation, not copied dtna handlers.

Required compatibility evidence includes the existing
`test/lambda/ui_dtna/dtna_timeline_statistic.ls` golden, native
`test/ui/dtna/timeline_statistic.json` interactions/pixels, current gallery output,
and the rendered history example under `temp/dtna-history-timeline/` when available.
That temporary example is illustrative, not a durable test dependency; promote
an independent fixture and expected result into the corpus during implementation.

## 6. Rendering, accessibility, and host contracts

### 6.1 Shared pipeline

Resolve supplied records → apply data transformations → normalize and validate
events/dates/IDs/references → establish order/groups/window → project events/series
→ measure content → resolve
stacking/clusters → produce geometry plus semantic event order → present.

Reuse chart `parse`, `records`, `transform`, `calendar`, `timezone`, `scale`,
`axis`, `text`, `collision`, `path_geometry`, `coordinate`, `picking`, `parameter`,
`interaction`, `animation`, and SVG helpers where their contracts match. Promote
a generally useful helper to its module interface instead of copying a static
implementation. Shared helpers may need real extensions; a time scale for deep
history must not be implemented by forcing out-of-range values through a helper
that only supports native datetimes.

HTML card/story presentation uses ordinary elements and a shared SVG geometry
layer when needed. SVG presentation uses the same projected model and text/image
measurements; it does not assume that arbitrary HTML, live video, or embedded web
content works inside `foreignObject`. Pure callbacks are measured with the same
fonts/resources used for painting, and cache invalidation follows explicit source,
theme, viewport, resource, and font revisions.

### 6.2 Native host/media readiness

The current [support matrix](../doc/HTML_CSS_SVG_Support.md) describes local
audio/video as partial, macOS-specific support, with empty static render boxes;
the [media/webview design](../doc/dev/radiant/RAD_22_Media_Webview.md) describes
the relevant host mechanisms. Therefore native multimedia, cross-platform
playback, provider embeds, fullscreen and posters need
explicit capability tests and potentially general engine work. Their full
requirements remain in scope.

Use the established embed/DOM boundary (**D7.5.3**). No timeline-specific native
tags, ad hoc JavaScript engine, direct `lambda-rt`→Radiant link, vendor patch, or
conservative stack scanning is introduced. General engine fixes require a root
cause and focused regression evidence, with retained data owned according to
**D4.5.1v4/D5.3.3**.

## 7. Prior art and reference sources

Official sources consulted **2026-10-11**. They establish the reference inventory,
not an implementation dependency or a promise that every third-party service
remains operational.

| Reference | Sources and relevance |
|---|---|
| React Chrono | [Repository](https://github.com/prabhuignoto/react-chrono), [props reference](https://github.com/prabhuignoto/react-chrono/blob/master/PROPS-REFERENCE.md), and [releases](https://github.com/prabhuignoto/react-chrono/releases): card layouts, nested stories, media, search, controls, theme, accessibility/localization, and fullscreen. The consulted package metadata reports 3.3.3; freeze a resolved release/commit for fixtures. |
| TimelineJS 3 | [Authoring/docs](https://timeline.knightlab.com/docs/index.html), [JSON model](https://timeline.knightlab.com/docs/json-format.html), [spreadsheet/media authoring](https://timeline.knightlab.com/docs/using-spreadsheets.html), [options](https://timeline.knightlab.com/docs/options.html), [media types](https://timeline.knightlab.com/docs/media-types.html), and [repository](https://github.com/NUKnightLab/TimelineJS3): narrative, dates, eras, groups, media metadata, and authoring. The consulted package metadata reports 3.9.13. Some documentation was available through indexed copies when direct requests returned HTTP 403; verify against pinned source when building the manifest. |
| vis-timeline | [Current documentation](https://visjs.github.io/vis-timeline/docs/timeline/index.html), [examples](https://visjs.github.io/vis-timeline/examples/timeline/), and [repository](https://github.com/visjs/vis-timeline): time-axis interaction, grouping, stacking/clustering, editing, and configurable scales. Use the maintained `vis-timeline` repository, not the legacy monolithic vis docs. |
| amCharts 5 Timeline | [Timeline documentation](https://www.amcharts.com/docs/v5/charts/timeline/), [v5 introduction](https://www.amcharts.com/introducing-timeline-curve-charts/), [serpentine demo](https://www.amcharts.com/demos/serpentine-timeline-chart/), [spiral demo](https://www.amcharts.com/demos/spiral-timeline/), [exporting](https://www.amcharts.com/docs/v5/concepts/exporting/), and [accessibility](https://www.amcharts.com/docs/v5/concepts/accessibility/): custom paths, projected chart series, and associated presentation services. Timeline types joined v5 in 5.12.0; use current v5 references rather than the deprecated v4 add-on page. |

The libraries are design and behavioral references. Implement Lambda-owned code
and preserve source/license provenance for any permitted borrowed assets. React
Chrono uses [MIT](https://github.com/prabhuignoto/react-chrono/blob/master/LICENSE);
TimelineJS uses [MPL-2.0](https://github.com/NUKnightLab/TimelineJS3/blob/master/LICENSE);
vis-timeline offers [MIT or Apache-2.0](https://github.com/visjs/vis-timeline/blob/master/LICENSE.md);
amCharts has its own [free-with-branding/commercial terms](https://www.amcharts.com/javascript-charts/). Those licenses are not
interchangeable, and this proposal does not authorize copying vendor source into
Lambda or adding an upstream JavaScript runtime dependency.

## Appendix A. Module transition and delivery outline

### A.1 Resolve the existing namespace

`lmd/package/chart/timeline.ls` currently implements **animation playback** and
exports `configure`, `inherit`, `target`, `position`, `segment`, `initial`, and
`command`. `chart.ls` and chart animation tests import it. Its filename already
maps to the requested public history-timeline namespace under **D7.2.4**.

Move that implementation once to `lmd/package/chart/playback.ls`, exporting the
same animation helpers, and update internal animation consumers/tests to import
`lambda.chart.playback`. Keep the chart specification's `timeline` property and
`<timeline><keyframe>` syntax unchanged. The new history facade imports playback
for animation reuse but chart playback must not import the history facade.

Preserve all seven old helper exports as deprecated forwarding wrappers, including
`timeline.initial`. New history initialization is named `timeline.initial_state`;
there is no conflicting export and no required breaking migration. Do not dispatch
by guessing whether a map looks like history data or animation data. The wrappers
contain no implementation beyond delegation to `playback`.

Proposed source organization:

```text
lmd/package/chart/
  timeline.ls                  public history facade and view registration
  playback.ls                  relocated chart keyframe/playback implementation
  timeline/
    schema.ls                  canonical normalization and validation
    date.ls                    package DateSpec, precision and deep-time records
    layout.ls                  measured event/lane/card placement
    view.ls                    shared live presenter and state reducer integration
    present.ls                 HTML/SVG semantic presentation
    story.ls                   active stage and narrative policy
    media.ls                   media descriptions and resource state policy
    adapters.ls                external data/provider normalization
    theme.ls                   neutral tokens and semantic parts
lmd/package/ui/dtna/timeline.ls compatibility adapter and dtna skin only
```

This is an ownership sketch, not a requirement to create every file in advance.
Existing chart helpers remain shared; extract new modules only when the code has
a distinct responsibility. Timeline-specific clustering/window queries extend
shared helpers when appropriate rather than copying the network clustering code.

### A.2 Delivery stages

| Stage | Deliverable | Gate |
|---|---|---|
| Foundation | Pin references; machine-readable feature manifest; schema/ID/date normalization; namespace migration; basic cards/axis SVG. | No animation regression; element/map equivalence; exact/stable date ordering and diagnostics. |
| dtna absorption | Shared core layout/presenter; dtna adapter and skin; pending/reverse/aliases; source-preserving state delegation. | Existing dtna goldens, interactions, pixels, classes, and repeated-parent-render state retention. |
| Time exploration | Windows/axes, groups/subgroups, stacking, selection, search, clusters, navigation, linked parameters, editing. | Pointer/keyboard/touch/reducer equivalence; dense-event and stale-request cases. |
| Narrative and media | Story/navigation/bookmarks, nested histories, media providers and slideshow. | Live capabilities tested per host; no unsupported player or blocked resource counted complete. |
| Curved timelines | Shared arc-distance projection/inversion, serpentine/spiral/custom path, overlays and guide/legend integration. | Geometry/picking agreement and readable measured labels across shapes and zoom. |
| Full parity and distribution | Scalability and keyed motion, complete retained reference manifest and documentation. | Every retained upstream feature has a working equivalent and acceptance evidence, or remains visibly incomplete. |

Full-scope completion requires all stages; stage boundaries do not redefine the
requested feature set. Detailed implementation plans/progress belong in a future
`vibe/impl/` record, following [Doc Convention](../doc/Doc_Convention.md).

## Appendix B. Verification plan

1. **Schema/model:** every child/enum/cardinality, duplicate/missing IDs, invalid
   date references, element/map/adapter equivalence, unknown options, nested cycles,
   controlled state, and value/raised error boundaries.
2. **Chronology:** same-time ties, reverse order, mixed date precision, BCE/CE
   boundary/year zero conversion, leap days, zone folds/gaps, deep-time close
   events, hidden spans, open ranges, uncertainty, and empty/single-event data.
3. **Rendering:** vertical/horizontal/alternating/all-visible; rich text/media
   dimensions; groups/eras/overlap/clusters; dark/high-contrast/RTL; every curve
   shape and series family; font/resource changes and viewport constraints.
4. **Interaction:** all commands and controls, zoom/pan/fit/snap limits, selection,
   search result navigation, edits/validation/cancellation, bookmarks, nested
   histories, focus retention, fullscreen lifecycle, media and slideshow timing.
5. **Ownership:** two independent timelines, repeated parent renders, source/data
   replacement, resize/layout-mode changes, pending completion after removal,
   precise-root GC stress, interruption and reduced motion, T0/AUTO/MIR agreement.
6. **Reference parity:** pinned official examples exercised in isolated reference
   harnesses, with screenshots, actions, outputs, source hashes, fonts, viewport, zone, and motion settings; evaluate native equivalents of
   retained capabilities only. Use Puppeteer's headless shell on macOS and
   keep all generated captures under `./temp/`.
7. **Scalability:** release-build benchmarks at 100, 1,000, 10,000, and 100,000
   events, including identical timestamps, many groups, expensive cards, and large
   year magnitudes. Measure normalization, layout, viewport updates, memory, and
   interaction latency; establish budgets from measurements rather than inventing
   unverified performance claims. Window queries should scale with visible data
   plus index lookup, not rescan/measure all offscreen cards on every frame.

Removed requirements do not gate the revised scope. New Lambda test scripts receive matching golden `.txt` files. Package changes
run the Lambda baseline; changes to Radiant run its baseline and applicable
layout lint. Engine defects are fixed at their root or reported as open; tests
and capability results must not be weakened to manufacture feature parity.
