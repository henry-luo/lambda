// Opt-in macOS smoke page for the real AVSpeechSynthesizer engine
// (Radiant_Design_Speech.md §8, M1): speaks en-US and zh-CN and counts word
// boundaries, then speaks <speak> markup with a bookmark. Run by RadiantViewTest.SpeechSmokeSpeaksWithBoundaries when
// LAMBDA_SPEECH_SMOKE=1; it needs an audio device and the default system voices.
import dom

view <smoke> state en: null, zh: null, mk: null, en_words: 0, zh_words: 0, mk_words: 0, en_done: false, zh_done: false, mk_done: false, mark: "", errors: "" {
    <div id: "smoke",
        <button id: "speak", "Speak">
        <output id: "en", if (en_done and en_words >= 3) "ok" else "pending " ++ string(en_words)>
        <output id: "zh", if (zh_done and zh_words >= 2) "ok" else "pending " ++ string(zh_words)>
        <output id: "markup", if (mk_done and mk_words >= 3 and mark == "middle") "ok" else "pending " ++ string(mk_words) ++ mark>
        <output id: "errors", errors>
    >
}
on click(evt) {
    let owner = dom.closest(evt.target, "#smoke")
    if (owner == null or dom.get_attribute(evt.target, "id") != "speak") { return 'pass' }
    en = dom.speak(owner, "Chengdu, inside the third ring road.", {lang: "en-US"})
    zh = dom.speak(owner, "成都，三环以内。", {lang: "zh-CN"})
    // markup goes to the engine as Apple SSML; the bookmark arrives as speechmark.
    mk = dom.speak(owner, <speak lang: "en-US", *["One ", <mark name: "middle">, "two", <break time: 0.3>,
        <prosody rate: 0.8, " three">]>, null)
    return 'handled'
}
on speechboundary(evt) {
    if (evt.kind != "word") { return 'handled' }
    if (evt.token == en) { en_words = en_words + 1 }
    if (evt.token == zh) { zh_words = zh_words + 1 }
    if (evt.token == mk) { mk_words = mk_words + 1 }
    return 'handled'
}
on speechmark(evt) { if (evt.token == mk) { mark = evt.name }; return 'handled' }
on speechend(evt) {
    if (evt.token == en) { en_done = true }
    if (evt.token == zh) { zh_done = true }
    if (evt.token == mk) { mk_done = true }
    return 'handled'
}
on speecherror(evt) { errors = errors ++ evt.reason ++ ";"; return 'handled' }

<html <body apply(<smoke>)>>
