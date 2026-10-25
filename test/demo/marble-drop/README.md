# Marble Drop

```sh
./lambda.exe demo marble-drop
```

Sixteen original mechanical puzzles inspired by
[Marble Drop (Maxis, 1997)](https://en.wikipedia.org/wiki/Marble_Drop).
Detailed da Vinci-style sepia ink studies show perspective gears, an articulated
wing, a screw-and-pulley mechanism and an escapement. Cross-hatching gives the
wood, metal and wing membrane depth. The transparent illustration sits behind
the working apparatus; slanted notes and leaders explain each puzzle.
Choose a colored marble from the tray and click **Drop A** or **Drop B** above
the machine. Fill every pocket with its matching color. The dots beneath a
pocket show its quota. The brighter rail shows the current route. A switch
flips after a marble passes, while a three-way distributor advances one notch.
Magnet and lens previews follow the selected marble's color.

The puzzles introduce alternating levers, branching routes, a remote switch
link, catches that keep their first marble, and pockets needing multiple
marbles. A steel ball operates devices without filling a colored pocket;
later machines also have silver pockets that need steel.
Puzzle 6 onward provides one black marble, which matches any
unfinished pocket. Colored marbles are limited to the pocket quotas;
**Undo** recovers a mistaken drop, even while it is moving. **Restart** resets
the current machine. All sixteen puzzles are available from the numbered tabs.
Three stars mean no lost colored marbles and no more than the stated par.
Stars stay in memory while the demo is open.

**A little hint** suggests a move from the current state. **Auto Play** finds
and animates a solution using the same rules as manual play; it stops at the
end of the puzzle. It can be stopped or paused during a drop. **Next invention**
continues after a win. There is no time limit.

Puzzles 9–16 add five devices, then combine them into larger machines:

| Device | Behavior |
| --- | --- |
| Three-way distributor | Routes left, middle, right, then repeats; remote links can advance it too |
| Counterweight catch | Keeps two marbles before opening permanently |
| Magnet | Sends steel left and glass right, including black marbles |
| Color lens | Sends its exact matching color left and all other colors right; black changes color only in a pocket |
| Spring launcher | Sends the marble along an upward flight into a higher part of the machine |

The strips beneath the apparatus show device settings and rules. The final
machine combines a magnet, counterweight, spring, distributor and blue lens.
All sixteen have clean solutions at the displayed par, including the steel
needed to load catches and fill silver pockets.

Sound effects start with the first drop: glass taps, brass switch clicks, a
catch's clunk, pocket chimes, a steel ping, a dull missed drop, and a short
glass arpeggio for a solved machine. Click **♪ Sound on/off** beneath the board
or press **M** to mute. That preference stays across restarts and puzzle changes.
Pause, undo, restart and navigation stop outstanding sounds; unmuting does not
replay earlier effects. If native playback is unavailable, the control says
**Sound unavailable** and the puzzles remain playable.

| Key | Action |
| --- | --- |
| 1–6 | Select red, orange, yellow, green, blue, purple |
| 7 / 8 | Select steel / black |
| A / B | Drop into that inlet |
| Space / Enter | Drop into the selected inlet |
| U / Backspace | Undo the last drop |
| P / Escape | Pause / resume |
| M | Sound effects on / off |
| H | Hint |
| R | Restart this puzzle |
| N | Next puzzle after winning |
| X | Start / stop Auto Play |

The game is entirely Lambda, CSS, and generated SVG, with no downloads or external
assets. `mod_levels.ls` defines the machines, `mod_model.ls` owns pure routing,
transitions and the bounded planner, and `mod_scene.ls` draws the interface.
The model follows **S1.4** (instance-owned mutable state) and **S12.1.3**
(pure template rendering, mutation in handlers) in the
[formal semantics](../../../doc/Lambda_Formal_Semantics.md).
Animation and planning share the device visitor; the moving marble follows the
same cubic geometry as the painted rail. The apparatus is cached as an SVG
image between device or colored-route changes, with a separate rolling marble overlay.
The detailed artwork is a static PNG layer, so moving a marble does not rebuild
or embed the illustration. The sketch asset was made with the built-in imagegen
tool; its exact prompt is recorded in
[`assets/sketches/artwork.json`](assets/sketches/artwork.json).
A shell-owned 40 ms clock survives game redraws and leaves an idle machine unchanged.
Each rail segment takes 280 ms; both manual drops and Auto Play use this speed.
`mod_sound.ls` selects cues from actual transitions and plays local WAV bytes
through `dom.audio_open`/`dom.audio_play`, with at most eight overlapping voices.
Bytes are cached after first use; playback handles open only for a cue and are
closed when finished, superseded, muted or the document closes. The local demo's
audible output follows **D7.5.4** in the
[formal design](../../../doc/Lambda_Formal_Design.md).
The seven small mono WAVs are original synthesized effects. Rebuild them with
`python3 test/demo/marble-drop/_generate_sounds.py`; Python is not used by the game.

These sixteen puzzles use original layouts.
The original shop, bonus locks and background music are outside this demo's scope.

Run the focused checks from the repository root:

```sh
mkdir -p temp/marble-drop
./lambda.exe --no-log test/demo/marble-drop/tests/marble_drop_model.ls > temp/marble-drop/model.actual.txt
diff -u test/demo/marble-drop/tests/marble_drop_model.txt temp/marble-drop/model.actual.txt
./lambda.exe demo marble-drop --headless \
  --event-file test/ui/marble_drop/gameplay.json \
  --event-result temp/marble-drop/gameplay-result.json
./lambda.exe demo marble-drop --headless \
  --event-file test/ui/marble_drop/machines.json \
  --event-result temp/marble-drop/machines-result.json
./lambda.exe demo marble-drop --headless \
  --event-file test/ui/marble_drop/compact.json \
  --event-result temp/marble-drop/compact-result.json
./lambda.exe demo marble-drop --headless \
  --event-file test/ui/marble_drop/audio.json \
  --event-result temp/marble-drop/audio-result.json
./lambda.exe demo marble-drop --headless \
  --event-file test/ui/marble_drop/devices.json \
  --event-result temp/marble-drop/devices-result.json
```

The model golden checks all sixteen solutions at par as well as device behavior,
limited stock, wrong colors, undo, pause, hints, restart, progression and sound-cue selection.
The native replays cover manual controls, keyboard routing, linked machines,
catches, color-dependent route previews, upward flights, Auto Play, completion
and a narrow window. Captures go under
`temp/marble-drop/`. The catalog registration also runs through the existing
`RadiantViewTest.DemoCommand*` cases.
The model is registered in the language golden suite. The gameplay/layout
replays mute through the normal button; the audio replay exercises all seven
effects, Auto Play, keyboard/button mute, pause, redraws and voice cleanup through native
playback. It needs access to the macOS audio service; the MIDI bus's offline
setting does not replace the native WAV backend.
Run all five registered UI fixtures together with
`./test/test_ui_automation_gtest.exe --suite marble-drop`.
