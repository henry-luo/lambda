// S12.1.1v3: whole-slide visuals are deterministic samples outside object transforms.
import c: .common
import ease: .easing

pub let attributes = ["transition", "transition_duration", "direction", "transition_variant", "transition_sound", "advance_on_click", "advance_after"]
pub let directions = ['left','right','top','bottom']
pub let catalog = [
    {id:'none',label:"None",directions:[],variants:['normal']},
    {id:'fade',label:"Fade",directions:[],variants:['normal','through-black']},
    {id:'push',label:"Push",directions:directions,variants:['normal']},
    {id:'wipe',label:"Wipe",directions:directions,variants:['normal']},
    {id:'split',label:"Split",directions:['left','top'],variants:['out','in']},
    {id:'reveal',label:"Reveal",directions:directions,variants:['normal']},
    {id:'cut',label:"Cut",directions:[],variants:['normal','through-black']},
    {id:'random-bars',label:"Random Bars",directions:['left','top'],variants:['normal']},
    {id:'shape',label:"Shape",directions:[],variants:['circle','diamond','box']},
    {id:'uncover',label:"Uncover",directions:directions,variants:['normal']},
    {id:'cover',label:"Cover",directions:directions,variants:['normal']},
    {id:'morph',label:"Morph",directions:[],variants:['normal']}]
pub fn descriptor(kind) => [for (entry in catalog where entry.id == (if (kind == 'slide') 'cover' else kind)) entry][0]
pub fn default_variant(kind) => descriptor(kind).variants[0]
pub fn animated(scene) => scene.transition != 'none' and
    (scene.transition != 'cut' or scene.transition_variant == 'through-black') and scene.transition_duration > 0.0

fn layer(opacity=1.0,tx=0.0,ty=0.0,clip="none",z=2) => {opacity:opacity,tx:tx,ty:ty,clip:clip,z:z}
fn percent(n) => c.fmt(n*100.0) ++ "%"
fn inset(top,right,bottom,left) => "inset(" ++ join([for (v in [top,right,bottom,left]) percent(v)]," ") ++ ")"
fn wipe(t,direction) => if (direction == 'left') inset(0.0,0.0,0.0,1.0-t)
    else if (direction == 'right') inset(0.0,1.0-t,0.0,0.0)
    else if (direction == 'top') inset(1.0-t,0.0,0.0,0.0) else inset(0.0,0.0,1.0-t,0.0)
pub fn rect_path(x,y,w,h) => "M" ++ c.fmt(x) ++ " " ++ c.fmt(y) ++ "h" ++ c.fmt(w) ++ "v" ++ c.fmt(h) ++ "h" ++ c.fmt(-w) ++ "Z"
pub fn path(parts) => "path(\"" ++ join(parts," ") ++ "\")"
fn split_clip(plan,t,horizontal,variant) {
    let margin = (1.0-t)/2.0;
    if (variant != 'in') if (horizontal) inset(0.0,margin,0.0,margin) else inset(margin,0.0,margin,0.0)
    else if (horizontal) path([rect_path(0.0,0.0,plan.width*t/2.0,plan.height),rect_path(plan.width*(1.0-t/2.0),0.0,plan.width*t/2.0,plan.height)])
    else path([rect_path(0.0,0.0,plan.width,plan.height*t/2.0),rect_path(0.0,plan.height*(1.0-t/2.0),plan.width,plan.height*t/2.0)])
}
fn bars(plan,t,horizontal) => path([for (i in 0 to 11) {
    // a fixed permutation gives every replay the same staggered reveal.
    let progress = max(0.0,min(1.0,t*1.7-float((i*7)%12)/12.0*0.7));
    if (horizontal) rect_path(0.0,plan.height*i/12.0,plan.width*progress,plan.height/12.0)
    else rect_path(plan.width*i/12.0,0.0,plan.width/12.0,plan.height*progress)
}])
fn shape_clip(plan,t,variant) {
    if (variant == 'box') inset((1.0-t)/2.0,(1.0-t)/2.0,(1.0-t)/2.0,(1.0-t)/2.0)
    else if (variant == 'diamond') {
        let radius = t*100.0;
        "polygon(50% " ++ c.fmt(50.0-radius) ++ "%," ++ c.fmt(50.0+radius) ++ "% 50%,50% " ++ c.fmt(50.0+radius) ++ "%," ++ c.fmt(50.0-radius) ++ "% 50%)"
    }
    else "circle(" ++ c.px(sqrt(plan.width*plan.width+plan.height*plan.height)*t/2.0) ++ " at 50% 50%)"
}

pub fn sample(plan,ps) {
    let scene = plan.slides[ps.slide]
    let p = if (ps.phase == 'transition' and scene.transition_duration > 0.0) min(1.0,max(0.0,ps.time_ms/scene.transition_duration)) else 1.0
    let t = ease.sample('ease-in-out',p)
    let horizontal = scene.direction == 'left' or scene.direction == 'right'
    let distance = if (horizontal) plan.width else plan.height
    let sign = if (scene.direction == 'left' or scene.direction == 'top') -1.0 else 1.0
    let entering = (t-1.0)*sign*distance
    let leaving = t*sign*distance
    let moving_in = layer(1.0,if (horizontal) entering else 0.0,if (horizontal) 0.0 else entering)
    let moving_out = layer(1.0,if (horizontal) leaving else 0.0,if (horizontal) 0.0 else leaving,"none",3)
    let visual = if (ps.phase != 'transition') {incoming:layer(),outgoing:layer(0.0)}
        else if (scene.transition_variant == 'through-black') {
            incoming:layer(if (scene.transition == 'cut') (if (p<0.75) 0.0 else 1.0) else max(0.0,2.0*t-1.0)),
            outgoing:layer(if (scene.transition == 'cut') (if (p<0.25) 1.0 else 0.0) else max(0.0,1.0-2.0*t),0.0,0.0,"none",1)}
        else if (scene.transition == 'fade') {incoming:layer(t),outgoing:layer(1.0,0.0,0.0,"none",1)}
        else if (scene.transition == 'push') {incoming:moving_in,outgoing:moving_out}
        else if (scene.transition in ['cover','slide']) {incoming:moving_in,outgoing:layer(1.0,0.0,0.0,"none",1)}
        else if (scene.transition == 'uncover') {incoming:layer(),outgoing:moving_out}
        else if (scene.transition == 'reveal') {incoming:layer(),outgoing:layer(1.0,
            if (horizontal) leaving*0.25 else 0.0,if (horizontal) 0.0 else leaving*0.25,
            wipe(1.0-t,if (scene.direction == 'left') 'right' else if (scene.direction == 'right') 'left' else if (scene.direction == 'top') 'bottom' else 'top'),3)}
        else if (scene.transition in ['wipe','split','random-bars','shape']) {
            incoming:layer(1.0,0.0,0.0,if (scene.transition == 'wipe') wipe(t,scene.direction)
                else if (scene.transition == 'split') split_clip(plan,t,horizontal,scene.transition_variant)
                else if (scene.transition == 'random-bars') bars(plan,t,horizontal) else shape_clip(plan,t,scene.transition_variant)),
            outgoing:layer(1.0,0.0,0.0,"none",1)}
        else {incoming:layer(),outgoing:layer(1.0,0.0,0.0,"none",1)};
    visual
}
pub fn background(plan,ps) => if (ps.phase == 'transition' and plan.slides[ps.slide].transition_variant == 'through-black') "#000" else "transparent"
pub fn properties(v) => {opacity:c.fmt(v.opacity),transform:"translate(" ++ c.px(v.tx) ++ "," ++ c.px(v.ty) ++ ")",
    ["clip-path"]:v.clip,["z-index"]:string(v.z)}
pub fn style(v) => join([for (key,value in properties(v)) string(key) ++ ":" ++ value ++ ";"],"")
