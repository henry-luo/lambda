# MusicXML Sheet Music and MIDI Playback

`lambda.music` reads MusicXML and lays out sheet music as SVG, using native
Lambda modules and bundled Bravura glyph outlines. This is an experimental
notation and playback package. It also converts scores to MIDI, reads Standard
MIDI Files, and plays them through a bundled SoundFont synthesizer.

The explicit entry module follows D7.2.4/D7.2.6. Scores and layouts are
ordinary immutable values. All public operations raise errors (`T^`,
S7.4.2); successful values retain recoverable warnings in `diagnostics`.
Import checks musical structure; layout additionally validates beam/spanner
connections and diagnoses unsupported visual decorations. Set `strict` on
each operation whose warnings should become errors.

## Display a score

```lambda
import music: lambda.music
let score = music.load("test/music/study.musicxml")^
music.render(score, {width: 760.0})^
```

Try the supplied example from the repository root:

```sh
./lambda.exe view test/music/_view.ls
./lambda.exe render test/music/_view.ls -o temp/music.png
```

The supplied viewer and player samples use `a4-study.musicxml`, a twenty-measure
Morning Study arranged in five piano systems on one A4 portrait page
(210 × 297 mm). Their shared page dimensions and engraving scale are in
`test/music/_sample.ls`.

`render` returns an HTML wrapper containing SVG pages. It uses the existing
Radiant display/export path. Music symbols are paths, and the bundled CMU
Serif text face is embedded in each SVG. No JavaScript or OSMD installation
is required. OSMD informed the score → layout → rendering architecture;
this package does not port its full engraving engine.

Embedded pages fit the available container width and keep their page aspect
ratio when the window narrows. Standalone `svg` output retains the requested
physical page dimensions. The player uses the same responsive score display.

## API

| Function | Result |
|---|---|
| `load(source, options = {})` | Load MusicXML text or a `.mxl` ZIP snapshot through central input services |
| `parse(xml_text, options = {})` | Parse in-memory XML text |
| `from_xml(document, options = {})` | Normalize an existing `<document>` or `<score-partwise>` tree |
| `layout(score, options = {})` | Produce pages, systems, staff geometry, drawing records, event bounds, diagnostics |
| `svg(layout, page = 0)` | Produce one standalone SVG element; page index is zero based |
| `render(score, options = {})` | Produce a wrapper for all pages |
| `performance(score, options = {})` | Expand a score into an exact MIDI performance timeline |
| `read_midi(bytes, options = {})` | Read Type 0/1 PPQN Standard MIDI File bytes into a performance |
| `midi(performance, options = {})` | Encode a Type 1 MIDI file as immutable binary |
| `player(score_or_performance, options = {})` | Construct a player with transport, track controls, and optional score |

For example, write the first SVG using ordinary procedural output:

```lambda
import music: lambda.music
pn main() {
    let score = music.load("test/music/study.musicxml")^
    let sheet = music.layout(score, {width: 760.0})^
    output(format(music.svg(sheet)^, 'xml')^, "temp/music.svg")^
}
```

Input options are `strict` (boolean), `format` (`'xml'` or `'mxl'`, for
`load`), and `rootfile` (an exact member path, for `.mxl`). Extension-based
MXL detection also works for path values and archive member nodes. The
container's `META-INF/container.xml` selects the score; unrelated XML and
PDF renditions are not selected. Multiple MusicXML roots require `rootfile`.
Input snapshots and archive limits follow S12.4.1v2/S14.3.1v2.

XML parsing uses `{type:'xml', flavor:'strict', preserve_whitespace:true}` to
retain meaningful spaces between lyric fragments. External DOCTYPE identifiers
are inert: no DTD is fetched. Internal subsets, unknown entities, malformed
XML, and foreign default namespaces fail. `from_xml` trusts the supplied
tree's XML syntax but performs the same music-model checks. When preparing a
tree yourself, use the same whitespace option for faithful lyric text.

| Layout option | Default | Meaning |
|---|---|---|
| `width` | `900.0` | Requested page width in CSS pixels, 100–20,000 |
| `scale` | `1.0` | Staff-space scale, 0.25–8; one space is `10 * scale` CSS pixels |
| `mode` | `'scroll'` | `'scroll'` gives one tall page; `'pages'` breaks at systems |
| `page_height` | `1120.0` | Requested height in pixels for paged layout, 100–30,000 |
| `parts` | all parts | Array of MusicXML part IDs in source order |
| `show_title` | `true` | Show score title and composer |
| `show_lyrics` | `true` | Include lyric text, connectors and verse spacing |
| `respect_breaks` | `true` | Honor source system/page breaks |
| `strict` | `false` | Promote diagnostics to errors |

Unknown options produce warnings. Invalid values fail. Systems that cannot
fit retain their minimum spacing, enlarge the output page, and report
overflow. The final system uses natural spacing unless it occupies most of
the requested width. Source page coordinates and `<defaults>` typography
are not reproduced.

## Current notation profile

| Area | Implemented | Limitations |
|---|---|---|
| Documents | Unnamespaced `score-partwise` and `score-timewise`, MusicXML versions 1.0–4.0, XML and MXL | No opus or namespace-prefixed core elements; each timewise measure must contain every declared part exactly once |
| Timing | Exact reduced fractions, decimal divisions, pickups, chords, backup/forward, multiple voices/parts/staves | Independent polymeters, changing staff counts, overlapping events in one voice fail |
| Pitch/context | Pitched notes, G/F/C clefs, conventional keys and cancellation, simple/additive/composite meters, hidden time signatures, context changes | No percussion, tablature, microtones, alternate staff tuning, cue notes, or octave-shift directions |
| Rhythm | Long through 64th notes/rests, up to three dots, omitted types inferred from exact duration/dots/tuplet ratio, grace notes and slash, explicit beams and hooks | Grace notes still need explicit types; no automatic beaming; cross-staff, mixed-stem, or cross-measure beams fail |
| Chords | Shared stems, alternating seconds, accidental columns | Dense multivoice unisons and complex chords may require further collision refinement |
| Spanners | Ties, numbered slurs, single-level tuplets, hairpins, system continuations | Nested tuplets fail; unmatched endpoints fail; complex curve collisions need further refinement |
| Marks | Words/rehearsal/tempo text, ppp–fff, mp/mf, sfz/sffz/fp, common articulations, fermatas, segno/coda | Other decorations produce warnings |
| Navigation | Repeat barlines, numbered ending brackets | Playback executes ordinary nonnested repeats/endings; jumps are unsupported |
| Lyrics | Multiple text/elision fragments, numbered/named verse rows above/below, print visibility, syllabic hyphens, melisma extensions across systems/pages, tied syllables, repeat-specific highlighting | Plain underlay typography; source font styling and exact authored positions are normalized; no synthesized singing |
| Output | SVG, HTML wrapper, part filtering, width/scale, scroll/pages, event bounds | Experimental engraving; no complete collision solver or editorial-layout reproduction |

Recognized structural features outside the profile fail rather than changing
pitch or timing silently. The importer is a bounded adapter, not a complete
MusicXML schema validator. Limits: 16 MB XML text, 128 element levels in the
music tree, 32 parts, 8 staves per part, 2,048 measures per part, 1,024 events
per measure, and 50,000 events per score. Fraction operands and intermediates
are limited to one billion; excess precision fails explicitly.

## Data and selection

`Score.parts` retains ordered part metadata and measures; `Score.events`
contains notes/rests. Each event has a structural ID, source ID when supplied,
part/staff/voice, measure index, exact local `onset` and `duration` as
`{num, den}` quarter-note fractions, written pitch, context, and notation.
Measure `start` is the exact score position. Reflow and part filtering preserve
event IDs. Source IDs are retained as metadata, never used as unchecked keys.

`Layout.systems` and drawing records use staff spaces. `Layout.events[id]`
gives `page`, `system`, `part`, `staff`, `voice`, and a CSS-pixel `bounds` map
with `left`, `top`, `right`, `bottom`, relative to that page. Bounds include
the event's symbols, stems, and local lyric underlay; connecting spanners have
separate drawing records. SVG note groups carry `data-event-id`; lyric groups
carry `data-lyric-id`, `data-verse` and `data-lyric-kind` (`text`, `hyphen`,
`extend`). Both have stable highlight IDs and accessible labels.

## Play and export music

```lambda
import music: lambda.music
let score = music.load("test/music/study.musicxml")^
music.player(score, {layout:{width:760.0}})^
```

Run `./lambda.exe view test/music/_player.ls` and press **Play**. The player
opens the audio device only on that action, following S12.1.1v2/D7.4.6.
Pause/resume, stop, position, speed, master volume, and part mute/solo share
the audio sample clock. Speed choices apply before playback, while playing,
and while paused; the status displays the synthesizer's current multiplier.
Part mute/solo controls sit beside the transport buttons in the primary toolbar.
The time display always includes tenths of a second and reserves its own space
so the controls stay still during playback. The A4 sample fills the window,
keeping the controls at the top while the sheet scrolls beneath them. Playback,
seeking and resizing bring the sounding notes into view; visible notes do not
trigger scrolling. In a pane too short for all sounding parts, the leading note
takes priority. Embedded players use the height of their containing element.
Active notes default to red. The **Highlight** swatches offer red, blue and
yellow as squares with rounded corners and a border marking the selected color.
They support mouse and keyboard selection, updating noteheads, stems, flags,
accidentals/dots and shared beams.
Lyrics use the same highlight color. Syllables remain highlighted over melismas
and tied continuations; `time-only` selects the verse for the current repeat
pass. Hyphens and extension lines highlight with their syllable. Pause keeps
the current lyric, seeking updates it, and Stop clears it. Try the original
two-verse example with `./lambda.exe view test/music/_lyrics_player.ls`.
Color changes apply immediately while playing or paused. Removing the player or closing
the document releases its native session; a disconnected device reports an
error. A real device is required: silent null backends are rejected.

Standalone MIDI uses the same transport, with channel controls and no invented
notation. Try `./lambda.exe view test/music/_midi_player.ls`, or:

```lambda
import music: lambda.music
music.player(music.read_midi(input("test/music/pulse.mid", 'binary')^)^)^
```

Export is separate from playback and uses ordinary procedural file output:

```lambda
import music: lambda.music
pn main() {
    let score = music.load("test/music/playback.musicxml")^
    output(music.midi(music.performance(score)^)^, "temp/performance.mid")^
}
```

| Operation/options | Behavior |
|---|---|
| `performance`: `tempo`, `strict` | Optional positive quarter-note BPM overrides source tempo instructions; default 120 BPM emits a diagnostic when no tempo exists |
| `read_midi`: `strict` | Promote unsupported-message and option diagnostics to errors |
| `midi`: `ppqn`, `quantize`, `strict` | Automatic exact resolution starts at 480 ticks/quarter; explicit integer resolution is 1–32767; rounding requires `quantize:true` |
| `player`: `layout`, `performance`, `soundfont`, `strict` | Nested maps configure score layout/performance; `soundfont` overrides the bundled local SF2 path; strict checks player option names |

Source timing remains exact `{num,den}` quarter notes. `Performance.events`
contains MIDI `status/a/b`, exact `at`, and derived `seconds`; `tempos` contains
microseconds per quarter and segment times. `tracks` groups channels for UI
controls; `occurrences` maps written event IDs and repeat passes to performed
positions. `duration` and `seconds` describe the same endpoint. Rounded export
positions are within half a tick; a collapsed sounding note fails. Source
tempos are rounded to MIDI's integer microseconds per quarter.

| Playback area | Supported profile and boundaries |
|---|---|
| Score timing | Chords, voices, rests, tuplets using their existing duration, contiguous ties, chromatic/octave transposition, simple repeats and comma-separated numbered endings |
| Score instruments | Per-note instrument IDs; up to 15 independent pitched channels, GM programs 1–128, part volume/pan; conflicting channels, nondefault banks, percussion and same-key overlaps fail |
| Tempo/expression | Numeric sound tempo, metronome beat units/dots, ppp–fff and mp/mf, numeric dynamics, sound damper-pedal; grace/fermata timing, jumps and nested sound instructions fail |
| Expressive notation | Articulations, slurs, hairpins, ornaments and free text do not infer expressive timing or velocity; unsupported playback expression emits diagnostics |
| MIDI import | Type 0/1 metrical timing, running status, note/program/control/pitch-bend messages; metadata retained; SysEx/pressure and unsupported controls diagnosed; Type 2/SMPTE rejected |
| MIDI export | Type 1 conductor plus channel tracks; retained metadata moves to the conductor track; source track order/encoding is not byte-preserved |
| Transport | Speed 0.25–4 via host API (UI offers 0.5–2), volume 0–1; seek/resume reconstruct controllers and held keys, restarting note envelopes; repeated sustained strikes of one key reconstruct the latest strike |
| Synthesis | TinySoundFont, 256 voices, stereo 48 kHz, bundled TimGM6mb GM bank; no hardware MIDI routing, recording, effects editor, or audio-file export |

Limits are 16 MB MIDI input, 128 source tracks, 200,000 events, 24 hours of
performance, 8,192 performed measures and 16 repeat passes. Seek replays the
bounded event prefix on the control thread. Up to four live MIDI sessions
belong to one document. SoundFont loading and device startup are synchronous
control-thread work; the audio callback performs no file IO, GC, or DOM work
(D4.5.2/D5.3.3).

Live output is verified on macOS/Core Audio. miniaudio includes Linux and
Windows backends, but those platforms have not been built or device-tested
for this package yet. Headless performance/MIDI conversion needs no device.
The full facade imports Radiant; reduced hosts can import the data-only
`lambda.music.performance` (`build`) and `lambda.music.midi` (`read`/`write`)
modules directly.

TinySoundFont and miniaudio are unmodified pinned dependencies. TimGM6mb is a
separate GPL-2.0 sound-bank asset, with provenance, copyright and license under
[`music/soundfonts/`](../lmd/package/music/soundfonts/SOURCES.md). Applications
can supply another compatible SF2 through `soundfont`.

Font provenance and license are in
[`music/fonts/SOURCES.md`](../lmd/package/music/fonts/SOURCES.md).
The [design](../vibe/Lambda_Design_Pkg_Music.md) describes the broader target;
the [implementation record](../vibe/impl/Lambda_Impl_Pkg_Music.md) tracks
validation and remaining work.
