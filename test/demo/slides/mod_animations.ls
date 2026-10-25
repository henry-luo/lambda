// S1.4 / S12.1.1v3: effects, cue groups and history are immutable authored values.
import m: .mod_model
import c: lambda.slide.common
import catalog: lambda.slide.animation

pub fn cues(st) {
    let src = m.scene(st).source;
    if (src is element) c.children(src,'cue') else []
}
fn leaves(node,ci,path) => [for (i,child in content(node)) {
    let key = path ++ "." ++ string(i);
    *if (name(child) == 'effect') [{key:c.value(child.id,key),path:key,ci:ci,effect:child}]
    else leaves(child,ci,key)
}]
pub fn entries(st) => [for (ci,cue in cues(st)) for (row in leaves(cue,ci,string(ci))) {
    let target = [for (obj in m.scene(st).targets where obj.id == string(row.effect.target)) obj][0]
    let spec = catalog.descriptor(row.effect.kind);
    {*:row,cue:cue,target:target,label:c.value(spec.label,string(row.effect.kind)),
        target_label:c.value(target.source.id,c.value(target.source.role,target.tag))}
}]
pub fn selected(st) {
    let rows = entries(st)
    let explicit = [for (row in rows where row.key == st.animation_key and row.target.dom_key == st.selected) row];
    if (len(explicit)>0) explicit[0] else [for (row in rows where row.target.dom_key == st.selected) row][0]
}
pub fn select(st,key) {
    let row = [for (entry in entries(st) where entry.key == key) entry][0];
    if (row == null) st else {*:st,animation_key:key,selected:row.target.dom_key,animation_pane:true,error:""}
}
fn rewrite(node,path,target,change) => m.rebuild(node,map(node),[for (i,child in content(node)) {
    let key = path ++ "." ++ string(i)
    let next = if (key == target) change(child) else if (name(child) in ['parallel','sequence']) rewrite(child,key,target,change) else child;
    *if (next == null or (name(next) in ['parallel','sequence'] and len(content(next))==0)) [] else [next]
}])
fn commit_edit(st,items,objects=null,key=null) any^ {
    let tail = [*[for (child in content(m.scene(st).source) where not contains(c.objects(m.scene(st).source),child) and name(child) != 'cue') child],*items];
    {*:m.commit_doc(st,m.scene_doc(st,c.value(objects,c.objects(m.scene(st).source)),{},tail))^,
        animation_key:key,animation_pane:true}
}
fn effect_id(st) => m.free_object_id([for (row in entries(st) where row.effect.id != null) string(row.effect.id)],"animation-",1)
pub fn compatible(obj,kind) => obj != null and
    (if (kind == 'highlight') obj.tag in ['shape','text']
     else if (kind == 'line-color') obj.tag == 'shape' and obj.source.stroke != null and obj.source.stroke != "none" and c.num(obj.source,"stroke_width",1.0)>0.0 else true)
pub fn choose(st,kind,append=false) any^ {
    let obj = m.chosen(st)
    let row = selected(st)
    let spec = catalog.descriptor(kind)
    let checked_kind = if (spec == null or not compatible(obj,kind)) raise error("Slides: select a compatible object for this effect")
    let id = if (not append and row.effect.id != null) string(row.effect.id) else effect_id(st)
    // switching from an instantaneous effect must restore a playable duration.
    let duration = if (kind in ['appear','disappear']) 0.0 else if (c.num(row.effect,"duration",0.0)>0.0)
        row.effect.duration else catalog.defaults(kind).duration
    let settings = {*:catalog.defaults(kind),*:if (append or row == null) {} else
        {duration:duration,delay:c.num(row.effect,"delay",0.0),easing:c.value(row.effect.easing,'linear'),repeat:c.num(row.effect,"repeat",1)}};
    if (not append and row != null) commit_edit(st,[for (i,cue in cues(st)) if (i == row.ci)
        rewrite(cue,string(i),row.path,(node)=><effect *:settings,id:id,target:node.target>) else cue],null,id)^
    else {
        let pin = m.animation_target(st);
        commit_edit(st,[*cues(st),<cue start:'click',<effect *:settings,id:id,target:pin.id>>],pin.objects,id)^
    }
}
pub fn property(st,key,value) any^ {
    let row = selected(st)
    let checked = if (row == null) raise error("Slides: select an animation in the pane")
    let id = c.value(row.effect.id,effect_id(st))
    let numeric = key in ["duration","delay","scale","angle","distance","repeat","opacity"]
    let parsed = if (key == "orient") (if (value == "auto") 'auto' else false) else if (numeric) float(value)^ else if (key in ["from","easing"]) symbol(value) else value
    let checked_seconds = if (key in ["duration","delay"] and (not c.finite(parsed) or parsed<0.0 or parsed>86400.0)) raise error("Slides: choose 0–86400 seconds")
    let path = catalog.path_preset(value).path
    let checked_path = if (key == "path-preset" and path == null) raise error("Slides: choose a motion path")
    let attrs = if (key == "path-preset") {path:path,id:id} else {[key]:if (key in ["duration","delay"]) parsed*1000.0 else parsed,id:id};
    if (key == "start" or key == "trigger") {
        let checked_previous = if (key == "start" and value in ["with-previous","after-previous"] and
            not any([for (i,prior in cues(st) where i<row.ci) prior.trigger == null])) raise error("Slides: automatic start requires a preceding build")
        let pinned_cue = rewrite(cues(st)[row.ci],string(row.ci),row.path,(node)=>m.attrs(node,{id:id}))
        let cue = m.attrs(pinned_cue,if (key == "start") {start:symbol(value),trigger:null} else {trigger:if (value == "") null else value,start:'click'})
        let items = [for (i,entry in cues(st)) if (i == row.ci) cue else entry]
        let ordered = if (key == "start" and value == "entry") [cue,*[for (i,entry in items where i != row.ci) entry]] else items;
        commit_edit(st,ordered,null,id)^
    } else commit_edit(st,[for (i,cue in cues(st)) if (i == row.ci) rewrite(cue,string(i),row.path,(node)=>m.attrs(node,attrs)) else cue],null,id)^
}
pub fn remove(st,all=false) any^ {
    let row = selected(st);
    if (all) commit_edit(st,[])^
    else if (row == null) st else {
        let items = [for (i,cue in cues(st)) {
            let next = if (i == row.ci) rewrite(cue,string(i),row.path,(node)=>null) else cue;
            *if (len(content(next))==0) [] else [next]
        }];
        commit_edit(st,items)^
    }
}
pub fn move(st,delta) any^ {
    let row = selected(st)
    let id = c.value(row.effect.id,effect_id(st))
    let items = [for (i,cue in cues(st)) if (i == row.ci) rewrite(cue,string(i),row.path,(node)=>m.attrs(node,{id:id})) else cue]
    let next = max(0,min(len(items)-1,row.ci+delta));
    if (row == null or next == row.ci) st else commit_edit(st,[for (i,cue in items)
        if (i == next) items[row.ci] else if (i == row.ci) items[next] else cue],null,id)^
}
pub fn copy(st) => [for (cue in cues(st)) {
    let obj = m.chosen(st)
    let dropped = [for (target in m.scene(st).targets where target.id != obj.id) target.id];
    let kept = m.prune(m.attrs(cue,{trigger:null}),dropped);
    *if (kept == null) [] else [kept]
}]
fn retarget(node,target,prefix,path) => if (name(node) == 'effect') <effect *:map(node),id:prefix ++ path,target:target>
    else m.rebuild(node,map(node),[for (i,child in content(node)) retarget(child,target,prefix,path ++ "-" ++ string(i))])
pub fn paint(st) any^ {
    let obj = m.chosen(st)
    let copied = c.value(st.animation_clipboard,[])
    let pin = m.animation_target(st)
    let id = effect_id(st)
    let checked = if (obj == null or len(copied)==0) raise error("Slides: copy animations from an animated object first")
    let items = [for (i,cue in copied) m.attrs(retarget(cue,pin.id,id ++ "-",string(i)),{id:null,trigger:null})];
    let combined = [*[for (cue in [*cues(st),*items] where cue.start == 'entry') cue],
        *[for (cue in [*cues(st),*items] where cue.start != 'entry') cue]];
    {*:commit_edit(st,combined,pin.objects)^,animation_painter:false,status:"Animations copied to this object."}
}
pub fn preview(st) {
    let row = selected(st)
    let items = if (row != null) [m.attrs(cues(st)[row.ci],{start:'entry',trigger:null})]
        else [*[for (cue in cues(st) where cue.trigger == null)
            m.attrs(cue,{start:if (cue.start in ['with-previous','after-previous']) cue.start else 'entry'})],
            *[for (cue in cues(st) where cue.trigger != null) m.attrs(cue,{start:'entry',trigger:null})]];
    <presentation *:map(st.doc),master:false,autostart:false,
        *[for (child in content(st.doc) where name(child) != 'slide') child];
        <slide *:map(m.scene(st).source),master:false,transition:'none',transition_sound:null,advance_after:null,
            *c.objects(m.scene(st).source);
            *items>>
}
