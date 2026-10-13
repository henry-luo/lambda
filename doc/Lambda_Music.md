# MusicXML Sheet Music

`lambda.music` reads MusicXML and lays out sheet music as SVG, using native
Lambda modules and bundled Bravura glyph outlines. This is an experimental
Phase 1 notation package. MIDI playback is planned for Phase 2 and has no
public implementation yet.

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

`render` returns an HTML wrapper containing SVG pages. It uses the existing
Radiant display/export path. Music symbols are paths, and the bundled CMU
Serif text face is embedded in each SVG. No JavaScript or OSMD installation
is required. OSMD informed the score → layout → rendering architecture;
this package does not port its full engraving engine.

## API

| Function | Result |
|---|---|
| `load(source, options = {})` | Load MusicXML text or a `.mxl` ZIP snapshot through central input services |
| `parse(xml_text, options = {})` | Parse in-memory XML text |
| `from_xml(document, options = {})` | Normalize an existing `<document>` or `<score-partwise>` tree |
| `layout(score, options = {})` | Produce pages, systems, staff geometry, drawing records, event bounds, diagnostics |
| `svg(layout, page = 0)` | Produce one standalone SVG element; page index is zero based |
| `render(score, options = {})` | Produce a wrapper for all pages |

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

XML parsing uses `{type:'xml', flavor:'strict'}`. External DOCTYPE identifiers
are inert: no DTD is fetched. Internal subsets, unknown entities, malformed
XML, and foreign default namespaces fail. `from_xml` trusts the supplied
tree's XML syntax but performs the same music-model checks.

| Layout option | Default | Meaning |
|---|---|---|
| `width` | `900.0` | Requested page width in CSS pixels, 100–20,000 |
| `scale` | `1.0` | Staff-space scale, 0.25–8; one space is `10 * scale` CSS pixels |
| `mode` | `'scroll'` | `'scroll'` gives one tall page; `'pages'` breaks at systems |
| `page_height` | `1120.0` | Requested height in pixels for paged layout, 100–30,000 |
| `parts` | all parts | Array of MusicXML part IDs in source order |
| `show_title` | `true` | Show score title and composer |
| `show_lyrics` | `true` | Include lyric text and its spacing |
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
| Documents | Unnamespaced `score-partwise`, MusicXML versions 1.0–4.0, XML and MXL | No `score-timewise`, opus, or namespace-prefixed core elements |
| Timing | Exact reduced fractions, decimal divisions, pickups, chords, backup/forward, multiple voices/parts/staves | Independent polymeters, changing staff counts, overlapping events in one voice fail |
| Pitch/context | Pitched notes, G/F/C clefs, conventional keys and cancellation, simple/additive meters, context changes | No percussion, tablature, microtones, alternate staff tuning, cue notes, or octave-shift directions |
| Rhythm | Long through 64th notes/rests, up to three dots, grace notes and slash, explicit beams and hooks | No automatic beaming; cross-staff, mixed-stem, or cross-measure beams fail |
| Chords | Shared stems, alternating seconds, accidental columns | Dense multivoice unisons and complex chords may require further collision refinement |
| Spanners | Ties, numbered slurs, single-level tuplets, hairpins, system continuations | Nested tuplets fail; unmatched endpoints fail; complex curve collisions need further refinement |
| Marks | Words/rehearsal/tempo text, ppp–fff, mp/mf, sfz/sffz/fp, common articulations, fermatas, segno/coda | Other decorations produce warnings |
| Navigation | Repeat barlines, numbered ending brackets | Display only; repeats and jumps are not executed |
| Lyrics | Text, verse rows, syllabic hyphens, measured spacing | Extender lines produce warnings and are not drawn |
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
the event's symbols, stems, and lyrics; connecting spanners have separate
drawing records. SVG event groups carry `data-event-id` and accessible labels.

Font provenance and license are in
[`music/fonts/SOURCES.md`](../lmd/package/music/fonts/SOURCES.md).
The [design](../vibe/Lambda_Design_Pkg_Music.md) describes the broader target;
the [implementation record](../vibe/impl/Lambda_Impl_Pkg_Music.md) tracks
validation and remaining work.
