// D7.2.5: the gallery shares ribbon chrome; the package owns transition choices.
import ribbon: ~~.mod_ribbon
import fx: lambda.slide.transitions
import model: .mod_transitions

fn thumbnail(kind) => <svg width:"48",height:"31",viewBox:"0 0 64 40",'aria-hidden':"true",
    <rect x:"3",y:"6",width:"48",height:"30",fill:"#d9e2ed",stroke:"#9bacc0">;
    <rect x:"14",y:"3",width:"47",height:"30",fill:if (kind == 'none') "#c6d1df" else "#8eafcf",stroke:"#607e9d">;
    if (kind in ['push','cover','uncover','reveal']) <path d:if (kind == 'uncover') "M44 25H23l7-6 M23 25l7 6" else "M51 23H30l7-6 M30 23l7 6",stroke:"#fff",'stroke-width':"2",fill:"none"> else null;
    if (kind == 'wipe') <path d:"M14 3h24v30H14z",fill:"#d9e2ed"> else null;
    if (kind == 'split') <path d:"M35 3v30 M25 18h20 M25 18l5-4 M25 18l5 4 M45 18l-5-4 M45 18l-5 4",stroke:"#fff",fill:"none"> else null;
    if (kind == 'random-bars') <path d:"M21 3v30 M27 3v30 M34 3v30 M42 3v30 M51 3v30",stroke:"#d9e2ed",'stroke-width':"3"> else null;
    if (kind == 'shape') <circle cx:"38",cy:"18",r:"11",fill:"#d9e2ed"> else null;
    if (kind == 'fade') <rect x:"14",y:"3",width:"47",height:"30",fill:"#fff",opacity:".4"> else null;
    if (kind == 'morph') <path d:"M24 24l7-14 8 14z M44 13h10v10H44z",fill:"#fff"> else null;
    if (kind == 'cut') <path d:"M14 32h47",stroke:"#fff",'stroke-width':"3"> else null>
fn choice(entry) => <button id:"transition-" ++ string(entry.id),type:"button",class:"transition-tile",'data-action':"transition-choose:" ++ string(entry.id),
    'aria-label':entry.label,'aria-pressed':"false",title:entry.label,thumbnail(entry.id);<span *ribbon.label_lines(entry.label)>>
fn numeric_control(id,label,key,max) => <label class:"transition-field",<span label>;<input id:id,type:"number",min:"0",max:max,step:"0.1",'data-property':"transition-" ++ key,'aria-label':label ++ " in seconds">>
pub fn tree() => <div class:"transitions-ribbon editor-ribbon",
    ribbon.group("Preview",[ribbon.tool("transition-preview","Preview","play","home-large")]);
    ribbon.group("Transition to This Slide",[<div class:"transition-gallery",role:"group",'aria-label':"Transition gallery",for (entry in fx.catalog) choice(entry)>]);
    ribbon.group("Options",[ribbon.menu("transition-options","Effect Options","object_position","home-large")]);
    ribbon.group("Timing",[ribbon.row([
        ribbon.stack([numeric_control("transition-duration","Duration","duration","60"),
            <label class:"transition-field",<span "Sound">;<select id:"transition-sound",'data-property':"transition-sound",'aria-label':"Transition sound",
                for (sound in model.sounds) <option value:sound.id,sound.label>>>]),
        ribbon.stack([<label class:"transition-check",<input id:"transition-click",type:"checkbox",'data-property':"transition-click">;<span "On Mouse Click">>,
            <div class:"transition-after",<label class:"transition-check",<input id:"transition-automatic",type:"checkbox",'data-property':"transition-automatic">;<span "After">>;
                <input id:"transition-after",type:"number",min:"0",max:"86400",step:"0.1",'data-property':"transition-after",'aria-label':"Advance after seconds">>])])]);
    ribbon.group("",[ribbon.tool("transition-apply-all","Apply To All","repeat_header","home-large")])>
pub fn sound_file() => <label id:"transition-sound-file",class:"field",<span "Transition audio file or URL">;
    <input id:"transition-sound-path",type:"text",'data-property':"transition-sound-path">>
fn label(value) => if (value == 'through-black') "Through Black" else if (value == 'normal') "Smooth / Direct"
    else if (value == 'out') "Outward" else if (value == 'in') "Inward" else upper(slice(string(value),0,1)) ++ slice(string(value),1)
pub fn menu_tree(st) {
    let spec = model.descriptor(st);
    [*[for (direction in spec.directions) ribbon.item("transition-direction:" ++ string(direction),
        if (len(spec.directions) == 2) (if (direction == 'left') "Horizontal" else "Vertical")
        else if (direction == 'left') "From Right" else if (direction == 'right') "From Left" else if (direction == 'top') "From Bottom" else "From Top","object_position")],
        *if (len(spec.variants)>1) [for (variant in spec.variants) ribbon.item("transition-variant:" ++ string(variant),label(variant),"shapes")] else []]
}
