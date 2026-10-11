# Superlambda

A side-scrolling platform adventure written in Lambda, with a restrained retro
palette and smooth SVG artwork. Lambda wears a red cap with **λ** on it.

From the repository root:

```sh
./lambda.exe demo superlambda
```

Click **LET'S GO** or press Enter. The meadow spans 3,840 pixels: collect coins,
bump yellow blocks from below, jump onto enemies, cross three gaps, and reach
the red λ finish flag. A gold checkpoint halfway through saves your respawn
position. Enemy contact, falling, and running out of time cost a life; you have
three lives and 240 seconds. Coins and defeated enemies survive a lost life.

Click **Auto Play** to watch a simple player walk right and jump when it sees
an obstacle, enemy, or gap ahead. It uses ordinary movement and collision rules,
collects coins, saves the checkpoint, and can reach the finish. Pause/resume
preserves Auto Play; moving or jumping takes control, and restart starts a
manual game. The finish and game-over screens also offer Auto Play.

| Control | Action |
| --- | --- |
| Left / Right, A / D | Hold to move |
| Space / Up / W | Jump; tap for a short hop, hold for a high jump |
| Shift | Hold to run faster |
| Enter / P / Escape | Start, pause, resume |
| R | Restart the whole course |

The buttons below the scene also support holding to move, jump, or run.
A collected coin or coin block earns 100 points, stomping an enemy earns 200,
and reaching the flag earns 1,000. There is one complete level, with win and
game-over screens. The scene uses a fixed 880 × 480 viewport; smaller windows
can scroll the surrounding page.

`superlambda_core.ls` contains pure collision, movement, scoring, patrol, and
life transitions. State lives in the view instance under **S9.1.4**; rendering
is pure and handlers perform mutation under **S12.1.3**. A document-owned CSS
clock emits one physics step every 40 ms, outside the changing game subtree.
The renderer culls distant objects and uses local SVG assets, including a
three-pose hero sheet. No JavaScript or remote assets are needed by the game.

Run the rules and interaction regressions:

```sh
mkdir -p temp
./lambda.exe test/demo/superlambda/superlambda_test.ls > temp/superlambda_test.actual.txt
diff -u test/demo/superlambda/superlambda_test.txt temp/superlambda_test.actual.txt
./lambda.exe view test/demo/superlambda/superlambda.ls --headless --no-log \
  --event-file test/demo/superlambda/superlambda_smoke.json \
  --event-result temp/superlambda_smoke_result.json
./lambda.exe view test/demo/superlambda/superlambda.ls --headless --no-log \
  --event-file test/demo/superlambda/superlambda_course.json \
  --event-result temp/superlambda_course_result.json
```

The 56 rules assertions cover physics, short taps, landing, walls, head bumps,
coins, patrols, stomps, damage, checkpoint respawns, timer expiry, and a complete
traversal using ordinary controls. The interactive replay checks keyboard
press/release, pointer controls, pause, restart, sustained animation, and window
close. It is registered in `test/ui/superlambda_smoke.json`.
The course replay reaches the checkpoint and finish flag, then starts another
round. Its waits account for the event harness's 60 Hz input-task turns as well
as explicit time advances.

`test/ui/superlambda_autoplay.json` additionally runs the automatic player to
the finish and checks pause/resume, manual takeover, restart, and close.

Radiant resolves both keyboard phases against live focus or the document body,
so **S12.1.3** redraws cannot swallow a release after retiring the focused node.
`test/ui/keyboard_keyup_body_fallback.json` separately covers that engine path.

Visual direction: [the supplied Craiyon reference](https://www.craiyon.com/en/image/JOMP3bAxTRaKtFaOhYb0OQ).
All SVG artwork in `assets/` was drawn for this demo.

For performance measurements, build the optimized host with `make release`.
Final measurements used an isolated release executable under `temp/`, so
concurrent debug builds could not replace the benchmark host.
The physics clock runs at 25 Hz. Radiant resolves SVG font metrics only for
`ex` lengths and uses geometric clip intersections for sprite batches, avoiding
repeated font-directory scans and full-surface alpha-mask composites.
The SVG resource lifetime follows **D4.2.2v2–D4.2.6/D4.5.1v4**.

A release replay of 240 separately painted frames at 1024 × 800 took 58.52 s
before these changes and a median 5.88 s after them (three runs, about 10.0×
faster). Each frame used a separate `advance_time` event of 40 ms and one step;
a single event containing many steps only paints the final frame and is not a
rendering benchmark. These timings include startup and replay overhead and
are not a claim of 60 Hz gameplay. Logs and captures belong under `temp/`.

A 10-second windowed release replay averaged 4.2 rendered frames/s before and
44.1 after; median render time fell from 228.88 ms to 15.30 ms. The window can
paint between physics steps, so rendered frames/s and the 25 Hz game clock are
different measurements. Both windowed replays closed successfully.
