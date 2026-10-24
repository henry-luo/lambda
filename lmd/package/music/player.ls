// presentation is pure; handlers own native transport effects (S12.1.1v2/D7.4.6).
import dom
import c: .common
import perf: .performance
import geometry: .layout
import engrave: .engrave
import svg: .svg
import numbers: lambda.chart.numbers

fn mask(channels) => sum([for (ch in channels) int(2**ch)])
let highlight_colors={red:"Red",blue:"Blue",yellow:"Yellow"}
let state_labels={ready:"Ready",playing:"Playing",paused:"Paused",ended:"Finished",error:"Unavailable"}
fn field(label,control,extra = "") element => <label
    style:"display:inline-flex;align-items:center;gap:8px;color:#64748b;font-size:12px;font-weight:500;white-space:nowrap;"++extra,
    label control>
fn button_style(primary) => "appearance:none;display:inline-flex;align-items:center;justify-content:center;"++
    "height:34px;box-sizing:border-box;padding:7px 13px;border-radius:7px;font:600 13px sans-serif;cursor:pointer;"++
    if (primary) "background:#24364b;border:1px solid #24364b;color:white" else "background:white;border:1px solid #d9e0e8;color:#475569"
fn swatch_style(selected) => "appearance:none;display:inline-flex;align-items:center;justify-content:center;"++
    "width:24px;height:24px;box-sizing:border-box;padding:2px;border-radius:6px;background:white;cursor:pointer;"++
    "border:2px solid "++if (selected) "#334155" else "transparent"
// reserve transport text space so fractional seconds and state labels cannot resize the seek field.
fn status_style(mode) => "display:inline-block;flex:0 0 auto;min-width:240px;text-align:right;"++
    "font:12px monospace;font-variant-numeric:tabular-nums;white-space:nowrap;color:"++
    if (mode=="error") "#b42318" else "#64748b"
fn tree(performance,sheet) element => <section class:"lambda-music-player",'data-state':"ready",
    style:"display:flex;flex-direction:column;height:100%;max-height:100vh;box-sizing:border-box;overflow:hidden;"++
        "font:13px sans-serif;color:#243247;background:#f1f4f8;padding:16px",
    <div class:"music-controls",style:"background:white;border:1px solid #dce3eb;border-radius:12px;"++
        "flex:0 0 auto;box-shadow:0 2px 8px rgba(15,23,42,0.04);padding:14px 16px;margin-bottom:18px",
        <div class:"music-toolbar",style:"display:flex;gap:16px;align-items:center;flex-wrap:wrap",
            <div class:"music-transport",style:"display:flex;gap:6px;align-items:center",
                for (action in [{id:"play",label:"Play"},{id:"pause",label:"Pause"},{id:"stop",label:"Stop"}])
                    <button type:"button",style:button_style(action.id=="play"),'data-action':action.id,action.label>>
            <div class:"music-tracks",style:"display:flex;gap:14px;align-items:center;flex-wrap:wrap",
                for (track in performance.tracks) <div class:"music-track",style:"display:flex;gap:6px;align-items:center",
                    <span style:"font-size:13px;font-weight:600;margin-right:2px",track.label>
                    for (control in [{id:"mute",label:"Mute"},{id:"solo",label:"Solo"}])
                        <label style:"display:inline-flex;gap:5px;align-items:center;padding:5px 7px;"++
                            "border:1px solid #e2e8f0;border-radius:6px;color:#64748b;font-size:12px;cursor:pointer",
                            <input type:"checkbox",style:"margin:0;width:13px;height:13px",'data-control':control.id,
                                'data-mask':string(mask(track.channels))> control.label>>>
            if (sheet!=null) <div class:"music-highlight",role:"group",'aria-label':"Note highlight color",'data-color':"red",
                style:"display:flex;gap:5px;align-items:center;margin-left:auto",
                <span style:"color:#64748b;font-size:12px;font-weight:500;margin-right:3px","Highlight">
                for (color,label in highlight_colors) <button type:"button",class:"music-swatch",'data-action':"highlight",
                    'data-color':string(color),'aria-label':label++" highlight",title:label,
                    'aria-pressed':string(string(color)=="red"),style:swatch_style(string(color)=="red"),
                    <span style:"display:block;width:16px;height:16px;border-radius:2px;"++
                        "box-shadow:inset 0 0 0 1px rgba(15,23,42,0.12);background:"++string(color)>>>
        >
        <div class:"music-timeline",style:"display:flex;gap:14px;align-items:center;flex-wrap:wrap;"++
            "margin-top:13px;padding-top:12px;border-top:1px solid #edf1f5",
            field("Position",<input type:"range",class:"music-seek",style:"flex:1;min-width:0;width:100%;margin:0",
                'data-control':"seek",min:"0",max:string(performance.seconds),step:"0.01",value:"0",'aria-label':"Playback position">,
                "flex:1;min-width:160px")
            field("Speed",<select 'data-control':"speed",'aria-label':"Playback speed",
                style:"height:30px;box-sizing:border-box;padding:4px 7px;border:1px solid #d9e0e8;"++
                    "border-radius:6px;background:white;color:#334155;font:13px sans-serif;cursor:pointer",
                for (speed in [0.5,0.75,1.0,1.25,1.5,2.0]) <option value:string(speed),
                    *:(if (speed==1.0) {selected:"selected"} else {}),string(speed)++"×">>)
            field("Volume",<input type:"range",style:"width:88px;margin:0",'data-control':"volume",min:"0",max:"1",step:"0.01",
                value:"0.8",'aria-label':"Volume">);
            <span class:"music-status",role:"status",style:status_style("ready"),"Ready">>>
    if (sheet!=null) <div class:"music-sheet-viewport",tabindex:"0",'aria-label':"Music sheet",
        style:"flex:1;min-height:0;min-width:0;overflow:auto;border-radius:10px",svg.document(sheet)^>>

pn status(owner,text,mode) {
    // unchanged attributes would recascade all controls and close an open native menu.
    if (dom.get_attribute(owner,"data-state")!=mode) dom.set_attribute(owner,"data-state",mode)
    let label=dom.query_selector(owner,".music-status")
    if (dom.text_content(label)!=text) dom.set_text_content(label,text)
    let style=status_style(mode)
    if (dom.get_attribute(label,"style")!=style) dom.set_attribute(label,"style",style)
}
fn control_value(owner,control) => ((if (control=="mute" or control=="solo")
    float(sum([for (input_node in dom.query_selector_all(owner,"input[data-control="++control++"]") where dom.get_state(input_node,"checked"))
        int(dom.get_attribute(input_node,"data-mask"))]) or 0) else
    float(dom.get_state(dom.query_selector(owner,"[data-control="++control++"]"),"value"))) ^ { null }) or -1.0
pn highlight(owner,session_value,active) {
    let control=dom.query_selector(owner,".music-highlight")
    let color=if (control==null) "red" else dom.get_attribute(control,"data-color") or "red"
    for (id in session_value.active where not contains(active,id)) {
        dom.remove_attribute(session_value.groups[id],"fill")
        dom.remove_attribute(session_value.groups[id],"style")
    }
    for (id in active where session_value.color!=color or not contains(session_value.active,id)) {
        // stems use stroke:currentColor; glyphs and beams use inherited fill.
        dom.set_attribute(session_value.groups[id],"style","color:"++color)
        dom.set_attribute(session_value.groups[id],"fill",color)
    }
    return {*:session_value,active:active,color:color}
}
pn choose_highlight(owner,color) {
    if (highlight_colors[color]==null) { return false }
    let picker=dom.query_selector(owner,".music-highlight")
    if (picker==null) { return false }
    if (dom.get_attribute(picker,"data-color")!=color) dom.set_attribute(picker,"data-color",color)
    for (swatch in dom.query_selector_all(picker,".music-swatch")) {
        let selected=dom.get_attribute(swatch,"data-color")==color
        if (dom.get_attribute(swatch,"aria-pressed")!=string(selected)) {
            dom.set_attribute(swatch,"aria-pressed",string(selected))
            dom.set_attribute(swatch,"style",swatch_style(selected))
        }
    }
    return true
}
pn follow_notes(owner,session_value,node,sounding) {
    if (len(sounding)==0) { return null }
    let pane=dom.query_selector(owner,".music-sheet-viewport")
    if (pane==null) { return null }
    let port=dom.bounding_box(pane)
    // svg groups have no CSS layout box; scale the engraving index through each page.
    let rects=[for (id in sounding,entry in [node.positions[id]] where entry!=null) (
        let page=dom.bounding_box(session_value.pages[entry.page]),
        let scale=page.width/node.page_widths[entry.page],
        {top:page.top+entry.bounds.top*scale,bottom:page.top+entry.bounds.bottom*scale})]
    if (len(rects)==0 or port.height<=0) { return null }
    let top=min([for (r in rects) r.top])
    let bottom=max([for (r in rects) r.bottom])
    let margin=min(24.0,port.height*0.1)
    if (top<port.top+margin or bottom>port.bottom-margin) {
        // keep the complete sounding group visible, or its leading note in a short pane.
        let y=if (bottom-top+2*margin>port.height) rects[0].top else top
        let scroll=dom.scroll_state(pane)
        dom.set_scroll_state(pane,scroll.x,max(0.0,scroll.y+y-port.top-margin))
    }
}
// The live audio session is recorded on its owner element. A player applied with
// another score is another template instance with fresh state on the same
// element (Reactive UI §5.1: state is keyed by the source item), so only the
// element knows which session is current: the new instance closes the old one,
// and the old instance stops once it no longer owns the element.
let SESSION="data-music-session"
let idle={token:null,frame:0,active:[],groups:{}}
pn close_session(owner,token) {
    dom.audio_close(owner,token)
    if (dom.get_attribute(owner,SESSION)==string(token)) dom.remove_attribute(owner,SESSION)
}
pn synchronize(owner,session_value,node) {
    let info=dom.midi_info(owner,session_value.token)
    if (info==null or info.state=="error") {
        status(owner,"Playback unavailable or audio device stopped","error")
        if (session_value.token!=null) close_session(owner,session_value.token)
        return {*:session_value,token:null,frame:0}
    }
    let occurrences=if (info.state=="playing" or info.state=="paused") [for (o in node.performance.occurrences where
        not o.rest and o.seconds<=info.position and o.end_seconds>info.position) o] else []
    let sounding=unique([for (o in occurrences) o.id])
    let active=unique([for (o in occurrences,key in (node.highlight_links[o.id] or [o.id]) where
        len(node.lyric_passes[key] or [])==0 or contains(node.lyric_passes[key],o.pass)) key])
    let highlighted=highlight(owner,session_value,active)
    if (sounding!=session_value.sounding) follow_notes(owner,session_value,node,sounding)
    dom.set_state(dom.query_selector(owner,".music-seek"),"value",string(info.position))
    status(owner,state_labels[info.state]++" · "++numbers.format_number(info.position,".1f")++" / "++
        numbers.format_number(info.duration,".1f")++" s · "++string(info.speed)++"×",info.state)
    return {*:highlighted,sounding:sounding,frame:if (info.state=="playing") dom.request_frame(owner,"music_tick") else 0}
}
pn handle(node,session_value,evt) {
    let owner=dom.closest(evt.target,".lambda-music-player")
    if (owner==null) { return session_value }
    var current=session_value
    // a stale instance: the element now presents another score, whose instance closed this session
    if (current.token!=null and dom.get_attribute(owner,SESSION)!=string(current.token)) { return idle }
    if (evt.type=="render") {
        dom.watch_window(owner,"closerequest",true)
        dom.watch_window(owner,"resize",true)
        let live=dom.get_attribute(owner,SESSION)
        if (current.token==null and live!=null) {
            // the score presented before this one is still playing on this element
            close_session(owner,int(live))
            dom.set_state(dom.query_selector(owner,".music-seek"),"value","0")
            status(owner,state_labels.ready,"ready")
        }
        return current
    }
    if (evt.type=="resize") {
        dom.request_frame(owner,"music_follow")
        return current
    }
    if (evt.type=="music_follow") {
        follow_notes(owner,current,node,current.sounding or [])
        return current
    }
    if (evt.type=="closerequest") {
        if (current.token!=null) close_session(owner,current.token)
        if (current.frame>0) dom.cancel_frame(owner,current.frame)
        return idle
    }
    if (evt.type=="music_tick") {
        if (current.token==null) { return current }
        return synchronize(owner,{*:current,frame:0},node)
    }
    let action_target=dom.closest(evt.target,"[data-action]")
    let action=if (action_target==null) null else dom.get_attribute(action_target,"data-action")
    let control=dom.get_attribute(evt.target,"data-control")
    // native controls own clicks; apply their committed value only on change.
    if (evt.type=="click" and action==null or evt.type=="change" and control==null) { return current }
    if (action=="highlight") {
        if (choose_highlight(owner,dom.get_attribute(action_target,"data-color"))) { return highlight(owner,current,current.active) }
        return current
    }
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
        dom.set_attribute(owner,SESSION,string(token))
        current={token:token,frame:0,active:[],
            pages:dom.query_selector_all(owner,".lambda-music-page svg"),sounding:[],
            groups:map([for (g in dom.query_selector_all(owner,"[data-highlight-id]")) (dom.get_attribute(g,"data-highlight-id"),g)])}
        // controls edited before opening the device still define the first audible state.
        for (control_name in ["speed","volume","mute","solo","seek"]) {
            if (not dom.midi_command(owner,token,control_name,control_value(owner,control_name))) {
                close_session(owner,token)
                status(owner,"Invalid playback control: "++control_name,"error")
                return {*:current,token:null}
            }
        }
    }
    if (current.frame>0) dom.cancel_frame(owner,current.frame)
    let command=action or control
    let value=if (action!=null) 0.0 else control_value(owner,control)
    if (not dom.midi_command(owner,current.token,command,value)) {
        close_session(owner,current.token)
        status(owner,"Playback command failed","error")
        return {*:current,token:null,frame:0}
    }
    return synchronize(owner,{*:current,frame:0},node)
}

view <music_player> state session:idle { ~.tree }
on render(evt) { session=handle(~,session,evt); return 'pass' }
on click(evt) { session=handle(~,session,evt); return 'handled' }
on change(evt) { session=handle(~,session,evt); return 'handled' }
on music_tick(evt) { session=handle(~,session,evt); return 'handled' }
on resize(evt) { session=handle(~,session,evt); return 'pass' }
on music_follow(evt) { session=handle(~,session,evt); return 'handled' }
on closerequest(evt) { session=handle(~,session,evt); return 'pass' }

pub fn create(source,options = {}) element^ {
    let issues=c.option_warnings(options,["strict","layout","performance","soundfont"])^
    let checked=c.require(source.kind=="music-score" or source.kind=="music-performance","player expects score or performance")^
    let performance=if (source.kind=="music-score") perf.build(source,options.performance or {})^ else source
    let sheet=if (source.kind=="music-score") engrave.finish(geometry.layout(source,options.layout or {})^)^ else null
    let font=options.soundfont or (sys.lambda.home#++"/package/music/soundfonts/TimGM6mb.sf2")
    let warnings=c.warnings(issues,options)^;
    apply(<music_player performance:performance,soundfont:font,
        positions:if (sheet==null) {} else sheet.events,
        page_widths:if (sheet==null) [] else [for (page in sheet.pages) page.width*sheet.unit],
        lyric_passes:if (sheet==null) {} else sheet.lyric_passes,
        highlight_links:if (sheet==null) {} else engrave.highlight_bindings(sheet),tree:tree(performance,sheet)>)
}
