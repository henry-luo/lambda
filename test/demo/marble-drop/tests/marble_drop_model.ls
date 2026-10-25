import game: ~~.mod_model
import sound: ~~.mod_sound

fn advance(st, count = 100) => reduce([st,*[for (i in 1 to count) i]],(s,unused)=>game.tick(s))
fn send(st, inlet, color) => advance(game.drop(st,inlet,color))
fn run_plan(st, plan) => reduce([st,*plan],(s,m)=>send(s,m.funnel,m.color))
let initial = game.new_game()
let moving = game.drop(initial,0,0)
let red = advance(moving)
let won = send(red,0,4)
let wrong = send(initial,0,4)
let undone = game.action(wrong,"undo")
{
    initial:initial.mode == "playing" and initial.moves == 0 and game.required(initial) == 2,
    immutable:initial.stock[0] == 1 and initial.bits[1] == 0 and initial.active == null,
    starts:moving.active != null and moving.stock[0] == 0 and moving.moves == 1,
    no_overlap:game.drop(moving,0,4) == moving,
    exact_arrival:red.active == null and red.filled[2] == 1 and red.bits[1] == 1,
    win:won.mode == "won" and game.collected(won) == 2 and won.medals[0] == 3,
    wrong_color:wrong.losses == 1 and game.collected(wrong) == 0 and wrong.stock[4] == 0,
    undo:undone.bits == initial.bits and undone.filled == initial.filled and undone.stock == initial.stock and undone.moves == 0,
    undo_inflight:game.action(moving,"undo").active == null and game.action(moving,"undo").stock == initial.stock,
    paused:game.tick(game.action(moving,"pause")) == game.action(moving,"pause"),
    resume:game.tick(game.action(game.action(moving,"pause"),"pause")).active.tick == 1,
    invalid_inlet:game.drop(initial,9,0) == initial and game.drop(initial,-1,0) == initial,
    invalid_color:game.drop(initial,0,9) == initial and game.drop(initial,0,-1) == initial,
    empty_color:game.drop(initial,0,1) == initial,
    locked_win:game.drop(won,0,6) == won,
    restart:game.action(wrong,"restart").stock == initial.stock,
    next:game.action(won,"next").stage == 1 and game.action(won,"next").medals[0] == 3,
    no_skip:game.action(initial,"next") == initial
}

let gate = game.new_game(3)
let opened = send(gate,0,6)
let sacrificed = send(gate,0,0)
let link = game.new_game(2)
let linked = send(link,0,1)
let wild = game.new_game(5)
let wildcard = send(send(wild,0,6),0,7)
let full = send(send(game.new_game(),0,0),0,6)
let excess = send({*:full,stock:[1,0,0,0,1,0,2,0]},0,0)
let no_steel = {*:wild,stock:[1,1,0,0,1,1,0,1]}
let black_plan = game.solve(no_steel)
{
    gate_preview:game.preview(gate,0).caught,
    steel_opens:opened.bits[1] == 1 and opened.stock[6] == 2 and opened.losses == 0 and game.collected(opened) == 0,
    color_caught:sacrificed.bits[1] == 1 and sacrificed.losses == 1,
    undo_gate:game.action(opened,"undo").bits == gate.bits and game.action(opened,"undo").stock == gate.stock,
    link_remote:linked.bits[3] == 1 and linked.bits[4] == 1 and linked.filled[5] == 1,
    link_preview:game.level(linked).nodes[game.preview(linked,1).node].color == 4,
    black_matches:game.collected(wildcard) == 1 and wildcard.losses == 0 and wildcard.stock[7] == 0,
    black_opens:send(wild,0,7).bits[2] == 1 and send(wild,0,7).losses == 1,
    black_plan:black_plan[0].color == 7 and run_plan(no_steel,black_plan).mode == "won",
    full_pocket:game.collected(excess) == 1 and excess.losses == 1 and excess.moves == 3,
    point_start:game.point({x:10,y:20},{x:100,y:200},0) == {x:10,y:20},
    point_end:game.point({x:10,y:20},{x:100,y:200},1) == {x:100,y:200},
    point_mid:game.point({x:10,y:20},{x:100,y:200},.5) == {x:55,y:110}
}

let solutions = [for (i in 0 to len(game.LEVELS)-1) {
    let start = game.new_game(i)
    let plan = game.solve(start)
    let done = run_plan(start,plan)
    {puzzle:i+1, solved:done.mode == "won", par:done.moves == game.level(start).par,
        clean:done.losses == 0, stock:all([for (v in done.stock) v >= 0]), medal:done.medals[i] == 3}
}]
solutions
{
    all_solved:all([for (s in solutions) s.solved and s.par and s.clean and s.stock and s.medal]),
    downward:all([for (l in game.LEVELS,n in l.nodes,d in n.exits) l.nodes[d].y > n.y]),
    valid_links:all([for (l in game.LEVELS,n in l.nodes where n.kind == "relay") l.nodes[n.link].kind == "switch"]),
    hint:game.action(initial,"hint").hint == "Try red in inlet A.",
    hint_changes:game.action(red,"hint").hint == "Try blue in inlet A.",
    no_solution:game.solve(wrong) == null,
    lost_hint:contains(game.action(wrong,"hint").hint,"Undo"),
    autoplay:advance(game.action(initial,"auto"),150).mode == "won",
    auto_pause:not game.action(game.action(initial,"auto"),"auto").autoplay,
    auto_undo:not game.action(game.tick(game.action(initial,"auto")),"undo").autoplay,
    stage_clamps:game.new_game(99).stage == 7 and game.new_game(-1).stage == 0
}

let at_switch = advance(moving,game.EDGE_TICKS-1)
let at_pocket = advance(moving,game.EDGE_TICKS*2-1)
let at_win = advance(game.drop(red,0,4),game.EDGE_TICKS*2-1)
let at_catch = advance(game.drop(gate,0,6),game.EDGE_TICKS-1)
let at_miss = advance(game.drop(initial,0,4),game.EDGE_TICKS*2-1)
let at_steel = advance(game.drop(initial,0,6),game.EDGE_TICKS*2-1)
let at_link = advance(game.drop(link,0,1),game.EDGE_TICKS-1)
{
    sound_default:sound.status(sound.initial()) == "on" and sound.initial().played == 0,
    drop_cue:sound.cue(initial,moving,"drop") == "drop",
    auto_drop_cue:sound.cue(initial,moving,"tick") == "drop",
    switch_cue:sound.cue(at_switch,game.tick(at_switch),"tick") == "switch",
    link_cue:sound.cue(at_link,game.tick(at_link),"tick") == "switch",
    catch_cue:sound.cue(at_catch,game.tick(at_catch),"tick") == "catch",
    pocket_cue:sound.cue(at_pocket,game.tick(at_pocket),"tick") == "pocket",
    win_cue:sound.cue(at_win,game.tick(at_win),"tick") == "win",
    miss_cue:sound.cue(at_miss,game.tick(at_miss),"tick") == "miss",
    steel_cue:sound.cue(at_steel,game.tick(at_steel),"tick") == "steel",
    quiet_rolling:sound.cue(moving,game.tick(moving),"tick") == "",
    quiet_pause:sound.cue(at_switch,game.action(at_switch,"pause"),"pause") == "",
    quiet_paused_tick:sound.cue(game.action(at_switch,"pause"),game.action(at_switch,"pause"),"tick") == "",
    quiet_undo:sound.cue(won,game.action(won,"undo"),"undo") == "",
    quiet_restart:sound.cue(won,game.action(won,"restart"),"restart") == "",
    quiet_next:sound.cue(won,game.action(won,"next"),"next") == "",
    quiet_hint:sound.cue(red,game.action(red,"hint"),"hint") == "",
    quiet_invalid:sound.cue(initial,initial,"drop") == "",
    quiet_empty:sound.cue(won,won,"tick") == "",
    voice_budget:sound.MAX_VOICES == 8
}
