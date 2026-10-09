import fixture: .mod_fixture
import sim: ~~.mod_sim
import controls: ~~.mod_input
let rules = fixture.rules
let level = fixture.level([])
let arena = fixture.map_data(level)
let game = sim.initialize(arena, fixture.game(level))
let starting = sim.frame(arena, game, sim.initial_clock(), 1000, rules)
let walking = {*: starting.game, input: controls.set_key(controls.empty(), "w", true)}
let moved = sim.frame(arena, walking, starting.clock, 1100, rules)
let stalled = sim.frame(arena, moved.game, moved.clock, 11100, rules)
let paused = sim.action(arena, stalled.game, "pause", rules, fixture.visuals)
let parked = sim.frame(arena, paused, stalled.clock, 20000, rules)
let resumed = sim.action(arena, parked.game, "pause", rules, fixture.visuals)
let resume_frame = sim.frame(arena, resumed, parked.clock, 30000, rules)
let restarted = sim.action(arena, stalled.game, "restart", rules, fixture.visuals)
let reversed = sim.clock_frame(moved.clock, 1000);
{
    steps: [starting.game.ticks, moved.game.ticks, stalled.game.ticks],
    movement: [round(moved.game.player.y), moved.game.input.edges],
    stall: [round(stalled.clock.dropped * 35), stalled.clock.remainder < sim.STEP],
    pause: [paused.mode, len(paused.input.held), parked.game.ticks == stalled.game.ticks,
        resume_frame.game.ticks == stalled.game.ticks, resumed.mode],
    restart: [restarted.ticks, restarted.player.y, restarted.player.health, restarted.generation,
        restarted.seed == game.seed],
    backwards: reversed.steps
}
