# Marble Drop

```sh
./lambda.exe demo marble-drop
```

Eight original mechanical puzzles inspired by
[Marble Drop (Maxis, 1997)](https://en.wikipedia.org/wiki/Marble_Drop).
The parchment background carries original da Vinci-style pen studies of toothed
wheels, pendulums, wings and falling spheres, with slanted margin notes and
leaders explaining each machine. Fine sepia strokes sit behind the working rails.
Choose a colored marble from the tray and click **Drop A** or **Drop B** above
the machine. Fill every pocket with its matching color. The dots beneath a
pocket show its quota. The brighter rail is the switch's current route;
every marble flips a switch after taking that route.

The puzzles introduce alternating levers, branching routes, a remote switch
link, catches that keep their first marble, and pockets needing multiple
marbles. A steel ball operates devices without filling a colored pocket.
The last three puzzles also provide one black marble, which matches any
unfinished pocket. Colored marbles are limited to the pocket quotas;
**Undo** recovers a mistaken drop, even while it is moving. **Restart** resets
the current machine. All eight puzzles are available from the numbered tabs.
Three stars mean no lost colored marbles and no more than the stated par.
Stars stay in memory while the demo is open.

**A little hint** suggests a move from the current state. **Auto Play** finds
and animates a solution using the same rules as manual play; it stops at the
end of the puzzle. It can be stopped or paused during a drop. **Next invention**
continues after a win. There is no time limit.

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
image between device changes, with a separate rolling marble overlay.
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

This demo has eight new puzzles and the core routing mechanics. The original
game's 50 puzzles, shop, bonus locks, background music and additional devices are outside
this demo's scope.

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
```

The model golden checks all eight solutions at par as well as device behavior,
limited stock, wrong colors, undo, pause, hints, restart, progression and sound-cue selection.
The native replays cover manual controls, keyboard routing, linked machines,
catches, Auto Play, completion and a narrow window. Captures go under
`temp/marble-drop/`. The catalog registration also runs through the existing
`RadiantViewTest.DemoCommand*` cases.
The model is registered in the language golden suite. The gameplay/layout
replays mute through the normal button; the audio replay exercises all seven
effects, Auto Play, keyboard/button mute, pause, redraws and voice cleanup through native
playback. It needs access to the macOS audio service; the MIDI bus's offline
setting does not replace the native WAV backend.
Run all four registered UI fixtures together with
`./test/test_ui_automation_gtest.exe --suite marble-drop`.
