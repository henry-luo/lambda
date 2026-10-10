// Pure Tetris rules; UI state belongs to its view instance (S9.1.4, S12.1.3).
let WIDTH = 10
let HEIGHT = 20
let SHAPES = [
  [[0, 1], [1, 1], [2, 1], [3, 1]], // I
  [[1, 0], [2, 0], [1, 1], [2, 1]], // O
  [[1, 0], [0, 1], [1, 1], [2, 1]], // T
  [[1, 0], [2, 0], [0, 1], [1, 1]], // S
  [[0, 0], [1, 0], [1, 1], [2, 1]], // Z
  [[0, 0], [0, 1], [1, 1], [2, 1]], // J
  [[2, 0], [0, 1], [1, 1], [2, 1]]  // L
]
// SRS clockwise kick offsets, with screen coordinates increasing downward.
let NORMAL_KICKS = [
  [[0, 0], [-1, 0], [-1, -1], [0, 2], [-1, 2]],
  [[0, 0], [1, 0], [1, 1], [0, -2], [1, -2]],
  [[0, 0], [1, 0], [1, -1], [0, 2], [1, 2]],
  [[0, 0], [-1, 0], [-1, 1], [0, -2], [-1, -2]]
]
let I_KICKS = [
  [[0, 0], [-2, 0], [1, 0], [-2, 1], [1, -2]],
  [[0, 0], [-1, 0], [2, 0], [-1, -2], [2, 1]],
  [[0, 0], [2, 0], [-1, 0], [2, -1], [-1, 2]],
  [[0, 0], [1, 0], [-2, 0], [1, 2], [-2, -1]]
]

pub fn empty_board() => [for (i in 0 to (WIDTH * HEIGHT - 1)) 0]
pub fn spawn(kind) => {kind: kind, rotation: 0, x: 3, y: 0}
fn rotate_cell(cell, size, turns) {
  if (turns == 0) cell
  else rotate_cell([size - 1 - cell[1], cell[0]], size, turns - 1)
}
pub fn cells(piece) => [for (cell in SHAPES[piece.kind],
  let offset = if (piece.kind == 1) cell
    else rotate_cell(cell, if (piece.kind == 0) 4 else 3, piece.rotation))
  [piece.x + offset[0], piece.y + offset[1]]]

pub fn fits(board, piece) => len([for (cell in cells(piece)
  where cell[0] < 0 or cell[0] >= WIDTH or cell[1] >= HEIGHT or
    (cell[1] >= 0 and board[cell[1] * WIDTH + cell[0]] != 0)) cell]) == 0

pub fn move(board, piece, dx, dy) {
  let candidate = {*:piece, x: piece.x + dx, y: piece.y + dy}
  if (fits(board, candidate)) candidate else piece
}
pub fn rotate_piece(board, piece, direction) {
  let turn = (piece.rotation + direction + 4) % 4
  let table = if (piece.kind == 0) I_KICKS else NORMAL_KICKS
  // Counterclockwise transitions reverse the corresponding clockwise kicks.
  let offsets = if (direction == 1) table[piece.rotation]
    else [for (offset in table[turn]) [-offset[0], -offset[1]]]
  let candidates = [for (offset in offsets,
    let candidate = {*:piece, rotation: turn,
      x: piece.x + offset[0], y: piece.y + offset[1]}
    where fits(board, candidate))
    candidate]
  if (piece.kind == 1 or len(candidates) == 0) piece else candidates[0]
}
pub fn landing(board, piece) {
  let below = {*:piece, y: piece.y + 1}
  if (fits(board, below)) landing(board, below) else piece
}

fn shuffled(seed, remaining) {
  if (len(remaining) == 0) {bag: [], seed: seed}
  else {
    let random = math.random(seed)
    let index = int(floor(random[0] * len(remaining)))
    let rest = shuffled(random[1], [for (i in 0 to (len(remaining) - 1)
      where i != index) remaining[i]])
    {bag: [remaining[index], *rest.bag], seed: rest.seed}
  }
}
pub fn seven_bag(seed) => shuffled(seed, [0, 1, 2, 3, 4, 5, 6])
fn refill(game) {
  if (len(game.queue) >= 4) game
  else {
    let fresh = seven_bag(game.seed)
    {*:game, queue: [*game.queue, *fresh.bag], seed: fresh.seed}
  }
}
pub fn new_game(seed) {
  let fresh = seven_bag(seed)
  {board: empty_board(), piece: spawn(fresh.bag[0]),
    queue: slice(fresh.bag, 1, 7), seed: fresh.seed,
    held: -1, can_hold: true, score: 0, lines: 0, level: 1,
    mode: "ready", last_clear: 0, autoplay: false}
}
fn next_piece(game) {
  let next = refill({*:game, piece: spawn(game.queue[0]),
    queue: slice(game.queue, 1, len(game.queue)), can_hold: true})
  if (fits(next.board, next.piece)) next else {*:next, mode: "over"}
}

pub fn clear_rows(board) {
  let rows = [for (y in 0 to (HEIGHT - 1),
    let row = slice(board, y * WIDTH, (y + 1) * WIDTH)
    where len([for (cell in row where cell == 0) cell]) > 0)
    row]
  let cleared_count = HEIGHT - len(rows)
  {board: [for (i in 0 to (cleared_count * WIDTH - 1)) 0, for (row in rows) *row], count: cleared_count}
}
// Locking and Auto Play previews share one merge of the four occupied cells.
fn board_with_piece(board, piece) {
  let indices = [for (cell in cells(piece)) cell[1] * WIDTH + cell[0]];
  [for (i in 0 to (WIDTH * HEIGHT - 1))
    if (contains(indices, i)) piece.kind + 1 else board[i]]
}
pub fn lock(game) {
  // A kicked piece above the ceiling cannot silently lose its hidden cells.
  if (len([for (cell in cells(game.piece) where cell[1] < 0) cell]) > 0) {
    {*:game, mode: "over"}
  } else {
    let merged = board_with_piece(game.board, game.piece)
    let cleared = clear_rows(merged)
    let lines = game.lines + cleared.count
    next_piece({*:game, board: cleared.board, lines: lines,
      level: 1 + (lines div 10), last_clear: cleared.count,
      score: game.score + [0, 100, 300, 500, 800][cleared.count] * game.level})
  }
}
fn down(game, points) {
  let candidate = move(game.board, game.piece, 0, 1)
  if (candidate.y == game.piece.y) lock(game)
  else {*:game, piece: candidate, score: game.score + points}
}
fn hold(game) {
  if (not game.can_hold) game
  else {
    let swapped = if (game.held < 0) next_piece(game)
      else {*:game, piece: spawn(game.held)}
    let next = {*:swapped, held: game.piece.kind, can_hold: false}
    if (fits(next.board, next.piece)) next else {*:next, mode: "over"}
  }
}
pub fn gravity_ms(level) => max(100, 700 - (level - 1) * 65)
pub fn action(game, command) {
  if (command == "restart" or command == "auto")
    {*:new_game(game.seed), mode: "playing", autoplay: command == "auto"}
  else if (command == "pause") {
    if (game.mode == "playing") {*:game, mode: "paused"}
    else if (game.mode == "ready" or game.mode == "paused") {*:game, mode: "playing"}
    else game
  }
  else if (game.mode != "playing") game
  else if (command == "left" or command == "right") {
    {*:game, piece: move(game.board, game.piece, if (command == "left") -1 else 1, 0)}
  }
  else if (command == "cw" or command == "ccw") {
    {*:game, piece: rotate_piece(game.board, game.piece, if (command == "cw") 1 else -1)}
  }
  else if (command == "tick" or command == "down") down(game, if (command == "down") 1 else 0)
  else if (command == "drop") {
    let ghost = landing(game.board, game.piece)
    lock({*:game, piece: ghost, score: game.score + 2 * (ghost.y - game.piece.y)})
  }
  else if (command == "hold") hold(game)
  else game
}

// Prefer low, even stacks without buried holes, rewarding completed rows.
fn placement_cost(game, piece) {
  let placed = clear_rows(board_with_piece(game.board, landing(game.board, piece)))
  let heights = [for (x in 0 to (WIDTH - 1))
    max([0, for (y in 0 to (HEIGHT - 1) where placed.board[y * WIDTH + x] != 0) HEIGHT - y])]
  let holes = len([for (x in 0 to (WIDTH - 1), y in 0 to (HEIGHT - 1)
    where y >= HEIGHT - heights[x] and placed.board[y * WIDTH + x] == 0) y])
  sum(heights) + 8 * holes + 2 * sum([for (x in 1 to (WIDTH - 1)) abs(heights[x] - heights[x - 1])]) -
    12 * placed.count + (if (not fits(placed.board, spawn(game.queue[0]))) 10000 else 0)
}
pub fn auto_plan(game) {
  let choices = [for (turns in 0 to 3, x in -3 to (WIDTH - 1),
    let rotated = reduce([game.piece, *[for (i in 1 to turns) i]],
      (piece, unused) => rotate_piece(game.board, piece, 1)),
    let candidate = {*:rotated, x: x}
    // Every intermediate horizontal position must be reachable by normal moves.
    where all([for (cx in min(x, rotated.x) to max(x, rotated.x)) fits(game.board, {*:rotated, x: cx})]))
    {turns: turns, x: x, cost: placement_cost(game, candidate) + 0.01 * (turns + abs(x - rotated.x))}]
  sort(choices, (choice) => choice.cost)[0]
}
pub fn auto_step(game, previous_plan = null) {
  if (game.mode != "playing") {game: game, plan: previous_plan}
  else {
    let plan = previous_plan or auto_plan(game)
    let command = if (plan == null) "drop" else if (plan.turns > 0) "cw"
      else if (game.piece.x < plan.x) "right" else if (game.piece.x > plan.x) "left" else "drop"
    {game: action(game, command), plan: if (command == "drop") null
      else if (command == "cw") {*:plan, turns: plan.turns - 1} else plan}
  }
}
