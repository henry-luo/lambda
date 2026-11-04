// D9.5.6 / D11.3.1: acquire bytes in Lambda and keep one native cue voice per player.
import dom
import media: lambda.doc.media

pub pn close(owner,voice) {
    if (voice.token != null) dom.audio_close(owner,voice.token)
}
pub pn sync(owner,voice,plan,ps,stopped=false) map {
    if (stopped) { close(owner,voice); return {slide:ps.slide,token:null,error:null} }
    var current = voice
    if (voice == null or voice.slide != ps.slide) {
        close(owner,voice)
        current = {slide:ps.slide,token:null,error:null}
        let source = plan.slides[ps.slide].transition_sound
        if (source != null) {
            let src = if (plan.base_uri == null or starts_with(source.src,"data:")) source.src else url_resolve(plan.base_uri,source.src)
            var bytes = null
            if (starts_with(src,"data:") ^ { return {*:current,error:^.message} }) { let __error_outcome_1 = media.data_uri_bytes(src) ^ { ^ }; if (__error_outcome_1 is error) { current = {*:current,error:__error_outcome_1.message} } else match __error_outcome_1 { case any { bytes = ~ } } }
            else { let __error_outcome_2 = input(src,'binary') ^ { ^ }; if (__error_outcome_2 is error) { current = {*:current,error:__error_outcome_2.message} } else match __error_outcome_2 { case any { bytes = ~ } } }
            if (bytes != null) {
                let token = dom.audio_open(owner,bytes)
                if (token == null) { current = {*:current,error:"slide: transition sound is unavailable (check the file and audio output)"} }
                else if (not dom.audio_play(owner,token,1.0)) {
                    dom.audio_close(owner,token)
                    current = {*:current,error:"slide: transition sound could not start playback"}
                } else { current = {*:current,token:token} }
            }
        }
    }
    if (current.token != null) {
        let status = dom.audio_state(owner,current.token)
        if (status == "ended" or status == "error") {
            close(owner,current)
            current = {*:current,token:null,error:if (status == "error") "slide: transition sound playback failed" else null}
        } else if (ps.paused) dom.audio_pause(owner,current.token)
        else if (status == "paused") dom.audio_play(owner,current.token,1.0)
    }
    return current
}
