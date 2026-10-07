import world: .superlambda_core

fn frames(game, remaining) => if (remaining <= 0) game else frames(world.tick(game), remaining - 1)
fn at(game, x, y, vx, vy, grounded) => {*:game,
  hero: {*:game.hero, x: x, y: y, vx: vx, vy: vy, grounded: grounded, coyote: 0}}

// Traverse the actual course through normal controls, including recovery after a hit.
fn play_course(game, remaining) {
  if (remaining <= 0 or game.mode != "playing") game
  else {
    let h = game.hero
    let obstacles = [for (s in world.TERRAIN where
      (s.kind == "pipe" and s.x >= h.x + world.HERO_WIDTH and s.x - h.x < 50) or
      (h.grounded and abs(h.y + world.HERO_HEIGHT - s.y) < 1 and
       s.x + s.w < world.WORLD_WIDTH and s.x + s.w - h.x < 70)) s]
    let enemies = [for (e in game.enemies where e.alive and e.x + e.w > h.x and
      e.x - h.x < 100 and h.y + world.HERO_HEIGHT > e.y) e]
    let held = contains(game.keys, "jump")
    let jump = if (h.grounded) not held and (len(obstacles) > 0 or len(enemies) > 0) else held
    let directed = world.set_control(world.set_control(game, "right", true), "jump", jump)
    play_course(world.tick(directed), remaining - 1)
  }
}
let initial = world.new_game()
let playing = world.action(initial, "pause")
let right = world.set_control(playing, "right", true)
let walking = frames(right, 12)
let released = frames(world.set_control(walking, "right", false), 10)
let running = frames(world.set_control(right, "run", true), 12)
let paused = world.action(walking, "pause")
{
  starts_ready: initial.mode == "ready" and initial.lives == 3,
  ready_frozen: world.tick(initial) == initial,
  starts_playing: playing.mode == "playing",
  horizontal_acceleration: walking.hero.x > 120 and walking.hero.vx == 6.5,
  run_faster: running.hero.x > walking.hero.x and running.hero.vx == 9.0,
  release_stops: released.hero.vx == 0.0,
  pause: paused.mode == "paused" and len(paused.keys) == 0,
  pause_freezes_world: frames(paused, 100) == paused,
  resume: world.action(paused, "pause").mode == "playing",
  key_repeat_idempotent: world.set_control(right, "right", true) == right,
  simultaneous_directions: frames(world.set_control(right, "left", true), 4).hero.x == 72.0,
  boundary_left: frames(world.set_control(playing, "left", true), 30).hero.x == 0.0,
  initial_snapshot: initial.hero.x == 72.0 and initial.mode == "ready"
}

let jumping = world.set_control(playing, "jump", true)
let tap = world.tick(world.set_control(jumping, "jump", false))
let apex = frames(jumping, 17)
let short_hop = frames(world.set_control(world.tick(jumping), "jump", false), 16)
let pointer_hop = world.release_controls(world.tick(jumping))
let landed = frames(jumping, 40)
let wall = frames(world.set_control(at(playing, 700.0, 360.0, 0.0, 0.0, true), "right", true), 16)
let pipe_landing = world.tick(at(playing, 750.0, 292.0, 0.0, 5.0, false))
let coyote = world.tick(world.set_control({*:at(playing, 1685.0, 360.0, 0.0, 0.0, false),
  hero: {*:playing.hero, x: 1685.0, coyote: 2, grounded: false}}, "jump", true))
let air_jump = world.tick(world.set_control(at(playing, 100.0, 280.0, 0.0, 2.0, false), "jump", true))
let bump = world.tick(at(playing, 314.0, 318.0, 0.0, -12.0, false))
let bump_twice = world.tick(at(bump, 314.0, 318.0, 0.0, -12.0, false))
{
  jump_rises: apex.hero.y < 240 and not apex.hero.grounded,
  quick_tap_jumps: tap.hero.vy < 0,
  release_shortens_jump: short_hop.hero.y > apex.hero.y,
  pointer_release_shortens_jump: pointer_hop.hero.vy == -5.0 and len(pointer_hop.keys) == 0,
  lands_on_ground: landed.hero.y == 360.0 and landed.hero.grounded,
  held_jump_does_not_repeat: landed.hero.vy == 0.0,
  wall_stops: wall.hero.x == 712.0 and wall.hero.vx == 0.0,
  pipe_top_landing: pipe_landing.hero.y == 296.0 and pipe_landing.hero.grounded,
  coyote_jump: coyote.hero.vy < 0,
  no_double_jump: air_jump.hero.vy > 0,
  question_bump: contains(bump.used, 21) and bump.coins == 1 and bump.score == 100,
  bump_stops_head: bump.hero.y == 312.0 and bump.hero.vy == 0.0,
  question_only_once: bump_twice.coins == 1 and bump_twice.score == 100,
  blocks_remain_solid: bump_twice.hero.y == 312.0
}

let collected = world.tick(at(playing, 210.0, 360.0, 0.0, 0.0, true))
let collected_twice = world.tick(collected)
let stomp = world.tick(at(playing, 515.0, 323.0, 0.0, 8.0, false))
let collision = world.tick(at(playing, 515.0, 360.0, 0.0, 0.0, true))
let protected = world.tick(at({*:playing, invincible: 10}, 515.0, 360.0, 0.0, 0.0, true))
let fall = world.tick(at(playing, 1085.0, 485.0, 0.0, 10.0, false))
let checkpoint = world.tick(at(playing, 1900.0, 360.0, 0.0, 0.0, true))
let respawn = world.lose_life(checkpoint)
let over = world.lose_life(world.lose_life(world.lose_life(playing)))
let won = world.tick(at(playing, 3580.0, 360.0, 0.0, 0.0, true))
let restarted = world.action(won, "restart")
let timeout = world.tick({*:playing, ticks: 5999})
let completed = play_course(playing, 1200)
{
  coin_pickup: collected.coins == 1 and collected.score == 100 and contains(collected.collected, 0),
  coin_only_once: collected_twice.coins == 1,
  stomp_defeats_enemy: not stomp.enemies[0].alive and stomp.score == 200,
  stomp_bounces: stomp.hero.vy < 0 and stomp.hero.y == 330.0,
  side_hit_costs_life: collision.lives == 2 and collision.hero.x == 72.0,
  respawn_protection: protected.lives == 3 and protected.invincible == 9,
  pit_costs_life: fall.lives == 2,
  checkpoint_activates: checkpoint.checkpoint,
  checkpoint_respawn: respawn.hero.x == world.CHECKPOINT_X and respawn.lives == 2,
  respawn_clears_controls: len(world.lose_life(right).keys) == 0,
  third_hit_game_over: over.mode == "over" and over.lives == 0,
  game_over_frozen: frames(over, 100) == over,
  goal_wins: won.mode == "won" and won.score == 1000,
  win_frozen: world.tick(won) == won,
  restart_fresh: restarted.mode == "playing" and restarted.score == 0 and restarted.lives == 3,
  timer_expiry_costs_life: timeout.lives == 2 and world.time_left(timeout) == 240,
  camera_clamps: world.camera_x(72) == 0 and world.camera_x(9999) == 2960,
  camera_tracks: world.camera_x(700) == 400,
  enemies_patrol: frames(playing, 200).enemies[0].x >= 440 and frames(playing, 200).enemies[0].x <= 650,
  full_course_reachable: completed.mode == "won" and completed.lives > 0,
  course_visits_checkpoint: completed.checkpoint and completed.coins > 0
}
