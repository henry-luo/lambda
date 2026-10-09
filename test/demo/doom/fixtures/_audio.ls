import dom
import data: ~~.mod_data
let BASE = url_resolve(data.entry_uri()^, "../")
let TREE = <main id:"audio-probe",
    <button id:"open", "Decode"> <button id:"play", "Play"> <button id:"pause", "Pause">
    <button id:"close", "Close"> <button id:"stale", "Reject stale token">
>
view <audio_probe> state token: null, retired: null { ~.rendered }
on click(evt) {
    let owner = dom.closest(evt.target, "#audio-probe")
    if (owner == null) { return 'pass' }
    let command = dom.get_attribute(evt.target, "id")
    if (command == "open") {
        if (token != null) dom.audio_close(owner, token)
        token = dom.audio_open(owner, input(url_resolve(BASE, "assets/sounds/DSPISTOL.wav"), 'binary')^)
        dom.set_attribute(owner, "data-fresh", string(token != null and token != retired))
    } else if (command == "play") {
        dom.set_attribute(owner, "data-accepted", string(dom.audio_play(owner, token, 1.0)))
    } else if (command == "pause") dom.audio_pause(owner, token)
    else if (command == "close") {
        dom.audio_close(owner, token)
        retired = token
        token = null
    } else if (command == "stale") {
        dom.set_attribute(owner, "data-stale", string(dom.audio_play(owner, retired, 1.0)))
    }
    dom.set_attribute(owner, "data-state", string(dom.audio_state(owner, token) or "closed"))
    return 'handled'
}
<html <head <title "Native byte-backed audio probe">> <body apply(<audio_probe rendered:TREE>)>>
