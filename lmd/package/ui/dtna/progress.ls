import c: lambda.ui.core.component
import icons: .icons
import collection: lambda.ui.core.collection

fn clamp_percent(value) => min(100,max(0,value))
fn step_count(value) => if (value is map) value.count else value
fn step_gap(value) => if (value is map) c.option(value,"gap",2) else 2
fn gradient_stops(value) array^ {
    let stops = if (c.has(value,"from") or c.has(value,"to")) (
        let names = c.properties(value,["from","to","direction"],"progress.gradient")^,
        [{offset:0,color:value.from},{offset:100,color:value.to}])
    else [for (key,color in value where string(key) != "direction") {
        offset:if (ends_with(string(key),"%")) (float(slice(string(key),0,len(string(key))-1)) or null) else null,color:color}];
    if (len(stops) < 2 or not all([for (stop in stops) c.finite(stop.offset) and stop.offset >= 0 and stop.offset <= 100 and c.css_color(stop.color)]))
        raise c.fail("progress","gradient needs at least two CSS colors with percentage stops from 0 to 100")
    else if (not c.enum_valid(value.direction,["to right","to left","to bottom","to top"])^) raise c.fail("progress","unsupported gradient direction")
    else sort(stops,(stop)=>stop.offset)
}
fn gradient_css(value) => "linear-gradient(" ++ c.text(c.option(value,"direction","to right")) ++ "," ++
    join([for (stop in gradient_stops(value)^) stop.color ++ " " ++ string(stop.offset) ++ "%"],",") ++ ")"
fn gradient_id(props) => props.id ++ "-gradient"
pub fn descriptor(props) element^ {
    let steps = c.option(props,"steps",0)
    let count = if (steps is map) steps.count else steps
    let gap = step_gap(steps)
    let colors = if (props.stroke_color is array) props.stroke_color else if (props.stroke_color == null or props.stroke_color is map) [] else [props.stroke_color]
    let flags = c.boolean_props(props,["show_info"],"progress")^
    let color = c.validate_colors(props,["rail_color"],"progress")^;
    if (not c.enum_valid(props.type,["line","circle","dashboard"])^) raise c.fail("progress","invalid type")
    else if (not c.enum_valid(props.stroke_linecap,["round","butt","square"])^) raise c.fail("progress","invalid stroke_linecap")
    else if (not c.enum_valid(props.gap_placement,["top","bottom","start","end"])^) raise c.fail("progress","invalid gap_placement")
    else if (props.percent != null and not c.finite(props.percent)) raise c.fail("progress","percent must be finite")
    else if (not (count is int) or count < 0 or count > 1000 or not c.finite(gap) or gap < 0)
        raise c.fail("progress","steps needs a count from 0 to 1000 and a nonnegative gap")
    else if (steps is map and not c.properties(steps,["count","gap"],"progress.steps")^) false
    else if (not all([for (value in colors) c.css_color(value)]) or (props.stroke_color is array and len(colors) == 0))
        raise c.fail("progress","stroke_color must be a CSS color or nonempty color array")
    else if (props.stroke_color is array and count == 0) raise c.fail("progress","a color array requires steps")
    else if (props.stroke_width != null and (not c.finite(props.stroke_width) or props.stroke_width <= 0 or props.stroke_width > 50))
        raise c.fail("progress","stroke_width must be greater than 0 and at most 50")
    else if (props.gap_degree != null and (not c.finite(props.gap_degree) or props.gap_degree < 0 or props.gap_degree >= 360))
        raise c.fail("progress","gap_degree must be from 0 up to 360 exclusive")
    else if (props.dimension != null and (not c.finite(props.dimension) or props.dimension <= 0)) raise c.fail("progress","dimension must be positive")
    else if (props.format != null and not (props.format is fn)) raise c.fail("progress","format must be a pure function")
    else if (props.rounding != null and not (props.rounding is fn)) raise c.fail("progress","rounding must be a pure function")
    else (
        let gradient = if (props.stroke_color is map) gradient_stops(props.stroke_color)^ else null,
        let identity = if (props.stroke_color is map) collection.require_id(props,"progress gradient")^ else true,
        let success = if (props.success == null) true else validate_success(props.success)^,
        // reject callback failures before apply can embed an error in presentation content.
        let rounded = completed_steps(props,count,clamp_percent(c.option(props,"percent",0)))^,
        c.node('progress',props,null,["type","percent","show_info","status","size","stroke_width","stroke_linecap",
            "stroke_color","rail_color","success","gap_degree","gap_placement","steps","dimension","format","rounding"])^
    )
}
fn validate_success(value) bool^ {
    let names = c.properties(value,["percent","stroke_color"],"progress.success")^
    let color = c.validate_colors(value,["stroke_color"],"progress.success")^;
    if (value.percent != null and not c.finite(value.percent)) raise c.fail("progress","success.percent must be finite") else true
}
fn status(props, percent) => if (props.status != null) c.text(props.status) else if (percent == 100) "success" else "normal"
fn stroke(props, phase, index = 0) => if (props.stroke_color is map) gradient_css(props.stroke_color)
    else if (props.stroke_color is array) props.stroke_color[min(index,len(props.stroke_color)-1)]
    else if (props.stroke_color != null) props.stroke_color
    else if (phase == "exception" or phase == "error") "var(--dtna-error)"
    else if (phase == "success") "var(--dtna-success)" else "var(--dtna-primary)"
fn indicator(props, percent, success, phase) => if (props.format is fn) c.render(props.format(percent,success))
    else if (phase == "exception" or phase == "error") icons.render({name:"close-circle",theme:'filled'})^
    else if (phase == "success") icons.render({name:"check-circle",theme:'filled'})^
    else string(percent) ++ "%"
fn completed_steps(props, count, percent) int^ {
    let raw = count*percent/100.0
    let rounded = if (props.rounding is fn) props.rounding(raw) else round(raw);
    if (not c.finite(rounded)) raise c.fail("progress","rounding must return a finite number")
    else int(min(count,max(0,rounded)))
}
fn line(node, percent, success, phase, count) {
    let p = node.props
    let width = c.option(p,"stroke_width",if (c.text(p.size) == "small") 6 else 8)
    let cap = c.option(p,"stroke_linecap","round")
    let rail = c.option(p,"rail_color","var(--dtna-disabled-background)")
    let completed = completed_steps(p,count,percent)^;
    if (count > 0) <div class:"dtna-progress-steps",style:"gap:" ++ c.px(step_gap(p.steps)) ++ ";height:" ++ c.px(width) ++ ";",
        *[for (index in 0 to (count-1)) <span class:"dtna-progress-step",style:"background:" ++
            (if (index < completed) stroke(p,phase,index) else rail) ++ ";">]>
    else <div class:"dtna-progress-track",style:"height:" ++ c.px(width) ++ ";background:" ++ rail ++ ";border-radius:" ++ (if (cap == "round") "999px" else "0") ++ ";",
        <div class:"dtna-progress-fill",style:"width:" ++ string(percent) ++ "%;background:" ++ stroke(p,phase) ++ ";border-radius:inherit;">
        if (success > 0) <div class:"dtna-progress-success",style:"width:" ++ string(success) ++ "%;background:" ++ c.option(p.success,"stroke_color","var(--dtna-success)") ++ ";"> else null>
}
// rail, progress, mask and success share exactly the same arc geometry.
fn ring_arc(metrics, class, color, start, size, cap = null) =>
    <circle class:class,cx:"50",cy:"50",r:string(metrics.radius),fill:"none",stroke:color,
        ["stroke-width"]:string(metrics.width),["stroke-linecap"]:if (cap == null) metrics.cap else cap,
        ["stroke-dasharray"]:string(size) ++ " " ++ string(metrics.length),["stroke-dashoffset"]:string(0-start),
        transform:"rotate(" ++ string(metrics.rotation) ++ " 50 50)">
fn ring_gradient(props, metrics, cells) {
    let id = gradient_id(props) ++ "-conic"
    let background = "conic-gradient(from " ++ string(metrics.rotation + 90) ++ "deg," ++
        join([for (stop in gradient_stops(props.stroke_color)^) stop.color ++ " " ++ string(floor(stop.offset*metrics.usable/metrics.length)) ++ "%"],",") ++ ")";
    [<defs <mask id:id,maskUnits:"userSpaceOnUse",x:"0",y:"0",width:"100",height:"100",*[for (cell in cells where cell.active) cell.arc]>>,
        <foreignObject class:"dtna-progress-gradient",x:"0",y:"0",width:"100",height:"100",mask:"url(#" ++ id ++ ")",
            <div xmlns:"http://www.w3.org/1999/xhtml",style:"width:100%;height:100%;background:" ++ background ++ ";">>]
}
fn ring(node, percent, success, phase, count) {
    let p = node.props
    let width = c.option(p,"stroke_width",6)
    let radius = 50.0-width/2.0
    let length = 2.0*math.pi*radius
    let degree = c.option(p,"gap_degree",if (c.text(p.type) == "dashboard") 75 else 0)
    let usable = length*(360.0-degree)/360.0
    let placement = c.text(c.option(p,"gap_placement","bottom"))
    let rotation = (if (degree == 0) -90 else if (placement == "top") -90 else if (placement == "start") 180 else if (placement == "end") 0 else 90) + degree/2.0
    let rail = c.option(p,"rail_color","var(--dtna-disabled-background)")
    let gradient = p.stroke_color is map
    let cap = if (gradient) "butt" else c.text(c.option(p,"stroke_linecap","round"))
    let metrics = {radius:radius,length:length,usable:usable,rotation:rotation,width:width,cap:cap}
    let completed = completed_steps(p,count,percent)^
    let segments = if (count > 0) count else 1
    let gap = if (count > 0) min(step_gap(p.steps),usable/count) else 0
    let cells = [for (index in 0 to (segments-1)) (
        let active = count == 0 or index < completed,
        let start = index*usable/segments,
        let size = if (count > 0) max(0,usable/segments-gap) else usable*percent/100.0,
        {active:active,arc:ring_arc(metrics,"dtna-progress-arc",if (not active) rail else if (gradient) "white" else stroke(p,phase,index),
            start,size,if (count > 0) "butt" else cap)})];
    <svg class:"dtna-progress-ring",viewBox:"0 0 100 100",width:string(c.option(p,"dimension",if (c.text(p.size) == "small") 60 else 120)),
        height:string(c.option(p,"dimension",if (c.text(p.size) == "small") 60 else 120)),["aria-hidden"]:"true",
        *[if (count == 0) ring_arc(metrics,null,rail,0,usable) else null,
        *[for (cell in cells where not gradient or not cell.active) cell.arc],
        *(if (gradient) ring_gradient(p,metrics,cells) else []),
        if (success > 0 and count == 0) ring_arc(metrics,"dtna-progress-success",c.option(p.success,"stroke_color","var(--dtna-success)"),0,usable*success/100.0) else null]>
}
view dtna_progress: <dtna kind:'progress'> {
    let p = ~.props
    let percent = clamp_percent(c.option(p,"percent",0))
    let success = min(percent,clamp_percent(c.option(p.success,"percent",0)))
    let phase = status(p,percent)
    let count = step_count(c.option(p,"steps",0))
    let circular = c.text(c.option(p,"type","line")) != "line";
    <div *:c.styled(~,"dtna-progress-" ++ c.text(c.option(p,"type","line")) ++ " dtna-progress-phase-" ++ phase),role:"progressbar",
        ["aria-valuemin"]:"0",["aria-valuemax"]:"100",["aria-valuenow"]:string(percent),["aria-label"]:p.label,
        *[if (circular) ring(~,percent,success,phase,count) else line(~,percent,success,phase,count),
        if (p.show_info != false) <span class:"dtna-progress-label",indicator(p,percent,success,phase)> else null]>
}
pub let css = "
.dtna-progress{display:flex;align-items:center;gap:8px}.dtna-progress-track{position:relative;flex:1;min-width:80px;overflow:hidden}
.dtna-progress-fill{height:100%}.dtna-progress-success{position:absolute;inset-inline-start:0;top:0;height:100%;border-radius:inherit}
.dtna-progress-label{font-size:14px;min-width:32px}.dtna-progress-phase-success .dtna-progress-label{color:var(--dtna-success)}
.dtna-progress-phase-error .dtna-progress-label,.dtna-progress-phase-exception .dtna-progress-label{color:var(--dtna-error)}
.dtna-progress-steps{display:flex;flex:1;min-width:80px}.dtna-progress-step{flex:1;min-width:0}
.dtna-progress-circle,.dtna-progress-dashboard{position:relative;display:inline-flex;gap:0;vertical-align:middle}
.dtna-progress-ring{display:block}.dtna-progress-circle .dtna-progress-label,.dtna-progress-dashboard .dtna-progress-label{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;font-size:24px}
.dtna-progress-circle.dtna-size-small .dtna-progress-label,.dtna-progress-dashboard.dtna-size-small .dtna-progress-label{font-size:14px}
.dtna-progress-phase-active .dtna-progress-fill{animation:dtna-progress-pulse 2s ease-in-out infinite}
@keyframes dtna-progress-pulse{0%,100%{opacity:1}50%{opacity:0.6}}
@media(prefers-reduced-motion:reduce){.dtna-progress-phase-active .dtna-progress-fill{animation:none}}
"
