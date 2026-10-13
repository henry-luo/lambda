# Lambda Music — Implementation

**Started:** 2026-10-13. **Status:** Phase 1 notation and Phase 2 MIDI bounded
profiles implemented; live playback verified on macOS. Linux/Windows live
verification and broader expressive notation remain outside the verified profile.

Design: [Lambda_Design_Pkg_Music.md](../Lambda_Design_Pkg_Music.md).
The user selected native Lambda and requested implementation. D7.2.4 governs
the package, S12.1.1v2 separates score transformations from playback effects,
and D7.1.2v2/D7.5.3 govern resource reads and Radiant calls.

## Delivered

- `lmd/package/music.ls`: explicit public `load`, `parse`, `from_xml`, `layout`,
  `svg`, and `render` facade (D7.2.4/D7.2.6, S7.4.2).
- `music/musicxml.ls`, `common.ls`, `time.ls`: score-partwise normalization,
  exact rational time, inherited/staff-scoped contexts, voices/chords,
  pickups, backup/forward, bounded validation, diagnostic policy. MXL uses
  the existing archive snapshot and declared rootfile; it never extracts
  or selects an arbitrary XML member (S12.4.1v2/S14.3.1v2).
- `music/fonts.ls`, `glyphs.ls`, `fonts/`: unmodified pinned Bravura/OFL
  assets, metadata, glyph outlines through existing `radiant.math_metrics`,
  shaped labels through `radiant.measure_text`. CMU Serif is reused from
  the existing LaTeX distribution (D7.1.2v2/D7.5.3).
- `music/notation.ls`, `header.ls`, `layout.ls`: accidentals, chord seconds,
  context glyphs, shared exact-onset columns, rhythmic spacing, source breaks,
  part filtering, staff geometry, scroll/pages, overflow diagnostics.
- `music/notes.ls`, `spans.ls`, `decorations.ls`, `draw.ls`, `engrave.ls`:
  anchored stems, beams/hooks, ledger lines, grace slashes, dots, articulation,
  lyrics, dynamics/words/metronome text, repeat barlines, endings, and
  tie/slur/tuplet/hairpin connections including system continuations.
- `music/svg.ls`: ordinary SVG with event IDs, accessible labels and page
  bounds. Event bounds use page-local CSS pixels; drawing geometry uses
  staff spaces. `doc/font.ls` owns shared font embedding, extracted from
  `math/font.ls` without a second implementation.
- Original piano and flute/viola fixtures, deterministic MXL archives,
  registered golden tests, and `test/music/_view.ls`.
- User API and support manifest: [Lambda_Music.md](../../doc/Lambda_Music.md).

The broad design remains the target. This release has no complete collision
solver, automatic beaming, lyric extender lines, percussion/tab/microtonal
profile, or cross-staff/cross-measure beams. Unsupported decorations warn;
unsupported structural features fail. Tuplets are single level. Source
coordinates/default typography are not reproduced. Spanners are resolved
during layout; structural event IDs are stable under reflow, with source IDs
retained separately as metadata. The Phase 2 delivery is recorded below.

## Shared runtime fixes

`input.cpp` exposes the existing strict XML reader through `flavor:'strict'`.
`XmlParseOptions.allow_inert_doctype` permits a single external identifier
before the document element. Internal subsets, entity declarations,
malformed/public identifiers, nested/trailing/duplicate DOCTYPEs fail. The
ordinary XML reader and existing strict clients retain their previous policy.
No external fetch or vendor-parser change was introduced.

Forced GC found a pre-existing lifetime defect in `fn_parse2`: converting the
symbol `flavor` could collect the converted `type` string, so
`{type:'xml',flavor:'strict'}` dispatched to format `strict`. Slot-backed roots
now retain the source/options and converted strings through dispatch, per
D5.3.3. `parse_option_roots.ls` and the music layout regression are registered
in the existing forced-GC corpus; conservative native-stack scanning remains
unused.

## Validation record

The following is the Phase 1 record; Phase 2 checks are recorded separately.

- `make build` and `make build-test`: passed.
- Six focused goldens: passed on both automatic and JIT execution (five music
  scripts plus parser-root regression).
- Registered forced-GC cases: 2/2 passed, covering native JIT, randomized
  collections and MIR-interpreted execution; complete GC corpus: 303/303.
- Input baseline: 2,112/2,112 passed.
- SVG serialization, piano PNG, second-page SVG/PNG, and piano PDF smoke
  checks passed. The PDF was also rasterized with Poppler and inspected.
- Initial broad Lambda baseline: 6,827/6,828 including the input baseline.
  `edit_view_only` fails the editor's Markdown math round-trip check. The
  same failure reproduces with the original, pre-extraction math font helper
  in an isolated Lambda home. Its expected output was not changed.
- Final isolated Lambda baseline: 4,718/4,720. The editor failure persists;
  `latex_test_latex_phase3_corpus` also hit the 60-second timeout while the
  broad PDF suite ran concurrently (it passed in the initial baseline).
  The isolated serial rerun also timed out at 60 seconds. Its timeout was
  not raised, and this remains an unresolved broad-gate failure; the initial
  non-isolated run's pass does not establish a clean final baseline.
- Broad input runner: 3,859/3,883 as reported by the runner. Failures include
  the Mermaid LOC budget, validator executables aborting with unresolved
  `_LIT_BOOL`, CSS CRUD/parsing/round-trip checks, and Markdown source-span
  checks. The PDF suite reported background/gradient/shadow pixel mismatches,
  then timed out in `CanvasWebGlExportTest.PixelsAlphaAndEffectsReachEveryOutput`
  after 180 seconds without output. These results are not reported as a
  passing gate; fixtures, thresholds, and goldens were not relaxed.

Concurrent builds replaced the shared `lambda.exe` during an initial input
run, producing exit-127 failures. Verification was repeated with a separately
linked debug executable under `temp/music/verify/`, an unmodified runner
copy, and independent scratch/output directories. The Lambda baseline uses
`temp/music/verify-lambda/` with the same immutable executable. Source and
fixtures remain the working checkout. This isolates executable replacement;
it is not a different test policy.

The first verbose forced-GC layout probe was killed under resource pressure;
the quiet probe and the registered GC harness subsequently passed. Test
artifacts are under `temp/music/`; no performance claims use these debug runs.

## Phase 2 delivery (2026-10-13)

- `music/performance.ls` and `timeline.ls` implement the common immutable
  performance: exact quarters, tempo segments, nonnested repeats/endings,
  per-note instruments, transposition, contiguous ties, numeric/basic dynamics
  and sound damper pedal. Occurrences retain written IDs and repeat passes.
  Invalid same-key overlaps, zero durations, unresolved ties, unsupported
  timing/navigation and channel conflicts fail. Ignored expressive notation
  is diagnosed; strict mode rejects diagnostics (S7.4.2, D7.2.5).
- `music/midi.ls` reads bounded Type 0/1 PPQN SMF, including running status,
  note/program/control/pitch-bend messages and retained metadata. SysEx and
  unsupported sound-changing messages are diagnosed. Export writes Type 1
  conductor/channel tracks; exact PPQN is derived from rational denominators,
  or explicit quantization rounds absolute positions and rejects collapsed
  notes. Import/export use immutable binary values, without device/file effects.
- `radiant/midi_player.cpp` copies native event buffers, preallocates 16
  channels/256 voices, synthesizes stereo PCM with pinned TinySoundFont and
  streams through miniaudio. Sample frames own transport time. Play, pause,
  stop, seek, speed, volume, mute and solo mutate only after joining the
  callback. The callback has no Lambda values, DOM access, GC, locks, resource
  reads or allocation (D4.5.2/D5.3.3). Null device backends are rejected.
- Existing `DocumentAudio` now owns MIDI slots alongside encoded-audio slots.
  Shared token allocation never reuses a session ID. A `DomNodeRef` tracks
  each owner across rebuilds; frame-loop pruning closes disconnected owners,
  even if a wrapper retains the node. Document teardown closes all slots.
  `dom.midi_open`, `midi_command`, `midi_info` extend the existing host seam;
  `audio_close` closes either resource kind (D7.4.6).
- `music/player.ls` builds a reactive, initially silent score/standalone MIDI
  player. Only Play loads the SoundFont and opens a device. UI controls and
  stable note-group highlights read the native clock; document frames never
  schedule musical onsets. Performance replacement/close releases the session.
- Pinned unmodified TinySoundFont (MIT), miniaudio 0.11.23 (MIT No Attribution)
  and separately licensed TimGM6mb (GPL-2.0) include provenance, hashes and
  notices. The existing release script already distributes `lmd`, including
  the new sound bank. The full executable includes the adapter through its
  existing Radiant source glob; the native test is registered in build config.

The pinned TSF public all-notes-off call ignores sustain and all-sounds-off
retains a release tail. The Lambda adapter sends ordinary key releases for
CC123 and retires active voice slots for immediate silence; vendor files are
unchanged. Seek/resume reconstruct controller state and held keys by replaying
the bounded prefix on the control thread. Envelopes restart; repeated sustained
strikes of one key reconstruct the latest strike. These limits are public.
GeneralUser GS was considered but its published notes warn of TinySoundFont
modulator incompatibility; it is not distributed.

### Runtime defect found by performance construction

`interp_plan.cpp` reserved one collection home for every Cartesian `for`
expression. Recursive evaluation retains one home per source clause, so a
three-source comprehension with allocating calls could exhaust scratch roots.
The planner now counts clauses and reserves those homes plus the existing
iteration temporaries. `cartesian_scratch_roots.ls` reproduces the ownership
pattern and joins the forced-GC corpus (D5.3.3); no conservative scanning or
fallback evaluator was introduced.

Package reducers also need explicit raised-error propagation before accessing
their result (S7.4.2). Otherwise an error's missing fields can look like an
empty note map. Negative tests cover overlaps, collapsed export durations,
unclosed ties, nested repeats, missing instruments and strict expression
diagnostics. Test fixtures and golden expectations were not relaxed.

### Phase 2 validation

- Native debug build and test build completed; no performance claims use them.
- Ten focused goldens pass in automatic, JIT and interpreter modes (using
  `LAMBDA_EXEC_BACKEND` for pinned tiers).
- Native synthesis: nine deterministic offline tests pass, including all 128
  GM programs, sample boundaries, transport, sustain, controller reset,
  immediate silence, seek, masks, invalid inputs and repeated open/close.
- Actual macOS output: the opt-in `MidiPlayback.LiveDeviceClock` passes with
  native device access. A sandbox run cannot open Core Audio; miniaudio's null
  fallback is explicitly refused. Device hot-unplug is not manually verified.
- Three native UI fixtures pass: 43 events and 18 assertions across score
  playback/highlighting, standalone MIDI completion, seek/speed/mute/solo,
  resource removal and stale-handle rejection. They are a separate `music`
  suite because they require real window/audio services.
- Player PNG rendered and visually inspected. Independent Mido 1.3.3 reads
  exported `playback.musicxml` as Type 1, three tracks, six sounding notes,
  ten seconds; standalone Type 0 import and Type 1 round trips have goldens.
- Four focused forced-GC regressions pass; full registered GC corpus passes
  306/306. The interpreter scratch-home regression also passes all tiers.
- Radiant float-layout lint (`^no-int-cast-radiant$`) passes.
- Broad Lambda baseline: 4,727/4,728, including the full 306-case GC corpus.
  The only failure is the Phase 1 `edit_view_only` Markdown/math source
  round-trip mismatch; its golden remains unchanged.
- Input baseline: 2,112/2,112. Combined with the runtime gate this is
  6,839/6,840; the editor round-trip remains the sole Lambda baseline failure.
- Radiant native view tests 176/176, UI automation 569/569, DOM UI 156/156,
  view-reuse 514/514 and Scene3D 24/24 pass with window access. Page snapshot
  passes; render baseline permits its four recorded differences and one skip.
  CSS cascade memory fails at `jqueryui` (381,630 live CSS bytes versus a
  373,940-byte budget). The isolated layout rerun has 2,946 passing,
  347 baseline-permitted partial cases, and one Markdown failure out of 3,294.
  These are unresolved broad-gate failures, not a clean Radiant baseline.

The initial sandbox Radiant run was stopped after window-service connection
failures. The native-access rerun completed; its layout adapter still followed
the main executable, which a concurrent build temporarily removed. Layout was
therefore rerun with `RADIANT_TEST_EXECUTABLE` pointing to the isolated host,
eliminating those startup failures. Tests, timeouts and baselines were not
weakened. Unrelated concurrent CSS/UI edits were left untouched.

Phase 2 artifacts are under `temp/music-p2/`. A separately linked debug host
under `verify/` and independent runner/output directories isolate concurrent
replacement of the checkout's main executable. Linux/Windows builds and
audio-device tests remain unverified; the miniaudio backend list alone is not
evidence of platform parity.
