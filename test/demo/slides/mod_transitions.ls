// S1.4 / D7.2.5: transition edits are one authored transaction, using the player catalog.
import m: .mod_model
import c: lambda.slide.common
import fx: lambda.slide.transitions
import html: lambda.slide.html

pub let sounds = [{id:"none",label:"No Sound"},{id:"chime",label:"Chime"},{id:"click",label:"Click"},{id:"whoosh",label:"Whoosh"},{id:"custom",label:"Other Audio…"}]
pub fn kind(st) => if (m.scene(st).source.transition == null) 'none' else m.scene(st).transition
pub fn descriptor(st) => fx.descriptor(kind(st))
pub fn sound_kind(st) => if (m.scene(st).transition_sound == null) "none"
    else c.value([for (sound in sounds where sound.label == m.scene(st).transition_sound.name) sound.id][0],"custom")
pub fn fields(st) => {*:map([for (key in fx.attributes) (key,m.scene(st).source[key])]),
    transition:kind(st),transition_duration:m.scene(st).transition_duration,direction:m.scene(st).direction,
    transition_variant:m.scene(st).transition_variant,advance_on_click:m.scene(st).advance_on_click}
pub fn choose(st,value) any^ {
    let kind = symbol(value)
    let spec = fx.descriptor(kind);
    if (spec == null) raise error("Slides: choose a transition from the gallery")
    else m.slide_properties(st,{transition:kind,transition_variant:if (kind == m.scene(st).transition) m.scene(st).transition_variant else spec.variants[0],
        transition_duration:if (m.scene(st).transition_duration <= 0.0) 600.0 else m.scene(st).transition_duration,
        direction:if (len(spec.directions)>0 and not contains(spec.directions,m.scene(st).direction)) spec.directions[0] else m.scene(st).direction})^
}
pub fn property(st,key,value) any^ {
    if (key == "duration" or key == "after") {
        let seconds = float(value)^;
        if (not c.finite(seconds) or seconds < 0.0 or seconds > (if (key == "duration") 60.0 else 86400.0))
            raise error("Slides: choose " ++ (if (key == "duration") "0–60" else "0–86400") ++ " seconds")
        else m.slide_properties(st,{[if (key == "duration") "transition_duration" else "advance_after"]:seconds*1000.0})^
    }
    else if (key == "click") m.slide_properties(st,{advance_on_click:value == true or value == "true"})^
    else if (key == "automatic") m.slide_properties(st,{advance_after:if (value == true or value == "true") c.value(st.after_draft,2000.0) else null})^
    else if (key == "direction") m.slide_properties(st,{direction:symbol(value)})^
    else if (key == "variant") m.slide_properties(st,{transition_variant:symbol(value)})^
    else raise error("Slides: unknown transition property")
}
pub fn sound(st,value) any^ => m.slide_properties(st,{transition_sound:value})^
pub fn apply_all(st) any^ {
    let settings = fields(st);
    m.commit_doc(st,m.with_slides(st.doc,[for (scene in m.scenes(st.doc)) <slide *:map(scene),*:settings,*content(scene)>]))^
}
pub fn preview(st) => <presentation *:map(st.doc),autostart:false,master:false,
    *[for (child in content(st.doc) where name(child) != 'slide') child];
    <slide id:"transition-before",background:html.layer_background(st.plan,if (st.index == 0) m.scene(st) else st.plan.slides[st.index-1]),
        *:if (st.index == 0) {} else {theme:st.plan.slides[st.index-1].source.theme},
        *if (st.index == 0) [] else c.objects(st.plan.slides[st.index-1].source)>
    <slide *:map(m.scene(st).source),id:"transition-after",advance_after:null,advance_on_click:false,*c.objects(m.scene(st).source)>>
