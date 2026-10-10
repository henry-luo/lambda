// Pure world transitions; mutable state belongs to the view (S9.1.4, S12.1.3).
pub let VIEW_WIDTH = 880
pub let WORLD_WIDTH = 3840
pub let HERO_WIDTH = 28
pub let HERO_HEIGHT = 40
pub let STEP_MS = 40
pub let GOAL_X = 3580
pub let CHECKPOINT_X = 1880

fn box(id, kind, x, y, w, h) => {id: id, kind: kind, x: x, y: y, w: w, h: h}
pub let TERRAIN = [
  box(0, "ground", 0, 400, 980, 80),
  box(1, "ground", 1100, 400, 580, 80),
  box(2, "ground", 1810, 400, 900, 80),
  box(3, "ground", 2830, 400, 1010, 80),
  box(4, "pipe", 740, 336, 64, 64),
  box(5, "pipe", 1410, 304, 64, 96),
  box(6, "pipe", 2240, 336, 64, 64),
  box(7, "pipe", 3100, 304, 64, 96),
  box(8, "ledge", 995, 324, 88, 24),
  box(9, "ledge", 1710, 310, 72, 24),
  box(10, "ledge", 2735, 320, 72, 24)
]
pub let BLOCKS = [
  box(20, "brick", 280, 280, 32, 32),
  box(21, "question", 312, 280, 32, 32),
  box(22, "brick", 344, 280, 32, 32),
  box(23, "question", 376, 280, 32, 32),
  box(24, "question", 560, 232, 32, 32),
  box(25, "brick", 1250, 264, 32, 32),
  box(26, "question", 1282, 264, 32, 32),
  box(27, "question", 2000, 280, 32, 32),
  box(28, "brick", 2032, 280, 32, 32),
  box(29, "question", 2460, 248, 32, 32),
  box(30, "brick", 2492, 248, 32, 32),
  box(31, "question", 3250, 264, 32, 32)
]
fn coin(id, x, y) => box(id, "coin", x, y, 20, 26)
pub let COINS = [
  coin(0, 210, 352), coin(1, 250, 352), coin(2, 318, 240),
  coin(3, 384, 240), coin(4, 566, 190), coin(5, 690, 300),
  coin(6, 760, 290), coin(7, 1018, 280), coin(8, 1200, 350),
  coin(9, 1290, 220), coin(10, 1430, 258), coin(11, 1735, 266),
  coin(12, 1940, 352), coin(13, 2006, 236), coin(14, 2160, 300),
  coin(15, 2260, 290), coin(16, 2470, 206), coin(17, 2760, 276),
  coin(18, 3020, 300), coin(19, 3120, 258), coin(20, 3260, 220),
  coin(21, 3400, 350), coin(22, 3450, 350), coin(23, 3500, 350)
]
fn enemy(id, kind, x, lo, hi) =>
  {*:box(id, kind, x, 370, 32, 30), lo: lo, hi: hi, direction: -1, alive: true}
pub let ENEMIES = [
  enemy(0, "grub", 520, 440, 650),
  enemy(1, "turtle", 880, 825, 940),
  enemy(2, "grub", 1330, 1160, 1360),
  enemy(3, "turtle", 2130, 1980, 2190),
  enemy(4, "grub", 2520, 2350, 2620),
  enemy(5, "turtle", 3000, 2880, 3050),
  enemy(6, "grub", 3420, 3300, 3500)
]
pub fn hero_at(x) => {x: x, y: 360.0, vx: 0.0, vy: 0.0, grounded: true,
  facing: 1, coyote: 4, buffer: 0}
pub fn new_game() => {hero: hero_at(72.0), mode: "ready", keys: [], pointer: false, autoplay: false,
  enemies: ENEMIES, collected: [], used: [], coins: 0, score: 0,
  lives: 3, checkpoint: false, invincible: 0, ticks: 0, camera: 0.0}
pub fn camera_x(x) => max(0.0, min(WORLD_WIDTH - VIEW_WIDTH, x - 300.0))
pub fn time_left(game) => max(0, 240 - game.ticks div 25)
pub fn overlap(a, b) => a.x < b.x + b.w and a.x + a.w > b.x and
  a.y < b.y + b.h and a.y + a.h > b.y
fn hero_box(hero) => {*:hero, w: HERO_WIDTH, h: HERO_HEIGHT}
fn vertical_overlap(hero, solid) => hero.y < solid.y + solid.h and
  hero.y + HERO_HEIGHT > solid.y
fn horizontal_overlap(hero, solid) => hero.x < solid.x + solid.w and
  hero.x + HERO_WIDTH > solid.x

// Resolve crossed faces separately, so landing and head bumps cannot tunnel.
fn horizontal(hero, solids) {
  let next_x = max(0.0, min(WORLD_WIDTH - HERO_WIDTH, hero.x + hero.vx))
  let hits = [for (s in solids where vertical_overlap(hero, s) and
    (if (hero.vx > 0) hero.x + HERO_WIDTH <= s.x and next_x + HERO_WIDTH > s.x
     else hero.x >= s.x + s.w and next_x < s.x + s.w))
    if (hero.vx > 0) s.x - HERO_WIDTH else s.x + s.w]
  let x = if (len(hits) == 0) next_x else if (hero.vx > 0) min(hits) else max(hits)
  {*:hero, x: x, vx: if (len(hits) == 0) hero.vx else 0.0}
}
fn vertical(hero, solids) {
  let next_y = hero.y + hero.vy
  let hits = [for (s in solids where horizontal_overlap(hero, s) and
    (if (hero.vy >= 0) hero.y + HERO_HEIGHT <= s.y and next_y + HERO_HEIGHT >= s.y
     else hero.y >= s.y + s.h and next_y <= s.y + s.h)) s]
  let y = if (len(hits) == 0) next_y
    else if (hero.vy >= 0) min([for (s in hits) s.y]) - HERO_HEIGHT
    else max([for (s in hits) s.y + s.h])
  {hero: {*:hero, y: y, vy: if (len(hits) > 0) 0.0 else hero.vy,
    grounded: len(hits) > 0 and hero.vy >= 0},
   bumped: [for (s in hits where hero.vy < 0 and s.kind == "question" and s.y + s.h == y) s.id]}
}
fn patrol(enemy) {
  if (not enemy.alive) enemy
  else {
    let x = enemy.x + enemy.direction * (if (enemy.kind == "turtle") 1.7 else 1.25)
    {*:enemy, x: max(enemy.lo, min(enemy.hi, x)),
      direction: if (x <= enemy.lo) 1 else if (x >= enemy.hi) -1 else enemy.direction}
  }
}
pub fn lose_life(game) {
  let hero = hero_at(if (game.checkpoint) CHECKPOINT_X else 72.0)
  {*:game, hero: hero, camera: camera_x(hero.x), lives: max(0, game.lives - 1),
    mode: if (game.lives <= 1) "over" else "playing", keys: [], pointer: false, invincible: 50,
    ticks: if (time_left(game) == 0) 0 else game.ticks}
}
pub fn action(game, command) {
  if (command == "restart" or command == "auto")
    {*:new_game(), mode: "playing", autoplay: command == "auto"}
  else if (command == "pause") {
    if (game.mode == "ready" or game.mode == "paused") {*:game, mode: "playing", keys: [], pointer: false}
    else if (game.mode == "playing") {*:game, mode: "paused", keys: [], pointer: false}
    else game
  }
  else game
}
pub fn release_controls(game) => {*:game, keys: [], pointer: false,
  hero: {*:game.hero, vy: max(-5.0, game.hero.vy)}}
pub fn set_control(game, command, pressed) {
  let already = contains(game.keys, command)
  let keys = if (pressed) [*game.keys, command] else [for (k in game.keys where k != command) k]
  if (not contains(["left", "right", "jump", "run", "pause", "restart"], command) or already == pressed) game
  else if (pressed and (command == "pause" or command == "restart")) {
    {*:action(game, command), keys: [command]}
  }
  else if (game.mode != "playing") {*:game, keys: if (pressed) game.keys else keys}
  else {
    let hero = if (command != "jump") game.hero
      else if (pressed) {*:game.hero, buffer: 5}
      // A quick tap still queues a hop even if released before the next frame.
      else {*:game.hero, vy: max(-5.0, game.hero.vy)}
    {*:game, hero: hero, keys: keys}
  }
}
// Look ahead for a wall, enemy or unsupported ground; hold each hop until landing.
pub fn auto_controls(game) {
  let h = game.hero
  let front = h.x + HERO_WIDTH
  let feet = h.y + HERO_HEIGHT
  let obstacles = any([for (s in [*TERRAIN, *BLOCKS])
    s.x >= front and s.x < front + 65 and s.y < feet and s.y + s.h > h.y])
  let enemies = any([for (e in game.enemies) e.alive and e.x + e.w > h.x and
    e.x < front + 90 and abs(e.y - h.y) < HERO_HEIGHT + 30])
  let supported = any([for (s in [*TERRAIN, *BLOCKS])
    s.x <= front + 65 and s.x + s.w > front + 65 and s.y <= feet and s.y + s.h >= feet])
  let jump = if (h.grounded) obstacles or enemies or not supported
    else contains(game.keys, "jump")
  let moving = set_control(game, "right", true)
  let released = if (h.grounded) set_control(moving, "jump", false) else moving
  set_control(released, "jump", jump)
}
pub fn tick(game) {
  if (game.mode != "playing") game
  else {
    let h = game.hero
    let direction = (if (contains(game.keys, "right")) 1 else 0) -
      (if (contains(game.keys, "left")) 1 else 0)
    let speed = if (contains(game.keys, "run")) 9.0 else 6.5
    let velocity = if (direction == 0) h.vx * 0.65
      else max(-speed, min(speed, h.vx + direction * 1.4))
    let can_jump = h.buffer > 0 and (h.grounded or h.coyote > 0)
    let moving = {*:h, vx: if (abs(velocity) < 0.1) 0.0 else velocity,
      vy: min(15.0, (if (can_jump) (if (contains(game.keys, "jump")) -15.2 else -9.0) else h.vy) + 0.86),
      facing: if (direction == 0) h.facing else direction,
      coyote: if (can_jump) 0 else if (h.grounded) 4 else max(0, h.coyote - 1),
      buffer: if (can_jump) 0 else max(0, h.buffer - 1)}
    let resolved = vertical(horizontal(moving, [*TERRAIN, *BLOCKS]), [*TERRAIN, *BLOCKS])
    let hero = resolved.hero
    let gained = [for (c in COINS where not contains(game.collected, c.id) and overlap(hero_box(hero), c)) c.id]
    let bumped = [for (id in resolved.bumped where not contains(game.used, id)) id]
    let enemies = [for (e in game.enemies) patrol(e)]
    let stomps = [for (e in enemies where e.alive and hero.vy >= 0 and
      h.y + HERO_HEIGHT <= e.y + 10 and overlap(hero_box(hero), e)) e.id]
    let danger = len([for (e in enemies where e.alive and not contains(stomps, e.id) and
      overlap(hero_box(hero), e)) e.id]) > 0
    let next_hero = if (len(stomps) == 0) hero else
      {*:hero, y: min([for (e in enemies where contains(stomps, e.id)) e.y]) - HERO_HEIGHT,
        vy: -10.0, grounded: false, coyote: 0}
    let next = {*:game, hero: next_hero, ticks: game.ticks + 1,
      camera: camera_x(next_hero.x), invincible: max(0, game.invincible - 1),
      collected: [*game.collected, *gained], used: [*game.used, *bumped],
      coins: game.coins + len(gained) + len(bumped),
      score: game.score + 100 * (len(gained) + len(bumped)) + 200 * len(stomps),
      checkpoint: game.checkpoint or hero.x >= CHECKPOINT_X,
      enemies: [for (e in enemies) if (contains(stomps, e.id)) {*:e, alive: false} else e]}
    if (hero.y > 480 or time_left(next) == 0 or (danger and game.invincible == 0)) lose_life(next)
    else if (hero.x >= GOAL_X) {*:next, mode: "won", keys: [], score: next.score + 1000}
    else next
  }
}
