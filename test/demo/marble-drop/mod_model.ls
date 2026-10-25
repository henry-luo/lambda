// Pure transitions, with mutable ownership confined to the view (S1.4, S12.1.3).
import levels: .mod_levels
pub let LEVELS = levels.LEVELS
pub let COLORS = levels.COLORS
pub let INKS = levels.INKS
// Each rail takes 280 ms on the shared 40 ms clock.
pub let EDGE_TICKS = 7
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

// Both animation and planning enter devices through this single routing rule.
pub fn visit(machine, bits, index) {
    let node = machine.nodes[index]
    let closed = node.kind == "gate" and bits[index] == 0
    let updated = if (node.kind == "switch") replace_at(bits,index,1-bits[index])
        else if (node.kind == "gate") replace_at(bits,index,1)
        else if (node.kind == "relay") replace_at(bits,node.link,1-bits[node.link]) else bits
    {bits:updated, next:if (node.kind == "bin" or closed) -1
        else node.exits[if (node.kind == "switch") bits[index] else 0], caught:closed, node:index}
}
fn trace(machine, bits, index, remaining) {
    let step = visit(machine,bits,index)
    if (step.next == -1 or remaining == 0) step
    else trace(machine,step.bits,step.next,remaining-1)
}
pub fn preview(game, funnel) => trace(level(game),game.bits,level(game).funnels[funnel],len(level(game).nodes))

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
        else if (result.caught) (if (color == 6) "Steel caught. The passage is now open." else "The catch kept your marble. Its passage is now open.")
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
        let step = visit(level(game),game.bits,from)
        {*:game, bits:step.bits, stock:replace_at(game.stock,color,game.stock[color]-1),
            selected:color, funnel:funnel, moves:game.moves+1, hint:"", message:"Follow the marble…",
            active:{from:from, to:step.next, tick:0, color:color}, history:[*game.history,snapshot(game)]}
    }
}
// A planner move runs the same devices and pocket rules without animation/history.
fn trial(game, funnel, color, result) => finish({*:game, moves:game.moves+1,
    stock:replace_at(game.stock,color,game.stock[color]-1)},result,color)
fn state_key(game) => {bits:game.bits, filled:game.filled}
fn candidates(game) => [for (f in 0 to len(level(game).funnels)-1) {
    let result = preview(game,f)
    let node = level(game).nodes[result.node]
    let wanted = not result.caught and game.filled[result.node] < node.need
    let color = if (wanted) (if (game.stock[node.color] > 0) node.color else 7)
        else if (game.stock[6] > 0) 6 else 7
    {funnel:f, color:color, result:result, useful:wanted or result.caught}
}]
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
        if (ball.tick+1 < EDGE_TICKS) {*:game,active:{*:ball,tick:ball.tick+1}}
        else {
            let step = visit(level(game),game.bits,ball.to)
            if (step.next == -1) finish(game,step,ball.color)
            else {*:game,bits:step.bits,active:{*:ball,from:ball.to,to:step.next,tick:0}}
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

// The SVG rail and moving ball share the same cubic, including its control points.
pub fn point(a, b, t) {
    let u = 1-t
    let mid = (a.y+b.y)/2
    {x:u*u*u*a.x+3*u*u*t*a.x+3*u*t*t*b.x+t*t*t*b.x,
     y:u*u*u*a.y+3*u*u*t*mid+3*u*t*t*mid+t*t*t*b.y}
}
pub fn rail(a,b) => "M" ++ string(a.x) ++ " " ++ string(a.y) ++ " C" ++ string(a.x) ++ " " ++
    string((a.y+b.y)/2) ++ " " ++ string(b.x) ++ " " ++ string((a.y+b.y)/2) ++ " " ++ string(b.x) ++ " " ++ string(b.y)
