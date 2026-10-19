// Run: ./lambda.exe demo sentence-game
import dom
import resources: ~~.mod_resources
import game_model: .mod_game
import scene: .mod_game_scene

let CSS = input(url_resolve(resources.entry_uri()^,"sentence-game.css"),'text')^

// The reader is a girl: the best installed US English female voice, since dom.speech_voices()
// lists voices best first; null falls back to the default en-US voice.
fn girl_voice() {
    let girls = [for (v in dom.speech_voices() where v.lang == "en-US" and v.gender == "female") v]
    if (len(girls) > 0) girls[0].id else null
}

// Reading aloud is a view side effect outside the immutable game (S12.1.3). `voice` is the
// utterance in progress, {token, part}; a new reading replaces it, as a single reader would.
pn read_aloud(owner,voice,words,part) {
    if (voice != null) { dom.speech_command(owner,voice.token,"cancel") }
    // a slightly unhurried pace suits early readers; a raised pitch makes the adult voice sound young.
    let token = dom.speak(owner,words,{voice:girl_voice(),lang:"en-US",rate:0.9,pitch:1.3})
    if (token == null) { return null }
    return {token:token,part:part}
}

// `heard` is the card under a double-click's first click: in Level 2 that click already moved the card away.
view <sentence_app> state game:game_model.new_game(), voice:null, heard:null {
    scene.page(game,voice != null and voice.part == "sentence")
}
on click(evt) {
    let button = dom.closest(evt.target,"button")
    let words = dom.get_attribute(button,"data-say")
    // a double-click's second click only reads; acting again would pick the card that slid under the pointer.
    if (evt.detail > 1 and words != null) { return 'handled' }
    if (evt.detail == 1 and heard != words) { heard = words }
    let command = dom.get_attribute(button,"data-action")
    if (button == null or command == null) { return 'pass' }
    if (command == "read") {
        let owner = dom.closest(button,"#sentence-game")
        if (voice != null and voice.part == "sentence") {
            dom.speech_command(owner,voice.token,"cancel")
            voice = null
        } else { voice = read_aloud(owner,voice,words,"sentence") }
        return 'handled'
    }
    game = game_model.action(game,command,dom.get_attribute(button,"data-card"))
    return 'handled'
}
on dblclick(evt) {
    let card = dom.closest(evt.target,"[data-say]")
    if (card == null or heard == null or dom.get_attribute(card,"data-action") == "read") { return 'pass' }
    voice = read_aloud(dom.closest(card,"#sentence-game"),voice,heard,"card")
    return 'prevent-default'
}
// a reading that ends or fails frees the read-aloud button; one it already replaced changes nothing.
on speechend(evt) {
    if (voice != null and evt.token == voice.token) { voice = null }
    return 'handled'
}
on speecherror(evt) {
    if (voice != null and evt.token == voice.token) { voice = null }
    return 'handled'
}
on keydown(evt) {
    if (evt.ctrlKey or evt.metaKey or evt.altKey) { return 'pass' }
    let changed = game_model.shortcut(game,lower(string(evt.key)))
    if (changed == game) { return 'pass' }
    game = changed
    return 'prevent-default'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Sentence Match — Lambda Playground"><style CSS>>
    <body apply(<sentence_app>)>
>
