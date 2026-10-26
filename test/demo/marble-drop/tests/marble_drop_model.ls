import game: ~~.mod_model
import sound: ~~.mod_sound

fn advance(st, count = 100) => reduce([st,*[for (i in 1 to count) i]],(s,unused)=>game.tick(s))
fn send(st, inlet, color) => advance(game.drop(st,inlet,color))
fn run_plan(st, plan) => reduce([st,*plan],(s,m)=>send(s,m.funnel,m.color))
fn before_arrival(st, node, budget = 200) {
    let next = game.tick(st)
    if (budget == 0 or (st.active.to == node and
        (next.active == null or next.active.from != st.active.from))) st
    else before_arrival(next,node,budget-1)
}
fn acyclic(machine,index,ancestors:array) bool | error => not contains(ancestors,index) and
    all([for (next in machine.nodes[index].exits) acyclic(machine,next,[*ancestors,index])])
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
    acyclic_routes:all([for (l in game.LEVELS,f in l.funnels) acyclic(l,f,[])]),
    ordinary_descents:all([for (l in game.LEVELS,n in l.nodes,d in n.exits where n.kind != "spring") l.nodes[d].y > n.y]),
    valid_links:all([for (l in game.LEVELS,n in l.nodes where n.kind == "relay") game.indexed(l.nodes[n.link])]),
    hint:game.action(initial,"hint").hint == "Try red in inlet A.",
    hint_changes:game.action(red,"hint").hint == "Try blue in inlet A.",
    no_solution:game.solve(wrong) == null,
    lost_hint:contains(game.action(wrong,"hint").hint,"Undo"),
    autoplay:advance(game.action(initial,"auto"),150).mode == "won",
    auto_pause:not game.action(game.action(initial,"auto"),"auto").autoplay,
    auto_undo:not game.action(game.tick(game.action(initial,"auto")),"undo").autoplay,
    stage_clamps:game.new_game(99).stage == len(game.LEVELS)-1 and game.new_game(-1).stage == 0
}

let at_switch = before_arrival(moving,1)
let at_pocket = before_arrival(moving,2)
let at_win = before_arrival(game.drop(red,0,4),3)
let at_catch = before_arrival(game.drop(gate,0,6),1)
let at_miss = before_arrival(game.drop(initial,0,4),2)
let at_steel = before_arrival(game.drop(initial,0,6),2)
let at_link = before_arrival(game.drop(link,0,1),2)
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

let rotary = game.new_game(8)
let rotary_one = send(rotary,0,0)
let rotary_two = send(rotary_one,0,3)
let rotary_three = send(rotary_two,0,4)
let weight = game.new_game(9)
let weight_one = send(weight,0,6)
let weight_two = send(weight_one,0,6)
let weight_pass = send(weight_two,0,1)
let magnet = game.new_game(10)
let steel_pocket = send(magnet,0,6)
let lens = game.new_game(11)
let spring = game.new_game(12)
let at_spring = before_arrival(game.drop(spring,0,0),1)
let launched = game.tick(at_spring)
let spring_nodes = game.level(spring).nodes
let third_link = game.new_game(14)
let remote_one = send(third_link,0,0)
let remote_two = send(remote_one,0,1)
{
    sixteen_puzzles:len(game.LEVELS) == 16,
    rotary_advances:rotary_one.bits[1] == 1 and rotary_two.bits[1] == 2,
    rotary_wraps:rotary_three.bits[1] == 0 and rotary_three.mode == "won",
    rotary_undo:game.action(rotary_three,"undo").bits[1] == 2,
    counter_first:weight_one.bits[1] == 1 and weight_one.filled == weight.filled and contains(weight_one.message,"1 / 2"),
    counter_second:weight_two.bits[1] == 2 and weight_two.losses == 0 and contains(weight_two.message,"open"),
    counter_pass:weight_pass.bits[1] == 2 and game.collected(weight_pass) == 1,
    counter_undo:game.action(weight_two,"undo").bits == weight_one.bits and game.action(weight_two,"undo").stock == weight_one.stock,
    magnet_steel:game.preview(magnet,0,6).node == 2,
    magnet_glass:game.preview(magnet,0,0).node == 4,
    steel_collection:game.collected(steel_pocket) == 1 and steel_pocket.filled[2] == 1 and steel_pocket.losses == 0,
    magnet_fixed:steel_pocket.bits[1] == 0 and steel_pocket.bits[3] == 0,
    filter_match:game.preview(lens,0,4).node == 2,
    filter_other:game.preview(lens,0,0).node == 4,
    filter_black:game.preview(lens,0,7).node == 4,
    selected_preview:game.preview(game.action(lens,"select",4),0).node == 2,
    spring_launch:launched.active.from == 1 and launched.active.to == 2,
    spring_arc:game.point(spring_nodes[1],spring_nodes[2],.5).y < spring_nodes[2].y,
    spring_arrival:send(spring,0,0).filled[3] == 1,
    spring_cue:sound.cue(at_spring,launched,"tick") == "steel",
    remote_rotor:remote_one.bits[3] == 1 and remote_two.bits[3] == 2,
    remote_wrap:game.visit(game.level(remote_two),remote_two.bits,2,0).bits[3] == 0,
    remote_preview:game.level(remote_two).nodes[game.preview(remote_two,1,5).node].color == 5,
    new_autoplay:advance(game.action(game.new_game(15),"auto"),1000).mode == "won"
}
