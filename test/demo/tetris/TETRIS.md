# Tetris in Lambda

From the repository root, open the native Radiant window:

```sh
./lambda.exe view test/demo/tetris/tetris.ls
```

Click **Start game** or focus the game and press Enter. All controls also have
clickable buttons.

| Key | Action |
| --- | --- |
| Left / Right, A / D | Move |
| Up / X / W | Rotate clockwise |
| Z | Rotate counterclockwise |
| Down / S | Soft drop (1 point per cell) |
| Space | Hard drop (2 points per cell) |
| C / Shift | Hold; once per locked piece |
| P / Escape / Enter | Start, pause, resume |
| R | Restart with a fresh bag |

The game has a 10 × 20 board, all seven tetrominoes, SRS rotation kicks, a ghost
piece, three next-piece previews, hold, spawn/ceiling top-out, and a seeded
seven-bag generator. Clearing 1/2/3/4 lines earns 100/300/500/800 points times the
current level. Gravity starts at 700 ms per row. Each ten lines advances a level
and speeds up gravity, down to 100 ms per row. Pieces lock when a downward move
is blocked; there is no lock delay, T-spin bonus, or persistent high score.
The initial seed is 42; restart
continues from the generator's current seed.

`tetris_core.ls` contains pure rules, `tetris.ls` the reactive view, and
`tetris.css` the presentation. State belongs to the view instance (S9.1.4),
with pure rendering and mutation in event handlers (S12.1.3). A repeating CSS
animation supplies document-owned gravity events; no JavaScript game code or
external assets are required. Its clock lives in a stable shell outside the
changing board, so moving or rotating a piece does not restart gravity.

Run the rules regression and interactive replay:

```sh
./lambda.exe test/demo/tetris/tetris_test.ls > temp/tetris_test.actual.txt
diff -u test/demo/tetris/tetris_test.txt temp/tetris_test.actual.txt
./lambda.exe view test/demo/tetris/tetris.ls --headless --no-log \
  --event-file test/demo/tetris/tetris_smoke.json \
  --event-result temp/tetris_smoke_result.json
```

Create `temp/` first if it does not exist. `tetris_test.txt` is the expected
result; every boolean must be true. The 48 rules assertions cover bags, kicks,
collision, line clearing, scoring, hold, and top-out. The 17 interactive
assertions check keyboard movement, rotation, scoring, hold, uninterrupted
gravity, pause/resume, and restart. The replay is also registered under
`test/ui/` for `make test-radiant-baseline`.
