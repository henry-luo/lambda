import music: lambda.music
import dom
let PERFORMANCE=music.performance(music.load("test/music/study.musicxml")^)^
let FONT=sys.lambda.home#++"/package/music/soundfonts/TimGM6mb.sf2"
view <music_lifecycle> state token:null,saved:null,report:"ready" {
    <section
        for (action in ["open","play","seek","speed","mute","solo","stop","remove","read","stale"])
            <button type:"button",id:action,action>;
        <output id:"report",report>>
}
on click(evt) {
    let root=dom.document_element(evt.target)
    let action=dom.get_attribute(evt.target,"id")
    if (action=="open") {
        saved=dom.query_selector(root,"#voice-owner")
        token=dom.midi_open(saved,PERFORMANCE,input(FONT,'binary')^)
        report=if (token==null) "failed" else "opened"
    } else if (action=="read") {
        report=if (dom.midi_info(saved,token)==null) "closed" else "live"
    } else if (action=="stale") {
        report=string(dom.midi_command(saved,token,"play",0.0))
    } else if (action=="remove") {
        dom.remove_child(dom.parent_node(saved),saved)
        report="removed"
    } else {
        let value=if (action=="seek") 2.0 else if (action=="speed") 1.5 else if (action=="mute" or action=="solo") 1.0 else 0.0
        let accepted=dom.midi_command(saved,token,action,value)
        let info=dom.midi_info(saved,token)
        report=if (not accepted) "failed" else action++":"++string(if (action=="seek") round(info.position)
            else if (action=="speed") info.speed else if (action=="mute") info.mute else if (action=="solo") info.solo else info.state)
    }
    return 'handled'
}
<html <body <div id:"voice-owner"> apply(<music_lifecycle>)>>
