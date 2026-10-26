// Run: ./lambda.exe demo marble-drop
import dom
import model: .mod_model
import scene: .mod_scene
import sound: .mod_sound
import sketches: .mod_sketches
import resources: ~~.mod_resources

let BASE = url_resolve(resources.entry_uri()^,".")

fn key_action(key) {
    let k = lower(string(key))
    if (contains(["1","2","3","4","5","6","7","8"],k)) {command:"select",value:int(k)-1}
    else if (k == "a" or k == "b") {command:"inlet",value:if (k == "a") 0 else 1}
    else if (k == " " or k == "space" or k == "enter") {command:"drop",value:0}
    else if (k == "u" or k == "backspace") {command:"undo",value:0}
    else if (k == "h") {command:"hint",value:0}
    else if (k == "p" or k == "escape") {command:"pause",value:0}
    else if (k == "r") {command:"restart",value:0}
    else if (k == "n") {command:"next",value:0}
    else if (k == "x") {command:"auto",value:0}
    else if (k == "m") {command:"sound",value:0}
    else null
}

fn initial() {
    let game = model.new_game()
    {game:game,shown:game,apparatus:scene.apparatus(game),sound:sound.initial(),sketches:sketches.layout(game.stage),
        frame:0,at:0.0,lag:0.0}
}
fn board_key(game) => {*:game,active:if (game.active == null) null else {*:game.active,tick:0}}
fn pose_game(game,lag) => {*:game,active:if (game.active == null) null else {*:game.active,tick:game.active.tick+lag/model.STEP_MS}}
fn update(session, game, pose, action) => if (game == session.game) session else
    {*:session,game:game,shown:if (action != "tick" or board_key(game) != board_key(session.game)) pose else session.shown,
        sketches:if (game.stage == session.game.stage) session.sketches else sketches.layout(game.stage),
        apparatus:if (scene.apparatus_key(game) == scene.apparatus_key(session.game)) session.apparatus
        else scene.apparatus(game)}

fn audio_owner(evt) => dom.query_selector(dom.root_node(evt.target),"#marble-clock")
fn running(game) => not game.paused and game.mode == "playing" and (game.active != null or game.autoplay)
pn transition(evt, session, game, command, now_ms) {
    let owner = audio_owner(evt)
    let audio = sound.follow(owner,session.sound,session.game,game,command,BASE)
    let active = running(game)
    let lag = if (game.active == null or session.game.active == null) 0.0 else session.lag
    let pose = pose_game(game,lag)
    if (game.active != null) {
        let marble = dom.query_selector(dom.root_node(evt.target),"#rolling-marble")
        // the new drop mounts at its origin; subsequent ticks move only its paint, without a page reflow
        if (marble != null) dom.presentation_style_set_property(marble,"transform",scene.rolling_transform(pose))
    }
    if (not active and session.frame > 0) dom.cancel_frame(owner,session.frame)
    return {*:update(session,game,pose,command),sound:audio,lag:lag,
        at:if (running(session.game) and active) session.at else now_ms,
        frame:if (active) (if (session.frame > 0) session.frame else dom.request_frame(owner,"marbleframe")) else 0}
}
pn command(evt, session, action, value, now_ms) {
    if (action == "sound") { return {*:session,shown:pose_game(session.game,session.lag),sound:sound.toggle(audio_owner(evt),session.sound)} }
    return transition(evt,session,model.action(session.game,action,value),action,now_ms)
}

// Rendering is pure; the view owns all live state and handlers (S12.1.3).
view <marble_game> state session:initial() {
    // semantic redraws retain the current pose; ordinary frames move only the existing leaf
    scene.page(session.shown,session.apparatus,session.sound,session.sketches)
}
on click(evt) {
    let button = dom.closest(evt.target,"button[data-action]")
    if (button == null) { return 'pass' }
    // Only indexed controls carry a value; int(null) produces an error, not a default.
    let value = dom.get_attribute(button,"data-value")
    session = command(evt,session,dom.get_attribute(button,"data-action"),if (value == null) 0 else int(value),evt.time_stamp)
    return 'handled'
}
on marblekey(evt) {
    session = command(evt,session,evt.detail.command,evt.detail.value,evt.detail.time)
    return 'handled'
}
on marbletick(evt) {
    if (evt.detail.token != session.frame or not running(session.game)) { return 'handled' }
    // keep routing on 40 ms steps and interpolate only the paint between them
    let elapsed = session.lag+max(0.0,evt.detail.time-session.at)
    let next_drop = session.game.active == null and session.game.autoplay
    let due = elapsed >= model.STEP_MS or next_drop
    let game = if (due) model.tick(session.game) else session.game
    let lag = if (next_drop) 0.0 else if (due) min(model.STEP_MS,elapsed-model.STEP_MS) else elapsed
    session = transition(evt,{*:session,frame:0,at:evt.detail.time,lag:lag},game,"tick",evt.detail.time)
    return 'handled'
}
on marbleclose(evt) {
    if (session.frame > 0) dom.cancel_frame(audio_owner(evt),session.frame)
    session = {*:session,sound:sound.silence(audio_owner(evt),session.sound)}
    return 'handled'
}

// the frame owner lives outside the changing game subtree; idle and paused boards request no frames
view <marble_shell> {
    <body apply(<marble_game>); <div id:"marble-clock", 'aria-hidden':"true">>
}
on keydown(evt) {
    if (evt.ctrlKey or evt.metaKey or evt.altKey or evt.repeat) { return 'pass' }
    let action = key_action(evt.key)
    if (action == null) { return 'pass' }
    dom.dispatch(dom.query_selector(dom.root_node(evt.target),"#marble-drop"),
        {type:"marblekey",detail:{*:action,time:evt.time_stamp}})
    return 'prevent-default'
}
on marbleframe(evt) {
    dom.dispatch(dom.query_selector(dom.root_node(evt.target),"#marble-drop"),
        {type:"marbletick",detail:{token:evt.detail,time:evt.time_stamp}})
    return 'handled'
}
on closerequest(evt) {
    dom.dispatch(dom.query_selector(dom.root_node(evt.target),"#marble-drop"),"marbleclose")
    return 'pass'
}

<html lang:"en", <head <meta charset:"UTF-8"><title "Marble Drop — The Inventor’s Workshop">
    <link rel:"stylesheet",href:"marble-drop.css">>;
    apply(<marble_shell>)>
