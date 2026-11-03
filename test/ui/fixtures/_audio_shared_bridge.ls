// import-only host fixture; exercises existing pn rows without a new public API.
import dom

pub pn request(owner, operation, token, value) map {
    let result = if (operation == "open") dom.audio_open(owner, value)
        else if (operation == "play") dom.audio_play(owner, token, value)
        else if (operation == "pause") dom.audio_pause(owner, token)
        else if (operation == "close") dom.audio_close(owner, token)
        else null
    return {result: result, state: dom.audio_state(owner, token), clock: dom.audio_clock(owner)}
}

pub pn provider_request(owner, operation, token, value) {
    if (operation == "midi_open") {
        let font=input("lmd/package/music/soundfonts/TimGM6mb.sf2",'binary')^
        return dom.midi_open(owner,{seconds:0.5,events:[{seconds:0,status:144,a:60,b:100},{seconds:0.25,status:128,a:60,b:0}]},font)
    }
    if (operation == "speech_open") {
        let handle=dom.speak(owner,"go go go",null)
        dom.speech_command(owner,handle,"pause")
        return handle
    }
    let handle=int(token)
    if (operation == "midi_read") { return dom.midi_info(owner,handle) }
    if (operation == "speech_read") { return dom.speech_info(owner,handle) }
    if (operation == "midi_close") { return dom.audio_close(owner,handle) }
    if (operation == "midi_command") { return dom.midi_command(owner,handle,value,0) }
    if (operation == "speech_command") { return dom.speech_command(owner,handle,value) }
    return null
}
