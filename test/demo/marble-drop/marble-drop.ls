// Run: ./lambda.exe demo marble-drop
import dom
import model: .mod_model
import scene: .mod_scene
import sound: .mod_sound
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
    {game:game,apparatus:scene.apparatus(game),sound:sound.initial()}
}
fn update(session, game) => if (game == session.game) session else
    {*:session,game:game,apparatus:if (scene.apparatus_key(game) == scene.apparatus_key(session.game)) session.apparatus
        else scene.apparatus(game)}

fn audio_owner(evt) => dom.query_selector(dom.root_node(evt.target),"#marble-clock")
pn transition(evt, session, game, command) {
    let audio = sound.follow(audio_owner(evt),session.sound,session.game,game,command,BASE)
    return {*:update(session,game),sound:audio}
}
pn command(evt, session, action, value) {
    if (action == "sound") { return {*:session,sound:sound.toggle(audio_owner(evt),session.sound)} }
    return transition(evt,session,model.action(session.game,action,value),action)
}

// Rendering is pure; the view owns all live state and handlers (S12.1.3).
view <marble_game> state session:initial() {
    scene.page(session.game,session.apparatus,session.sound)
}
on click(evt) {
    let button = dom.closest(evt.target,"button[data-action]")
    if (button == null) { return 'pass' }
    // Only indexed controls carry a value; int(null) produces an error, not a default.
    let value = dom.get_attribute(button,"data-value")
    session = command(evt,session,dom.get_attribute(button,"data-action"),if (value == null) 0 else int(value))
    return 'handled'
}
on marblekey(evt) {
    session = command(evt,session,evt.detail.command,evt.detail.value)
    return 'handled'
}
on marbletick(evt) {
    // Idle ticks leave the game state unchanged.
    let game = session.game
    if (not game.paused and game.mode == "playing" and (game.active != null or game.autoplay)) {
        session = transition(evt,session,model.tick(game),"tick")
    }
    return 'handled'
}
on marbleclose(evt) {
    session = {*:session,sound:sound.silence(audio_owner(evt),session.sound)}
    return 'handled'
}

// The clock lives outside the changing game subtree, so redraws cannot reset it.
view <marble_shell> {
    <body apply(<marble_game>); <div id:"marble-clock", 'aria-hidden':"true">>
}
on keydown(evt) {
    if (evt.ctrlKey or evt.metaKey or evt.altKey or evt.repeat) { return 'pass' }
    let action = key_action(evt.key)
    if (action == null) { return 'pass' }
    dom.dispatch(dom.query_selector(dom.root_node(evt.target),"#marble-drop"),
        {type:"marblekey",detail:action})
    return 'prevent-default'
}
on animationiteration(evt) {
    dom.dispatch(dom.query_selector(dom.root_node(evt.target),"#marble-drop"),"marbletick")
    return 'handled'
}
on closerequest(evt) {
    dom.dispatch(dom.query_selector(dom.root_node(evt.target),"#marble-drop"),"marbleclose")
    return 'pass'
}

<html lang:"en", <head <meta charset:"UTF-8"><title "Marble Drop — Eight Little Inventions">
    <link rel:"stylesheet",href:"marble-drop.css">>;
    apply(<marble_shell>)>
