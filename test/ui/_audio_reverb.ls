// MIDI reverb send through the radiant-dom rows, on offline output
// (LAMBDA_AUDIO_OUTPUT=offline; Radiant_Design_Speech.md §3.2): sessions start
// with a light send, `reverb` sets it from 0 to 1, and other values are refused.
import dom

let font = sys.lambda.home# ++ "/package/music/soundfonts/TimGM6mb.sf2"
let tune = {kind: "music-performance", seconds: 1.0,
    events: [{seconds: 0.0, status: 144, a: 60, b: 100}, {seconds: 0.5, status: 128, a: 60, b: 0}]}

view <reverb_probe> state token: null, log: "" {
    <div id: "probe",
        <button id: "run", "Run">
        <output id: "log", log>
    >
}
on click(evt) {
    let owner = dom.closest(evt.target, "#probe")
    if (owner == null) { return 'pass' }
    var problem = ""
    let bytes = input(font, 'binary') ^ { problem = ^.message }
    if (problem != "") { log = "no font"; return 'handled' }
    token = dom.midi_open(owner, tune, bytes)
    if (token == null) { log = "no session"; return 'handled' }
    let first = dom.midi_info(owner, token).reverb
    let set = dom.midi_command(owner, token, "reverb", 0.5)
    let second = dom.midi_info(owner, token).reverb
    let refused = not dom.midi_command(owner, token, "reverb", 1.5)
    let dry = dom.midi_command(owner, token, "reverb", 0.0)
    log = string(round(first * 10)) ++ ";" ++ string(set) ++ ";" ++ string(second) ++ ";" ++ string(refused) ++ ";" ++
        string(dry) ++ ";" ++ string(dom.midi_info(owner, token).reverb)
    dom.audio_close(owner, token)
    return 'handled'
}

<html <body apply(<reverb_probe>)>>
