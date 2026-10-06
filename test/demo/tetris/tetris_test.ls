import game: .tetris_core

let board = game.empty_board()
let initial = game.new_game(42)
let playing = game.action(initial, "pause")
let paused = game.action(playing, "pause")
let bag = game.seven_bag(42)
{
  board_size: len(board) == 200,
  board_empty: sum(board) == 0,
  bag_unique: len(unique(bag.bag)) == 7,
  bag_range: sort(bag.bag) == [0, 1, 2, 3, 4, 5, 6],
  reproducible: bag == game.seven_bag(42),
  ready: initial.mode == "ready",
  start: playing.mode == "playing",
  pause: paused.mode == "paused",
  paused_input: game.action(paused, "drop") == paused,
  paused_tick: game.action(paused, "tick") == paused,
  resume: game.action(paused, "pause").mode == "playing"
}

let tee = game.spawn(2)
let left = {*:tee, x: -1}
let bottom = game.landing(board, tee)
let wall = {*:tee, rotation: 1, x: -1}
let kicked = game.rotate_piece(board, wall, -1)
let floor_piece = {*:game.spawn(0), y: 18}
let floor_kick = game.rotate_piece(board, floor_piece, 1)
let cycled = game.rotate_piece(board, game.rotate_piece(board,
  game.rotate_piece(board, game.rotate_piece(board, tee, 1), 1), 1), 1)
{
  four_cells: len(game.cells(tee)) == 4,
  fits_spawn: game.fits(board, tee),
  rejects_wall: not game.fits(board, left),
  blocked_move: game.move(board, tee, -5, 0) == tee,
  moves: game.move(board, tee, 1, 0).x == 4,
  ghost_bottom: bottom.y == 18,
  ghost_fits: game.fits(board, bottom),
  ghost_stops: not game.fits(board, {*:bottom, y: bottom.y + 1}),
  rotation_cycle: cycled == tee,
  wall_kick: kicked.x == 0 and kicked.rotation == 0,
  floor_kick: floor_kick.y == 16 and game.fits(board, floor_kick),
  square_fixed: game.rotate_piece(board, game.spawn(1), 1) == game.spawn(1)
}

// Preserve a partial row and clear separated full rows in one compaction.
let stacked = [for (i in 0 to 199)
  if (i div 10 == 17 or i div 10 == 19) 3 else if (i == 180) 6 else 0]
let cleared = game.clear_rows(stacked)
let empty_clear = game.clear_rows(board)
let well = [for (i in 0 to 199) if (i >= 160 and i % 10 != 5) 2 else 0]
let vertical = {*:game.spawn(0), rotation: 1, x: 3, y: 16}
let tetris = game.lock({*:playing, board: well, piece: vertical, lines: 9})
{
  clears_two: cleared.count == 2,
  compact_size: len(cleared.board) == 200,
  compact_order: cleared.board[190] == 6 and sum(cleared.board) == 6,
  no_clear: empty_clear.count == 0 and empty_clear.board == board,
  clears_four: tetris.last_clear == 4 and tetris.lines == 13,
  clear_score: tetris.score == 800,
  level_up: tetris.level == 2,
  cleared_empty: sum(tetris.board) == 0,
  gravity: game.gravity_ms(2) < game.gravity_ms(1) and game.gravity_ms(100) == 100
}

let held = game.action(playing, "hold")
let dropped = game.action(held, "drop")
let swapped = game.action(dropped, "hold")
let soft = game.action(playing, "down")
let hard = game.action(playing, "drop")
let ceiling = [for (i in 0 to 199) if (i < 20 and i % 10 >= 3 and i % 10 <= 6) 1 else 0]
let topped = game.lock({*:playing, board: ceiling, piece: game.landing(board, tee)})
let hidden = game.lock({*:playing, piece: {*:tee, y: -1}})
let restarted = game.action(topped, "restart")
{
  holds_current: held.held == playing.piece.kind,
  hold_advances: held.piece.kind == playing.queue[0],
  hold_once: game.action(held, "hold") == held,
  lock_resets_hold: dropped.can_hold,
  swaps_hold: swapped.piece.kind == held.held and swapped.held == dropped.piece.kind,
  soft_score: soft.score == 1 and soft.piece.y == playing.piece.y + 1,
  hard_score: hard.score == 2 * (game.landing(board, playing.piece).y - playing.piece.y),
  locks_four: len([for (cell in hard.board where cell != 0) cell]) == 4,
  next_advances: hard.piece.kind == playing.queue[0],
  preview_refills: len(dropped.queue) >= 4,
  spawn_topout: topped.mode == "over",
  hidden_topout: hidden.mode == "over",
  over_input: game.action(topped, "left") == topped,
  restart: restarted.mode == "playing" and restarted.score == 0 and sum(restarted.board) == 0,
  restart_fresh: restarted.seed != initial.seed,
  snapshot: sum(playing.board) == 0 and initial.mode == "ready"
}
