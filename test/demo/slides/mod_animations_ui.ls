import ribbon: ~~.mod_ribbon
import catalog: lambda.slide.animation
import model: .mod_animations
import m: .mod_model
import c: lambda.slide.common

fn star(entry) => <svg width:"30",height:"30",viewBox:"0 0 32 32",'aria-hidden':"true",
    <path d:if (entry.family == 'motion') "M3 25C3 6 29 6 29 25M23 20l6 5 1-8" else "M16 2l4 9h10l-8 7 3 11-9-6-9 6 3-11-8-7h10z",
        fill:if (entry.family == 'entrance') "#4c995c" else if (entry.family == 'exit') "#c75454" else if (entry.family == 'motion') "none" else "#ba922e",
        stroke:if (entry.family == 'motion') "#587eac" else "none",'stroke-width':"2">>
fn tile(entry) => <button id:"animate-" ++ string(entry.id),type:"button",class:"animation-tile",'data-action':"animation-choose:" ++ string(entry.id),
    title:entry.label,'aria-label':entry.label,'aria-pressed':"false",star(entry);<span *ribbon.label_lines(entry.label)>>
fn select(id,label,values) => <label id:"animation-field-" ++ id,class:"transition-field",<span label>;
    <select id:"animation-" ++ id,'data-property':"animation-" ++ id,'aria-label':label,
        for (entry in values) <option value:entry[0],entry[1]>>>
fn numeric(id,label,step="0.1") => <label id:"animation-field-" ++ id,class:"transition-field",<span label>;
    <input id:"animation-" ++ id,type:"number",step:step,'data-property':"animation-" ++ id,'aria-label':label>>
pub fn tree() => <div class:"animations-ribbon editor-ribbon",
    ribbon.group("Preview",[ribbon.tool("animation-preview","Preview","play","home-large")]);
    *[for (family in ['entrance','emphasis']) ribbon.group(if (family == 'entrance') "Entrance" else "Emphasis",[
        <div class:"animation-gallery",role:"group",'aria-label':string(family) ++ " animations",for (i,entry in [for (candidate in catalog.catalog where candidate.family == family) candidate] where (i < 5)) tile(entry)>,
        ribbon.menu("animation-" ++ string(family),"More","shapes","home-large")])];
    ribbon.group("Effects",[ribbon.menu("animation-exit","Exit Effects","shapes","home-large"),ribbon.menu("animation-motion","Path Animation","object_position","home-large")]);
    ribbon.group("Advanced Animation",[ribbon.menu("animation-add","Add Animation","shapes","home-large"),
        ribbon.stack([ribbon.tool("animation-options","Effect Options","settings"),ribbon.tool("animation-pane-toggle","Animation Pane","selection_pane")]),
        ribbon.stack([ribbon.menu("animation-trigger","Trigger","link"),ribbon.tool("animation-painter","Animation Painter","format_painter")])]);
    ribbon.group("Timing",[ribbon.stack([
        select("start","Start",[["click","On Click"],["with-previous","With Previous"],["after-previous","After Previous"],["entry","On Slide Entry"]]),
        numeric("duration","Duration (s)"),numeric("delay","Delay (s)")])])>
pub fn menu_tree(st,key) {
    if (key == "animation-trigger") [ribbon.item("animation-trigger:","Normal click sequence","play"),
        *[for (target in m.scene(st).targets where target.source.id != null)
            ribbon.item("animation-trigger:" ++ target.id,"On click of " ++ target.id,"link")]]
    else [for (entry in catalog.catalog where key == "animation-add" or
        (key == "animation-entrance" and entry.family == 'entrance') or (key == "animation-emphasis" and entry.family == 'emphasis') or
        (key == "animation-exit" and entry.family == 'exit') or (key == "animation-motion" and entry.family == 'motion'))
        ribbon.item((if (key == "animation-add") "animation-add:" else "animation-choose:") ++ string(entry.id),entry.label,star(entry),not model.compatible(m.chosen(st),entry.id))]
}
pub fn pane() => <aside id:"animation-pane",class:"animation-pane",
    <div class:"panel-heading",<b "Animation Pane">;ribbon.tool("animation-pane-close","Close","close")>;
    <div class:"animation-pane-actions",ribbon.tool("animation-up","Earlier","arrow_up");ribbon.tool("animation-down","Later","arrow_down");ribbon.tool("animation-remove","Remove","delete")>;
    <div id:"animation-list",role:"list",'aria-label':"Slide animations">;
    <p id:"animation-empty",class:"help","Select an object, then choose an effect. Use Add Animation to layer more effects.">;
    <div id:"animation-details",
        <h3 "Effect Options">;
        <p id:"animation-group-help",class:"help">;
        select("from","Direction",[["left","From Left"],["right","From Right"],["top","From Top"],["bottom","From Bottom"]]);
        <label id:"animation-field-color",class:"field",<span "Color">;<input id:"animation-color",type:"text",'data-property':"animation-color",'aria-label':"Effect color">>;
        numeric("scale","Scale","0.05");numeric("angle","Angle (degrees)","15");numeric("distance","Distance (px)","10");numeric("opacity","Opacity","0.05");
        select("path-preset","Path",[*[for (entry in catalog.paths) [entry.id,entry.label]],["custom","Custom Path"]]);
        select("orient","Orientation",[["none","Keep Orientation"],["auto","Follow Path"]]);
        select("easing","Easing",[["linear","Linear"],["ease","Ease"],["ease-in","Ease In"],["ease-out","Ease Out"],["ease-in-out","Ease In / Out"],["ease-in-cubic","Cubic In"],["ease-out-cubic","Cubic Out"],["ease-in-out-cubic","Cubic In / Out"],["custom","Custom Curve"]]);
        numeric("repeat","Repeat","1");
        <p id:"animation-trigger-label",class:"help">>;
    <button id:"clear-animations",type:"button",'data-action':"clear-animations","Clear slide animations">>
pub fn rows(st) {
    let active = model.selected(st);
    [for (i,row in model.entries(st)) <button type:"button",class:"animation-row",'data-action':"animation-select:" ++ string(row.key),
        'data-animation':string(row.key),'aria-pressed':row.key == active.key,
        <b string(i+1) ++ ". " ++ row.label>;<span string(row.target_label)>;
        <small if (row.cue.trigger != null) "Click " ++ string(row.cue.trigger) else string(c.value(row.cue.start,'click'))>>]
}
