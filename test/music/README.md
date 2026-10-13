# Music fixtures

All score content here is original, created for Lambda's regression suite.
It contains no copied commercial engraving or score source.

- `study.musicxml`: two-measure piano study; chords, backup, two clefs, beam,
  slur, dynamics, articulation, final barline.
- `a4-study.musicxml`: original twenty-measure expansion of Morning Study,
  arranged as five piano systems on one A4 portrait page (210 × 297 mm).
- `features.musicxml`: flute/viola example; inert public DOCTYPE, thirds of a
  quarter note, tuplet, grace slash, key cancellation, mid-measure clef,
  multiple voices, lyrics, tie/slur/hairpin continuations, repeats/endings,
  system/page breaks.
- `study.mxl`: deterministic ZIP with the study at `scores/main.musicxml`,
  an unrelated XML before it, and a declared PDF rendition. Tests container
  selection rather than first-file selection.
- `ambiguous.mxl`: two declared MusicXML roots, requiring explicit selection.
- `_view.ls`: manual A4 sheet viewer/export entry point, not a golden unit test.
- `_sample.ls`: shared A4 layout and score source for the viewer/player samples.
- `playback.musicxml`: original two-instrument repeat/ending study, with
  transposition, ties, numeric dynamics and tempo changes.
- `pulse.mid`: original hand-encoded Type 0 file with running status, one
  piano note and two tempo segments (1.5 seconds total).
- `_player.ls`, `_midi_player.ls`: manual A4 score and standalone MIDI players.
- `_lifecycle.ls`: native-session lifecycle probe, including a retained
  wrapper after its owner is removed from the document.
- `player.json`, `midi_player.json`, `lifecycle.json`: Radiant UI event tests,
  registered in the separate `music` UI suite; they require window/audio
  device access. Playback is deliberately started only by a test action.
  The score player also checks that track controls occupy the primary toolbar.
- `player_controls.json`: keyboard/mouse speed commits before and during
  playback, keeping the dropdown open across active-note updates, plus
  speed changes while paused.
- `player_resize.json`: SVG containment from 320 to 1,200 pixels and page
  aspect ratio after resizing back to a wide viewport.
- `player_highlight.json`: mouse/keyboard swatch selection and exclusive
  pressed state, red/blue/yellow whole-note colors, inherited stem strokes,
  shared beam highlighting, paused updates and clearing on stop.
- `player_scroll.json`: smaller swatches, stable transport/status geometry,
  independent sheet scrolling, note visibility after seeking forward/backward
  and resizing, and following during playback across systems in a short window,
  with no page scroll or toolbar movement.

Package goldens live in `test/lambda/music/` and are registered with
`test_lambda_gtest`. Layout, performance and MIDI tests are also in the
forced-GC corpus.
Bravura's independent license/provenance is under `lmd/package/music/fonts/`.
