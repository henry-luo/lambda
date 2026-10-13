# Music fixtures

All score content here is original, created for Lambda's regression suite.
It contains no copied commercial engraving or score source.

- `study.musicxml`: two-measure piano study; chords, backup, two clefs, beam,
  slur, dynamics, articulation, final barline.
- `features.musicxml`: flute/viola example; inert public DOCTYPE, thirds of a
  quarter note, tuplet, grace slash, key cancellation, mid-measure clef,
  multiple voices, lyrics, tie/slur/hairpin continuations, repeats/endings,
  system/page breaks.
- `study.mxl`: deterministic ZIP with the study at `scores/main.musicxml`,
  an unrelated XML before it, and a declared PDF rendition. Tests container
  selection rather than first-file selection.
- `ambiguous.mxl`: two declared MusicXML roots, requiring explicit selection.
- `_view.ls`: manual viewer/export entry point, not a golden unit test.
- `playback.musicxml`: original two-instrument repeat/ending study, with
  transposition, ties, numeric dynamics and tempo changes.
- `pulse.mid`: original hand-encoded Type 0 file with running status, one
  piano note and two tempo segments (1.5 seconds total).
- `_player.ls`, `_midi_player.ls`: manual score and standalone MIDI players.
- `_lifecycle.ls`: native-session lifecycle probe, including a retained
  wrapper after its owner is removed from the document.
- `player.json`, `midi_player.json`, `lifecycle.json`: Radiant UI event tests,
  registered in the separate `music` UI suite; they require window/audio
  device access. Playback is deliberately started only by a test action.

Package goldens live in `test/lambda/music/` and are registered with
`test_lambda_gtest`. Layout, performance and MIDI tests are also in the
forced-GC corpus.
Bravura's independent license/provenance is under `lmd/package/music/fonts/`.
