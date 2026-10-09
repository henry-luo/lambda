# DOOM in Lambda Script

**Status:** P0–P5 implemented: all nine maps, gameplay, keyboard/mouse input,
weapons/enemies, animation, effects, macOS audio and spectator cameras run in
Lambda Script/Radiant. Deterministic rules, native walkthrough/combat/death,
episode controls and browser/native visual references are verified. P6 remains
open for final release performance, sustained-memory and baseline delivery checks;
work was stopped at the user's wrap-up request on 2026-10-09.

The [implementation plan](../../../vibe/impl/Lambda_Impl_Doom.md) defines a native
Lambda Script port of [cssDOOM](https://github.com/NielsLeenheer/cssDOOM). Gameplay,
input, scene construction, HUD and lifecycle are `.ls` modules; Radiant
renders the CSS 3D scene. No JavaScript application, npm build or browser runtime
is required by the demo.

Game state belongs to a view instance and effects run in event handlers, following
**S9.1.7** and **S12.1.3** in the
[formal semantics](../../../doc/Lambda_Formal_Semantics.md).

Game code, maps, assets, tools, tests, input replays and recorded references belong
in this directory. Generic engine fixes remain in their owning Lambda/Radiant
modules. `upstream.json` records the pinned reference; `data/resources.json`
records file hashes, original paths and provenance. See `ATTRIBUTION.md` for
upstream credits and the separate resource provenance.

Verify the imported files and deterministic rules from the repository root:

```sh
python3 test/demo/doom/tools/import_upstream.py --verify
./lambda.exe test/demo/doom/tools/verify_maps.ls
./test/test_lambda_gtest.exe --gtest_filter='DoomDemoTests.*:*core_test*:*physics_test*'
```

Open `doom.ls` from the repository root with:

```sh
./lambda.exe view test/demo/doom/doom.ls
```

Press Start, then W/S to move, A/D to strafe, arrows to turn, Shift to run,
E/Space to use, Control or mouse button 1 to fire, and 1–6 to select a weapon.
P/Escape pauses and R restarts in player mode. Player mode captures relative mouse movement;
pause or window blur releases capture. Tab cycles player, follow and top views.
In top view, W/A/S/D pans, Q/E rotates, R/F or the wheel zooms, and dragging pans.
Use the map and difficulty selectors, then Load map, to visit any of the nine
bundled maps at one of five difficulty levels. Restart preserves the active
difficulty; episode transitions preserve difficulty and inventory.
Temporary results and captures go in `./temp/doom/`.

Native sound currently uses AVAudioPlayer on macOS. Other hosts report an absent
audio backend visibly. Sound bytes are loaded by Lambda, while native voices are
owned by the document and closed on teardown (**D7.1.2v2**, **D4.5.1v4**).

The native replays cover the E1M1 walkthrough, combat/pickups and exit to E1M2,
natural enemy damage through death/restart, every episode map and spectator input:

```sh
./lambda.exe view test/demo/doom/doom.ls --headless \
  --event-file test/demo/doom/replay/e1m1_exit.json \
  --event-result temp/doom/exit-result.json
./test/test_radiant_view_gtest.exe --gtest_filter='RadiantViewTest.Doom*'
```

`tools/generate_replay.py` derives exit/death checkpoint values from the same
fixed-step simulation and recorded control routes. These replays set
`input_turn_ms: 0` so explicit time advances determine the simulation budget.
Event `wait` and `advance_time` use virtual time; performance measurements use
the real native timer and `replay/performance.json` instead.

Reimport resources and derive image sizes, sheet layouts and source hashes from
a clean checkout of the pinned upstream revision:

```sh
python3 test/demo/doom/tools/import_upstream.py ./temp/cssdoom-upstream
```

Python and Node are offline import tools. Neither runs inside the demo.
