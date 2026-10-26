// Cue selection is pure; native playback belongs to handlers (S12.1.3, D7.5.4).
import dom
import model: .mod_model

pub let MAX_VOICES = 8
let VOLUME = {drop:.45, switch:.28, catch:.5, pocket:.42, steel:.35, miss:.4, win:.42}
pub fn initial() => {enabled:true,failed:false,voices:[],cache:{},played:0,last:""}
pub fn status(sound) => if (sound.failed) "unavailable" else if (sound.enabled) "on" else "off"

pub fn cue(before, after, command) {
    if (contains(["undo","restart","level","next"],command) or before.stage != after.stage) ""
    else if (before.active == null and after.active != null and after.moves == before.moves+1) "drop"
    else if (command != "tick" or before.active == null or before.paused) ""
    else {
        let node = model.level(before).nodes[before.active.to]
        if (after.active != null) {
            if (after.active.from == before.active.from) ""
            else if (node.kind == "spring") "steel"
            else if (model.indexed(node) or model.selective(node) or node.kind == "relay") "switch" else ""
        }
        else if (after.mode == "won") "win"
        else if (model.catching(node) and before.bits[before.active.to] < node.threshold) "catch"
        else if (after.losses > before.losses) "miss"
        else if (model.collected(after) > model.collected(before)) "pocket"
        else if (before.active.color == 6) "steel"
        else ""
    }
}

pub pn silence(owner, sound) {
    for (token in sound.voices) dom.audio_close(owner,token)
    return {*:sound,voices:[]}
}
pub pn toggle(owner, sound) {
    let quiet = silence(owner,sound)
    return {*:quiet,enabled:not sound.enabled,failed:false}
}
pn play(owner, sound, name, base) {
    var voices = []
    for (token in sound.voices) {
        let voice_state = dom.audio_state(owner,token)
        if (voice_state == "playing") { voices = [*voices,token]; continue }
        dom.audio_close(owner,token)
    }
    // Keep overlapping glass tails, but bound native players during fast Auto Play.
    if (len(voices) >= MAX_VOICES) {
        dom.audio_close(owner,voices[0])
        voices = slice(voices,1,len(voices))
    }
    let bytes = sound.cache[name] or (input(url_resolve(base,"assets/sounds/" ++ name ++ ".wav"),'binary') ^ { null })
    let token = if (bytes == null) null else dom.audio_open(owner,bytes)
    if (token == null or not dom.audio_play(owner,token,VOLUME[name])) {
        if (token != null) dom.audio_close(owner,token)
        return {*:silence(owner,{*:sound,voices:voices}),failed:true}
    }
    return {*:sound,voices:[*voices,token],cache:{*:sound.cache,[name]:bytes},played:sound.played+1,last:name}
}
pub pn follow(owner, sound, before, after, command, base) {
    // Old sounds must not follow the player back through time or into another machine.
    if (contains(["undo","restart","level","next"],command) or (after.paused and not before.paused)) {
        return silence(owner,sound)
    }
    if (not sound.enabled or sound.failed or after.paused) { return sound }
    let name = cue(before,after,command)
    if (name == "") { return sound }
    return play(owner,sound,name,base)
}
