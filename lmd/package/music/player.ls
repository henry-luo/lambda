// presentation is pure; handlers own native transport effects (S12.1.1v2/D7.4.6).
import dom
import c: .common
import perf: .performance
import geometry: .layout
import engrave: .engrave
import svg: .svg

fn mask(channels) => sum([for (ch in channels) int(2**ch)])
fn field(label,control) element => <label style:"display:inline-flex;align-items:center;gap:6px",label control>
fn tree(performance,sheet) element => <section class:"lambda-music-player",'data-state':"ready",
    style:"font:15px sans-serif;color:#18202b;background:#f4f5f7;padding:16px",
    <div class:"music-controls",style:"display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:12px",
        for (action in [{id:"play",label:"Play"},{id:"pause",label:"Pause"},{id:"stop",label:"Stop"}])
            <button type:"button",style:"padding:7px 12px",'data-action':action.id,action.label>;
        field("Position",<input type:"range",class:"music-seek",'data-control':"seek",min:"0",max:string(performance.seconds),step:"0.01",value:"0",'aria-label':"Playback position">)
        field("Speed",<select 'data-control':"speed",'aria-label':"Playback speed",
            for (speed in [0.5,0.75,1.0,1.25,1.5,2.0]) <option value:string(speed),*:(if (speed==1.0) {selected:"selected"} else {}),string(speed)++"×">>)
        field("Volume",<input type:"range",'data-control':"volume",min:"0",max:"1",step:"0.01",value:"0.8",'aria-label':"Volume">);
        <span class:"music-status",role:"status","Ready">>
    <div class:"music-tracks",style:"display:flex;gap:16px;flex-wrap:wrap;margin-bottom:12px",
        for (track in performance.tracks) <div class:"music-track",style:"display:flex;gap:8px;align-items:center",
        <span track.label>
        for (control in [{id:"mute",label:"Mute"},{id:"solo",label:"Solo"}])
            field(control.label,<input type:"checkbox",'data-control':control.id,'data-mask':string(mask(track.channels))>)>>
    if (sheet!=null) svg.document(sheet)^>

pn status(owner,text,mode) {
    dom.set_attribute(owner,"data-state",mode)
    let label=dom.query_selector(owner,".music-status")
    dom.set_text_content(label,text)
    dom.set_attribute(label,"style",if (mode=="error") "color:#a21b28" else "color:#18202b")
}
fn control_value(owner,control) => (if (control=="mute" or control=="solo")
    float(sum([for (input_node in dom.query_selector_all(owner,"input[data-control="++control++"]") where dom.get_state(input_node,"checked"))
        int(dom.get_attribute(input_node,"data-mask"))]) or 0) else
    float(dom.get_state(dom.query_selector(owner,"[data-control="++control++"]"),"value"))) or -1.0
pn synchronize(owner,session_value,performance) {
    let info=dom.midi_info(owner,session_value.token)
    if (info==null or info.state=="error") {
        status(owner,"Playback unavailable or audio device stopped","error")
        if (session_value.token!=null) dom.audio_close(owner,session_value.token)
        return {*:session_value,token:null,frame:0}
    }
    let active=unique([for (o in performance.occurrences where not o.rest and o.seconds<=info.position and o.end_seconds>info.position) o.id])
    if (active!=session_value.active) {
        for (id in session_value.active where not contains(active,id)) dom.remove_attribute(session_value.groups[id],"fill")
        for (id in active where not contains(session_value.active,id)) dom.set_attribute(session_value.groups[id],"fill","#1565c0")
    }
    dom.set_state(dom.query_selector(owner,".music-seek"),"value",string(info.position))
    status(owner,info.state++" · "++string(round(info.position*10)/10)++" / "++string(round(info.duration*10)/10)++" s",info.state)
    return {*:session_value,active:active,frame:if (info.state=="playing") dom.request_frame(owner,"music_tick") else 0}
}
pn handle(node,session_value,evt) {
    let owner=dom.closest(evt.target,".lambda-music-player")
    if (owner==null) { return session_value }
    var current=session_value
    if (evt.type=="render") {
        dom.watch_window(owner,"closerequest",true)
        if (current.token!=null and current.performance!=node.performance) {
            dom.audio_close(owner,current.token)
            return {token:null,frame:0,active:[],groups:{},performance:null}
        }
        return current
    }
    if (evt.type=="closerequest") {
        if (current.token!=null) dom.audio_close(owner,current.token)
        if (current.frame>0) dom.cancel_frame(owner,current.frame)
        return {token:null,frame:0,active:[],groups:{},performance:null}
    }
    if (evt.type=="music_tick") {
        if (current.token==null) { return current }
        return synchronize(owner,{*:current,frame:0},node.performance)
    }
    let action=dom.get_attribute(evt.target,"data-action")
    let control=dom.get_attribute(evt.target,"data-control")
    if (action==null and control==null) { return current }
    if (current.token==null) {
        if (action!="play") { return current }
        var problem=""
        var token:any=null
        let bytes=input(node.soundfont,'binary') ^ { problem=^.message }
        if (problem=="") { token=dom.midi_open(owner,node.performance,bytes) }
        if (token==null) {
            status(owner,if (problem!="") problem else "Cannot open SoundFont or audio device","error")
            return current
        }
        current={token:token,frame:0,active:[],performance:node.performance,
            groups:map([for (g in dom.query_selector_all(owner,"[data-event-id]")) (dom.get_attribute(g,"data-event-id"),g)])}
        // controls edited before opening the device still define the first audible state.
        for (control_name in ["speed","volume","mute","solo","seek"]) {
            if (not dom.midi_command(owner,token,control_name,control_value(owner,control_name))) {
                dom.audio_close(owner,token)
                status(owner,"Invalid playback control: "++control_name,"error")
                return {*:current,token:null}
            }
        }
    }
    if (current.frame>0) dom.cancel_frame(owner,current.frame)
    let command=action or control
    let value=if (action!=null) 0.0 else control_value(owner,control)
    if (not dom.midi_command(owner,current.token,command,value)) {
        dom.audio_close(owner,current.token)
        status(owner,"Playback command failed","error")
        return {*:current,token:null,frame:0}
    }
    return synchronize(owner,{*:current,frame:0},node.performance)
}

view <music_player> state session:{token:null,frame:0,active:[],groups:{},performance:null} { ~.tree }
on render(evt) { session=handle(~,session,evt); return 'pass' }
on click(evt) { session=handle(~,session,evt); return 'handled' }
on change(evt) { session=handle(~,session,evt); return 'handled' }
on music_tick(evt) { session=handle(~,session,evt); return 'handled' }
on closerequest(evt) { session=handle(~,session,evt); return 'pass' }

pub fn create(source,options = {}) element^ {
    let issues=c.option_warnings(options,["strict","layout","performance","soundfont"])^
    let checked=c.require(source.kind=="music-score" or source.kind=="music-performance","player expects score or performance")^
    let performance=if (source.kind=="music-score") perf.build(source,options.performance or {})^ else source
    let sheet=if (source.kind=="music-score") engrave.finish(geometry.layout(source,options.layout or {})^)^ else null
    let font=options.soundfont or (sys.lambda.home#++"/package/music/soundfonts/TimGM6mb.sf2")
    let warnings=c.warnings(issues,options)^;
    apply(<music_player performance:performance,soundfont:font,tree:tree(performance,sheet)>)
}
