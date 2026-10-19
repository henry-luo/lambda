// Speech conformance page for the fake engine on offline output
// (LAMBDA_SPEECH_ENGINE=fake, LAMBDA_AUDIO_OUTPUT=offline; Radiant_Design_Speech.md §6):
// each word is a 0.25 s tone and 0.05 s rest.
import dom

// voices are listed best first, with their quality and gender.
fn best_voice() {
    let v = dom.speech_voices()[0]
    v.lang ++ " " ++ v.gender ++ " " ++ v.quality
}

view <narrator> state token: null, log: "", state_name: "none", refused: 0, errors: 0 {
    <div id: "narrator",
        <button id: "say", "Say">
        <button id: "markup", "Markup">
        <button id: "long", "Long">
        <button id: "cancel", "Cancel">
        <button id: "bad", "Bad">
        <button id: "info", "Info">
        <button id: "cue", "Cue">
        <button id: "late", "Late">
        <output id: "voices", string(len(dom.speech_voices()))>
        <output id: "best", best_voice()>
        <output id: "log", log>
        <output id: "state", state_name>
        <output id: "refusals", string(refused) ++ "/" ++ string(errors)>
    >
}
on click(evt) {
    let id = dom.get_attribute(evt.target, "id")
    let owner = dom.closest(evt.target, "#narrator")
    if (owner == null) { return 'pass' }
    if (id == "say") { token = dom.speak(owner, "Hello big world. Bye.", {lang: "en-US"}) }
    if (id == "markup") {
        token = dom.speak(owner, <speak lang: "en-US", *["One ", <mark name: "half">, "two", <break time: 0.5>, " three"]>, null)
    }
    if (id == "long") { token = dom.speak(owner, "a b c d e f g h", null) }
    if (id == "cancel") { dom.speech_command(owner, token, "cancel") }
    if (id == "bad") {
        // each refusal returns null and raises speecherror on the owner (S7.4.2).
        let bad = [<speak *[<bogus "x">]>, <speak *[<break time: 99>]>, <prosody rate: 2, "x">,
            <speak *[<mark>]>, <speak *[<emphasis level: "loud", "x">]>, 42]
        for (u in bad) { if (dom.speak(owner, u, null) == null) { refused = refused + 1 } }
        let options = [{rate: 50}, {at: -1}, {voice: "nope"}]
        for (o in options) { if (dom.speak(owner, "hi", o) == null) { refused = refused + 1 } }
    }
    // `at` is seconds on the document sub-mix clock (Radiant_Design_Speech §3.4).
    if (id == "cue") { token = dom.speak(owner, "cue", {at: dom.audio_clock(owner) + 0.5}) }
    if (id == "late") { token = dom.speak(owner, "late", {at: 0}) }
    if (id == "info") {
        let info = dom.speech_info(owner, token)
        state_name = if (info == null) "missing" else info.state
    }
    return 'handled'
}
on speechstart(evt) { log = log ++ (if (evt.delay > 0) "late;" else "start;"); return 'handled' }
on speechboundary(evt) {
    let kind = if (evt.kind == "sentence") "s" else "w"
    log = log ++ kind ++ string(evt.char_index) ++ ":" ++ string(evt.char_length) ++ ";"
    return 'handled'
}
on speechmark(evt) { log = log ++ "m=" ++ evt.name ++ ";"; return 'handled' }
on speechend(evt) { log = log ++ (if (evt.cancelled) "cancelled;" else "end;"); return 'handled' }
on speecherror(evt) { errors = errors + 1; return 'handled' }

<html <body apply(<narrator>)>>
