// Pure transitions, with mutable ownership confined to the view (S1.4, S12.1.3).
import levels: .mod_levels
import motion: .mod_motion
pub let LEVELS = levels.LEVELS
pub let COLORS = levels.COLORS
pub let INKS = levels.INKS
pub let STEP_MS = motion.STEP_MS
pub fn level(game) => LEVELS[game.stage]
fn replace_at(values, index, value) => [for (i,v in values) if (i == index) value else v]
pub fn targets(game) => [for (i,n in level(game).nodes where n.kind == "bin") i]
pub fn required(game) => sum([for (n in level(game).nodes) n.need])
pub fn collected(game) => sum(game.filled)
pub fn new_game(stage = 0, medals = []) {
    let index = max(0,min(len(LEVELS)-1,stage))
    let machine = LEVELS[index]
    let stock = [for (c in 0 to 7) if (c == 6) max(3,machine.par-sum([for (n in machine.nodes) n.need]))
        else if (c == 7) (if (index >= 5) 1 else 0)
        else sum([for (n in machine.nodes where n.color == c) n.need])]
    {stage:index, medals:medals, bits:[for (i,n in machine.nodes) if (contains(machine.initial,i)) 1 else 0],
        filled:[for (n in machine.nodes) 0], stock:stock, selected:[for (c in 0 to 5 where stock[c] > 0) c][0],
        funnel:0, active:null, mode:"playing", paused:false, autoplay:false, plan:[],
        moves:0, losses:0, history:[], hint:"", message:"Choose a marble, then drop it into an inlet."}
}

pub fn indexed(node) => node.kind == "switch" or node.kind == "rotor"
pub fn selective(node) => node.kind == "magnet" or node.kind == "filter"
pub fn catching(node) => node.kind == "gate" or node.kind == "counter"
fn advance_device(machine,bits,index) {
    let next = bits[index]+1
    replace_at(bits,index,if (next >= len(machine.nodes[index].exits)) 0 else next)
}
pub fn outlet(machine,bits,index,color) {
    let node = machine.nodes[index]
    if (indexed(node)) bits[index]
    else if (selective(node)) (if (color == node.match) 0 else 1)
    else 0
}
// Animation, colored route previews and the planner share every device rule.
pub fn visit(machine, bits, index, color = -1) {
    let node = machine.nodes[index]
    let closed = catching(node) and bits[index] < node.threshold
    let updated = if (indexed(node)) advance_device(machine,bits,index)
        else if (catching(node)) replace_at(bits,index,min(node.threshold,bits[index]+1))
        else if (node.kind == "relay") advance_device(machine,bits,node.link) else bits
    {bits:updated, next:if (node.kind == "bin" or closed) -1
        else node.exits[outlet(machine,bits,index,color)], caught:closed, node:index}
}
fn trace(machine, bits, index, remaining, color) {
    let step = visit(machine,bits,index,color)
    if (step.next == -1 or remaining == 0) step
    else trace(machine,step.bits,step.next,remaining-1,color)
}
pub fn preview(game, funnel, color = null) => trace(level(game),game.bits,level(game).funnels[funnel],
    len(level(game).nodes),if (color == null) game.selected else color)

fn finish(game, result, color) {
    let node = level(game).nodes[result.node]
    let correct = not result.caught and node.kind == "bin" and game.filled[result.node] < node.need and
        (color == node.color or color == 7)
    let filled = if (correct) replace_at(game.filled,result.node,game.filled[result.node]+1) else game.filled
    let wasted = not correct and color != 6
    let won = sum(filled) == required(game)
    let losses = game.losses + (if (wasted) 1 else 0)
    let medal = if (losses == 0 and game.moves <= level(game).par) 3 else if (losses <= 1) 2 else 1
    let medals = if (won) [for (i in 0 to len(LEVELS)-1)
        if (i == game.stage) max(medal,game.medals[i] or 0) else game.medals[i] or 0] else game.medals
    let message = if (won) "A beautiful solution. Every color is home."
        else if (result.caught) (if (result.bits[result.node] < node.threshold)
            "Counterweight loaded: " ++ string(result.bits[result.node]) ++ " / " ++ string(node.threshold) ++ ". One more marble opens it."
            else if (color == 6) "Steel caught. The passage is now open." else "The catch kept your marble. Its passage is now open.")
        else if (correct) COLORS[if (color == 7) node.color else color] ++ " arrived safely."
        else if (color == 6) "Test complete. The steel ball leaves the machine."
        else if (game.filled[result.node] >= node.need) "That pocket is full. Undo to recover the marble."
        else "Wrong color. Undo to recover the marble."
    {*:game, bits:result.bits, filled:filled, active:null, losses:losses, medals:medals,
        mode:if (won) "won" else "playing", autoplay:game.autoplay and not won, message:message}
}
fn snapshot(game) => {bits:game.bits, filled:game.filled, stock:game.stock, selected:game.selected,
    funnel:game.funnel, moves:game.moves, losses:game.losses, mode:game.mode, message:game.message, medals:game.medals}
pub fn drop(game, funnel, color) {
    if (game.mode != "playing" or game.paused or game.active != null or
        funnel < 0 or funnel >= len(level(game).funnels) or color < 0 or color > 7 or game.stock[color] <= 0) game
    else {
        let from = level(game).funnels[funnel]
        let step = visit(level(game),game.bits,from,color)
        {*:game, bits:step.bits, stock:replace_at(game.stock,color,game.stock[color]-1),
            selected:color, funnel:funnel, moves:game.moves+1, hint:"", message:"Follow the marble…",
            active:{from:from, to:step.next, tick:0, color:color,
                motion:motion.start(level(game).nodes[from],level(game).nodes[step.next])},
            history:[*game.history,snapshot(game)]}
    }
}
// A planner move runs the same devices and pocket rules without animation/history.
fn trial(game, funnel, color, result) => finish({*:game, moves:game.moves+1,
    stock:replace_at(game.stock,color,game.stock[color]-1)},result,color)
fn state_key(game) => {bits:game.bits, filled:game.filled}
fn candidates(game) {
    let moves = [for (f in 0 to len(level(game).funnels)-1, color in 0 to 7 where game.stock[color] > 0) {
        let result = preview(game,f,color)
        let node = level(game).nodes[result.node]
        let wanted = not result.caught and node.kind == "bin" and game.filled[result.node] < node.need and
            (color == node.color or color == 7)
        {funnel:f,color:color,result:result,useful:wanted or result.caught,
            legal:wanted or color >= 6}
    }];
    [for (move in moves where move.legal) move]
}
fn search(game, seen, depth) {
    if (game.mode == "won") []
    else if (depth == 0) null
    else {
        let moves = candidates(game)
        // Prefer a useful pocket/catch, but allow steel to cycle a lever when necessary.
        let ordered = [*[for (m in moves where m.useful) m], *[for (m in moves where not m.useful) m]]
        choose_plan(game,ordered,seen,depth,0)
    }
}
fn choose_plan(game, moves, seen, depth, index) {
    if (index >= len(moves)) null
    else {
        let move = moves[index]
        let next = trial(game,move.funnel,move.color,move.result)
        let key = state_key(next)
        let rest = if (game.stock[move.color] <= 0 or contains(seen,key)) null
            else search(next,[*seen,key],depth-1)
        if (rest != null) [{funnel:move.funnel,color:move.color},*rest]
        else choose_plan(game,moves,seen,depth,index+1)
    }
}
pub fn solve(game) => search(game,[state_key(game)],sum(game.stock))
pub fn hint_text(game, plan) => if (plan == null or len(plan) == 0)
    "No solution with this tray. Undo a drop or restart the machine."
    else "Try " ++ lower(COLORS[plan[0].color]) ++ " in inlet " ++
        level(game).nodes[level(game).funnels[plan[0].funnel]].label ++ "."

pub fn tick(game) {
    if (game.paused or game.mode != "playing") game
    else if (game.active == null) {
        if (not game.autoplay or len(game.plan) == 0) game
        else {*:drop(game,game.plan[0].funnel,game.plan[0].color), plan:slice(game.plan,1,len(game.plan))}
    } else {
        let ball = game.active
        let elapsed = (ball.tick+1)*STEP_MS
        if (elapsed < ball.motion.duration) {*:game,active:{*:ball,tick:ball.tick+1}}
        else {
            let step = visit(level(game),game.bits,ball.to,ball.color)
            if (step.next == -1) finish(game,step,ball.color)
            else {*:game,bits:step.bits,active:{*:ball,from:ball.to,to:step.next,
                tick:(elapsed-ball.motion.duration)/STEP_MS,
                // junctions dissipate energy; a spring supplies its own launch impulse
                motion:motion.start(level(game).nodes[ball.to],level(game).nodes[step.next],ball.motion.speed*.86)}}
        }
    }
}
pub fn action(game, command, value = 0) {
    if (command == "restart") new_game(game.stage,game.medals)
    else if (command == "level") new_game(value,game.medals)
    else if (command == "next" and game.mode == "won") new_game((game.stage+1)%len(LEVELS),game.medals)
    else if (command == "undo" and len(game.history) > 0) {
        let old = game.history[len(game.history)-1]
        {*:game,*:old,active:null,autoplay:false,plan:[],paused:false,hint:"",
            history:slice(game.history,0,len(game.history)-1)}
    }
    else if (command == "pause" and game.mode == "playing") {*:game,paused:not game.paused}
    else if (command == "select" and value >= 0 and value < 8 and game.stock[value] > 0 and not game.autoplay)
        {*:game,selected:value}
    else if (command == "funnel" and value >= 0 and value < len(level(game).funnels)) {*:game,funnel:value}
    else if (command == "drop" and not game.autoplay) drop(game,game.funnel,game.selected)
    else if (command == "inlet" and not game.autoplay) drop(game,value,game.selected)
    else if (command == "hint" and game.active == null and game.mode == "playing")
        {*:game,hint:hint_text(game,solve(game))}
    else if (command == "auto" and game.mode == "playing") {
        if (game.autoplay) {*:game,autoplay:false,plan:[]}
        else if (game.active != null) game
        else {
            let plan = solve(game)
            {*:game,autoplay:plan != null and len(plan) > 0,plan:plan or [],paused:false,hint:hint_text(game,plan)}
        }
    }
    else game
}

pub fn point(a,b,t) => motion.point(a,b,t)
pub fn rail(a,b) => motion.rail(a,b)
pub fn ball_position(game) => motion.position(game.active.motion,game.active.tick*STEP_MS)
