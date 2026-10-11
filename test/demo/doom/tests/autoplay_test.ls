import fixture: .mod_fixture
import autoplay: ~~.mod_autoplay
import sim: ~~.mod_sim
let rules = fixture.rules
let level = fixture.level([fixture.thing(3004, 0, 200)])
let arena = fixture.map_data(level)
let game = sim.action(arena, fixture.game(level), "auto", rules, fixture.visuals)
let aiming = autoplay.decide(arena, game, sim.STEP, rules)
let played = reduce([game, *[for (i in 1 to 175) i]], (acc, unused) =>
    sim.step(arena, acc, autoplay.decide(arena, acc, sim.STEP, rules), sim.STEP, rules))
let wall = {start: {x: -100, y: 40}, end: {x: 100, y: 40}, isSolid: true, bottomHeight: 0, topHeight: 128}
let blocked_level = fixture.level([], [wall])
let blocked = autoplay.decide(fixture.map_data(blocked_level), fixture.game(blocked_level), sim.STEP, rules)
let paused = sim.action(arena, game, "pause", rules, fixture.visuals)
let frozen = sim.frame(arena, paused, sim.initial_clock(), 1000, rules)
let frame = sim.frame(arena, game, {*:sim.initial_clock(), last_ms: 0}, 100, rules);
{
    starts: game.autoplay and game.mode == "playing",
    moves_and_fires: contains(aiming.held, "forward") and contains(aiming.held, "fire"),
    combat: played.actors[0].collected and played.player.ammo.bullets < 50,
    steering: contains(blocked.held, "turn_left") and not contains(blocked.held, "forward"),
    doors: contains(blocked.edges, "use"),
    fixed_steps: frame.game.ticks == 3 and frame.game.player.y > 0,
    pause: frozen.game.ticks == 0 and frozen.game.autoplay,
    resume: sim.action(arena, paused, "pause", rules, fixture.visuals).autoplay,
    restart_manual: not sim.action(arena, game, "restart", rules, fixture.visuals).autoplay
}
