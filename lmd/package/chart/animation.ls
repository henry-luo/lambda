// Explicit-elapsed SVG sampling; timing and geometry are immutable values (S12.1.1v2).
import util: .util
import parse: .parse
import parameter: .parameter
import expr: .expression
import paths: .path_geometry
import geometry: .geometry
import svg: .svg
import easing: lambda.slide.easing
import colors: lambda.slide.color
import affine: .svg_transform
import picking: .picking

let phases = ["enter", "update", "exit"]
let kinds = ["fade", "grow_x", "grow_y", "scale", "path_reveal", "wave", "morph"]
pub fn merge(parent, own) => if (own == false) false else if (own == null) if (parent == true) {enter: {type: "fade", duration: 300}} else parent
    else if (own == true) {enter: {type: "fade", duration: 300}} else {*:parse.attributes(parent), *:parse.attributes(own)}
fn policy(options, phase) => if (options == false or options == null or options[phase] == false) null
    else if (options[phase] == true) {type: if (phase == "update") "morph" else "fade", duration: 300}
    else options[phase]
fn evaluate(value, row, index, group, values) {
    if (value is fn) value(row, index, group)
    else if (value.expr != null) expr.evaluate(expr.compile(value.expr, parameter.expression_values(values)), row)
    else value
}
fn curve(raw, t) {
    let name = if (raw == null) 'linear' else if (raw is string) symbol(replace(raw, "_", "-")) else raw;
    let result = if (raw is fn) raw(t) else if (easing.valid(name)) easing.sample(name, t) else error("chart: unknown animation easing");
    if (result is error) result else if (not util.finite_number(result) or result < 0 or result > 1)
        error("chart: easing must return finite progress in [0,1]") else if (t == 0 or t == 1) t else result
}
fn group_value(row, grouping) => [for (field in grouping.by) row[field]]
fn groups(data, grouping) {
    let values = util.unique_vals(data |> group_value(~, grouping));
    if (grouping.order == "ascending" or grouping.order == "descending")
        sort(values, if (grouping.order == "descending") 'desc' else 'asc')
    else if (grouping.order is array) [*grouping.order, for (value in values where not contains(grouping.order, value)) value]
    else values
}
fn timing(ctx, row, index, group, options, phase) {
    let config = policy(options, phase);
    let delay_channel = ctx.encoding[phase ++ "_delay"];
    let duration_channel = ctx.encoding[phase ++ "_duration"];
    let delay = evaluate(if (delay_channel != null) parse.channel_value(delay_channel, row)
        else if (config.delay != null) config.delay else 0.0, row, index, group, ctx._parameter_values);
    let duration = evaluate(if (duration_channel != null) parse.channel_value(duration_channel, row)
        else if (config.duration != null) config.duration else 300.0, row, index, group, ctx._parameter_values);
    let kind = if (config.type != null) config.type else if (phase == "update") "morph" else "fade";
    if (config == null) null
    else if (not contains(kinds, kind)) error("chart: unsupported transition type")
    else if (not util.finite_number(delay) or delay < 0 or not util.finite_number(duration) or duration < 0)
        error("chart: animation delay and duration must be finite and nonnegative")
    else if (config.fallback != null and not contains(["crossfade", "error"], config.fallback)) error("chart: invalid morph fallback")
    else if (config.boundary!=null and (not util.finite_number(config.boundary) or config.boundary<0 or config.boundary>1)) error("chart: discrete transition boundary must lie in [0,1]")
    else {type: kind, delay: delay, duration: duration, easing: config.easing, fallback: config.fallback,
        boundary: if (config.boundary != null) config.boundary else 0.5}
}
fn descriptor(ctx, row, options, phase) {
    let grouping = options.group;
    let group = group_value(row, grouping);
    let ordered = groups(ctx.data, grouping);
    let rank = index_of(ordered, group);
    let index = index_of(ctx.data, row);
    let own = timing(ctx, row, index, group, options, phase);
    let stagger = if (grouping.stagger != null) grouping.stagger else 0.0;
    let lengths = if (grouping.mode == "sequence") [for (value in slice(ordered, 0, rank))
        max([0.0, for (i, datum in ctx.data where group_value(datum, grouping) == value,
            let settings = timing(ctx, datum, i, value, options, phase)) settings.delay + settings.duration])] else [];
    let offset = sum(lengths) + (if (rank != null) rank else 0) * stagger;
    let progress = if (own.duration == 0) (if (ctx._time < own.delay + offset) 0.0 else 1.0)
        else util.clamp_val((ctx._time - own.delay - offset) / own.duration, 0.0, 1.0);
    let eased = curve(own.easing, if (ctx._time == null) 1.0 else progress);
    if (own == null or own is error) own
    else if (not util.finite_number(stagger) or stagger < 0 or grouping.mode != null and not contains(["parallel", "sequence"], grouping.mode))
        error("chart: invalid animation group timing")
    else if (eased is error) eased
    else {*:own, easing: if (own.easing is fn) "custom" else own.easing, delay: own.delay + offset, progress: eased}
}
pub fn attributes(ctx, row, mark_options = {}) {
    let options = merge(ctx._animate, mark_options.animate);
    if (options == null) {} else if (options == false) {'data-chart-animation': "{\"disabled\":true}"} else {
        let descriptors = [for (phase in phases) descriptor(ctx, row, options, phase)];
        let failure = util.first_error(descriptors);
        if (failure is error) failure else {
            'data-chart-animation': format(map([for (i, phase in phases) for (part in [phase, descriptors[i]]) part]), 'json'),
            'data-chart-group-key': if (ctx.encoding.group_key != null) format(parse.channel_value(ctx.encoding.group_key, row), 'json') else null}
    }
}

pub fn validate(spec) {
    let options = spec.animate;
    let keys = if (spec.encoding.key!=null) [for (row in spec.data) parse.channel_value(spec.encoding.key,row)] else null;
    let ctx = {data: spec.data, encoding: spec.encoding, _animate: options, _parameter_values: spec._parameter_values};
    if (options == false or options == null and spec._requires_key!=true) null else util.first_error([if (options != null and options != false and not (options is map or options == true)) error("chart: animate must be a map, true or false"),
        if ((spec._requires_key==true or options != false and (options.update != null and options.update != false or options.exit != null and options.exit != false)) and keys == null)
            error("chart: update and exit transitions require an explicit key encoding"),
        if (options.group!=null and (not (options.group is map) or options.group.by!=null and
            (not (options.group.by is array) or any(options.group.by |> not (~ is string))) or
            options.group.order!=null and not (options.group.order is array) and not contains(["ascending","descending"],options.group.order)))
            error("chart: animation groups require ordered field names"),
        if (keys!=null and any([for (key in keys) not (key is string or key is symbol or util.finite_number(key))])) error("chart: animated keys must be strings, symbols or finite numbers"),
        if (keys != null and len(util.unique_vals(keys)) != len(keys)) error("chart: animated keys must be unique within a mark"),
        for (phase in phases,let settings=policy(options,phase) where settings!=null)
            if (not (settings is map)) error("chart: transition options must be a map") else
            if (len(spec.data)==0 and not (settings.delay is fn or settings.delay.expr!=null or settings.duration is fn or settings.duration.expr!=null))
                timing(ctx,{},0,[],options,phase),
        for (row in spec.data) attributes(ctx, row)])
}

fn metadata(node) => if (node["data-chart-animation"] == null) null else parse(node["data-chart-animation"], 'json') ^ {~}
fn identity(node, index) => if (node is element and node["data-chart-key"] != null)
    "key:" ++ node["data-chart-view"] ++ ":" ++ string(node["data-chart-part"]) ++ ":" ++ node["data-chart-key"]
    else if (node is element and node["data-chart-row"] != null) "row:" ++ node["data-chart-row"]
    else if (node is element and node.id != null) "id:" ++ node.id else "slot:" ++ string(index)
fn keyed(children) => [for (i, child in children) {node: child, id: identity(child, i),
    role: len([for (j, earlier in children where j < i and identity(earlier, j) == identity(child, i)) true])}]
fn decorative(node) => if (not (node is element)) node else svg.rebuild(name(node),
    {*:map([for (key, value in map(node) where not starts_with(string(key), "data-chart-") and not contains(["tabindex","data-focus-key"],string(key)))
        for (part in [string(key), value]) part]), 'pointer-events': "none"}, content(node) |> decorative(~))
fn faded(node, opacity, exit = false) => if (node == null) null else <g opacity: opacity, *:(if (exit) {'pointer-events': "none"} else {}),
    if (exit) decorative(node) else node>
fn crossfade(a, b, t) => <g class: "chart-crossfade", faded(a, 1.0 - t, true); faded(b, t)>
fn points(node) => if (contains(['path','rect','line','circle','ellipse'],name(node))) picking.outlines(node) else null
fn resample(polyline, count) {
    let clean=[for (i,p in polyline.points where (i==0 or p!=polyline.points[i-1]) and
        not (polyline.closed and i==len(polyline.points)-1 and p==polyline.points[0])) p];
    let vertices = if (polyline.closed) [*clean, clean[0]] else clean;
    let lengths = [for (i in 0 to (len(vertices)-2)) math.sqrt(sum([for (axis in [0,1]) (vertices[i+1][axis]-vertices[i][axis])**2]))];
    let total = sum(lengths);
    [for (i in 0 to (count-1), let distance=total*float(i)/float(if (polyline.closed) count else max(1,count-1)),
        let candidates=[for (j, length in lengths where sum(slice(lengths,0,j+1))>=distance) j],
        let segment=if (len(candidates)>0) candidates[0] else max(0,len(lengths)-1),
        let t=if (lengths[segment]>0) (distance-sum(slice(lengths,0,segment)))/lengths[segment] else 0.0)
        if (len(vertices)<=1) vertices[0] else geometry.interpolate(vertices[segment],vertices[segment+1],t)]
}
fn align(source,target,closed) {
    let shifts=if (closed) [for (i in 0 to (len(target)-1)) i] else [0];
    let scores=[for (shift in shifts) {shift:shift,cost:sum([for (i,p in source) paths.distance(p,target[(i+shift)%len(target)])**2])}];
    let best=(sort(scores,(entry)=>entry.cost))[0].shift;
    [for (i in 0 to (len(target)-1)) target[(i+best)%len(target)]]
}
fn nesting(lines) => [for (i,line in lines) for (j,other in lines where i!=j and line.closed and other.closed)
    geometry.contains_point(other.points,line.points[0])]
fn vertex_count(line) => len(line.points)-(if (line.closed and line.points[0]==line.points[len(line.points)-1]) 1 else 0)
pub fn morph(a, b, t, fallback = "crossfade") {
    let source=points(a); let target=points(b);
    let failure=util.first_error([source,target]);
    let compatible=source!=null and target!=null and len(source)==len(target) and all([for (i, line in source)
        line.closed==target[i].closed and (not line.closed or (geometry.area(line.points)>=0)==(geometry.area(target[i].points)>=0))]);
    if (failure is error) failure else if (not compatible) if (fallback=="error") error("chart: incompatible morph topology") else null
    else {
        let lines=[for (i,line in source,let count=max([2,vertex_count(line),vertex_count(target[i])]),
            let aa=resample(line,count),let bb=align(aa,resample(target[i],count),line.closed))
            {closed:line.closed,points:[for (j,p in aa) geometry.interpolate(p,bb[j],t)]}];
        let crossing=any([for (line in lines where line.closed)
            not geometry.simple_ring([*line.points,line.points[0]])]) or
            any([for (i,line in lines where line.closed) for (j,other in lines where j>i and other.closed)
                geometry.rings_intersect(line.points,other.points)]) or
            nesting(source)!=nesting(target) or nesting(lines)!=nesting(source);
        if (crossing) if (fallback=="error") error("chart: morph would intersect itself") else null else paths.path(lines)
    }
}
fn color(a,b,t) {
    let ac=colors.parse(a,"chart animation") ^ {null}; let bc=colors.parse(b,"chart animation") ^ {null};
    if (ac==null or bc==null) null else colors.css(colors.sample(ac,bc,t))
}
fn resources(node) => if (not (node is element)) [] else [
    if (contains(['linearGradient','radialGradient','pattern','clipPath'],name(node))) node,
    for (child in content(node)) for (resource in resources(child)) resource] |: ~!=null
fn resource(items,reference) => if (not (reference is string) or not starts_with(reference,"url(#")) null else
    (items |: ~.id==slice(reference,5,len(reference)-1))[0]
fn compatible_paint(a,b) => a!=null and b!=null and name(a)==name(b) and
    (name(a)=='linearGradient' or name(a)=='radialGradient') and len(content(a))==len(content(b)) and
    a.gradientUnits==b.gradientUnits and a.spreadMethod==b.spreadMethod and all([for (i,stop in content(b))
        content(a)[i]["stop-color"]==stop["stop-color"] or color(content(a)[i]["stop-color"],stop["stop-color"],0.5)!=null])
fn references(node) => if (not (node is element)) [] else [for (value in [node.fill,node.stroke] where value is string and starts_with(value,"url(#")) value,
    for (child in content(node)) for (value in references(child)) value]
fn paint_id(a,b,t) => "chart-transition-"++a.id++"-to-"++b.id++"-at-"++util.fmt_num(t)
fn paint_between(a,b,t,config) {
    let aa=resource(config.source_resources,a); let bb=resource(config.target_resources,b);
    if (compatible_paint(aa,bb)) "url(#"++paint_id(aa,bb,t)++")" else color(a,b,t)
}
fn attributes_between(a,b,t,boundary,config={}) => map([for (key,value in map(b),let previous=a[string(key)])
    for (part in [string(key),if (util.finite_number(previous) and util.finite_number(value)) util.lerp(previous,value,t)
        else if (string(key)=="transform" and previous!=value) affine.interpolate(previous,value,t)
        else if (contains(["fill","stroke","stop-color"],string(key)) and previous!=value) paint_between(previous,value,t,config)
        else if (previous==value or starts_with(string(key),"data-chart-")) value
        else if (t<boundary and previous!=null) previous else value]) part])
fn transition(node, config, phase, t) {
    let progress=if (phase=="exit") 1.0-t else t;
    let opacity=if (node.opacity!=null) node.opacity else 1.0;
    let image=if (phase=="exit") decorative(node) else node;
    if (config.type=="grow_x" or config.type=="grow_y" or config.type=="scale" or config.type=="wave")
        <g opacity: progress,transform: if (config.type=="wave") svg.translate(0.0, (1.0-progress)*20.0)
            else "scale("++util.fmt_num(if (config.type=="grow_y") 1.0 else progress)++","++util.fmt_num(if (config.type=="grow_x") 1.0 else progress)++")",image>
    else if (config.type=="path_reveal" and name(node)=='path')
        svg.rebuild('path',{*:map(image),pathLength:1.0,'stroke-dasharray':"1",'stroke-dashoffset':1.0-progress},content(image))
    else faded(image,progress)
}
fn progress(config, elapsed) => if (config.progress!=null) config.progress else
    curve(config.easing,if (config.duration==0) (if (elapsed<config.delay) 0.0 else 1.0) else util.clamp_val((elapsed-config.delay)/config.duration,0.0,1.0))
fn group_identity(node) => if (node["data-chart-group-key"]==null) null else
    format([node["data-chart-view"],node["data-chart-part"],node["data-chart-group-key"]],{type:"json",compact:true})
fn children_between(a,b,elapsed,default_config,overall) {
    let aa=keyed(content(a)); let bb=keyed(content(b));
    [for (entry in bb,let matches=aa |: ~.id==entry.id and ~.role==entry.role,
        let groups=if (len(matches)==0 and entry.node["data-chart-group-key"]!=null)
            aa |: group_identity(~.node)==group_identity(entry.node) else [],
        let targets=bb |: group_identity(~.node)==group_identity(entry.node),
        let ambiguous=len(matches)==0 and len(groups)>1 and len(targets)>1,
        let source=if (len(matches)>0) matches[0].node else if (len(groups)==1) groups[0].node
            else if (len(groups)>1 and len(targets)==1) <g for (group in groups) group.node> else null
        where not ambiguous or entry==targets[0])
        if (ambiguous) if (default_config.fallback=="error") error("chart: ambiguous many-to-many correspondence") else
            crossfade(<g for (group in groups) group.node>,<g for (target in targets) target.node>,overall)
        else sample_node(source,entry.node,elapsed,default_config,overall),
     for (entry in aa,
        let group=group_identity(entry.node),
        let sources=aa |: group_identity(~.node)==group,
        let targets=bb |: group_identity(~.node)==group
        where not any([for (target in bb) target.id==entry.id and target.role==entry.role]) and
            not (group!=null and len(targets)>0))
        for (exiting in [sample_node(entry.node,null,elapsed,default_config,overall)] where exiting!=null) exiting]
}
fn sample_node(a,b,elapsed,default_config,overall) {
    let phase=if (a==null) "enter" else if (b==null) "exit" else "update";
    let own=metadata(if (b!=null) b else a)[phase];
    let config=if (own!=null) own else default_config;
    let t=if (default_config.timeline == true and own==null) overall else if (config!=null) progress(config,elapsed) else overall;
    let container=b is element and contains(['svg','g','defs','clipPath','linearGradient','radialGradient','pattern'],name(b));
    let disabled=metadata(if (b!=null) b else a).disabled==true;
    if (disabled) b
    else if (t is error) t
    else if (container and (a==null or a is element and name(a)==name(b))) (
        let attrs=if (a==null) map(b) else attributes_between(a,b,t,0.5,default_config),
        let children=children_between(a,b,elapsed,default_config,overall),
        let failure=util.first_error(children),
        if (failure is error) failure else svg.rebuild(name(b),
            if (name(b)=='svg' and (b.viewBox!=null or t>0 and t<1)) {*:attrs,viewBox:"0 0 "++string(attrs.width)++" "++string(attrs.height)} else attrs,children))
    else if (a==null) if (own==null) b else if (t>=1) b else transition(b,config,phase,t)
    else if (b==null) if (own==null or t>=1) null else transition(a,config,phase,t)
    else if (t<=0) if (a is element and b is element) svg.rebuild(name(a),{*:map(a),
        *:map([for (key,value in map(b) where starts_with(string(key),"data-chart-") or string(key)=="data-focus-key")
            for (part in [string(key),value]) part])},content(a)) else a else if (t>=1) b
    else if (not (a is element and b is element)) if (a==b or t>=0.5) b else a
    else if (config.type=="fade" or name(a)=='text' and content(a)!=content(b)) crossfade(a,b,t)
    else if (config.type!="morph") crossfade(a,transition(b,config,"enter",t),t)
    else {
        let geometric=contains(['path','rect','circle','ellipse','line'],name(a)) and contains(['path','rect','circle','ellipse','line'],name(b));
        let path=if (config.type=="morph" and geometric and (name(a)=='path' or name(a)!=name(b))) morph(a,b,t,config.fallback) else null;
        let attrs=attributes_between(a,b,t,if (config.boundary!=null) config.boundary else 0.5,default_config);
        let paints=[for (key in ["fill","stroke"],let aa=resource(default_config.source_resources,a[key]),
            let bb=resource(default_config.target_resources,b[key]) where a[key]!=b[key] and compatible_paint(aa,bb))
            svg.rebuild(name(bb),{*:attributes_between(aa,bb,t,0.5),id:paint_id(aa,bb,t)},
                [for (i,stop in content(bb)) <stop *:attributes_between(content(aa)[i],stop,t,0.5)>])];
        let incompatible_paint=any([for (key in ["fill","stroke","stop-color"] where b[key]!=null and a[key]!=null and a[key]!=b[key]) attrs[key]==null]);
        let children=children_between(a,b,elapsed,default_config,overall);
        let failure=util.first_error([path,*children]);
        if (failure is error) failure
        else if (incompatible_paint or name(a)!=name(b) and path==null or config.type=="morph" and geometric and name(a)=='path' and path==null)
            if (config.fallback=="error") error("chart: incompatible transition geometry or paint") else crossfade(a,b,t)
        else {
            let image=if (path!=null) <path *:attrs,d:path,*children> else svg.rebuild(name(b),attrs,children);
            if (len(paints)>0) <g <defs *paints>;image> else image
        }
    }
}
pub fn sample(previous, target, elapsed, options = {}) {
    let duration=if (options.duration!=null) options.duration else 300.0;
    let old_resources=resources(previous);let new_resources=resources(target);
    let config={type:"morph",duration:duration,delay:0.0,easing:options.easing,fallback:options.fallback,timeline:options.timeline,
        source_resources:old_resources,target_resources:new_resources};
    if (not util.finite_number(elapsed) or elapsed<0) error("chart: frame elapsed must be finite and nonnegative")
    else {
        let image=refine_states(sample_node(previous,target,elapsed,config,if (options.progress!=null) options.progress else if (duration==0) 1.0 else util.clamp_val(elapsed/duration,0.0,1.0)));
        let used=references(image);
        let retained=old_resources |: not contains(resources(image) |> ~.id,~.id) and contains(used,"url(#"++~.id++")");
        if (image is error) image else picking.refresh(if (len(retained)>0 and image is element and name(image)=='svg')
            svg.rebuild('svg',map(image),[<defs *retained>,*content(image)]) else image)
    }
}

fn refine_states(node) => if (not (node is element)) node else svg.rebuild(name(node),
    {*:map(node), *:(if (node["data-chart-state"]!=null) parse(node["data-chart-state"], 'json') ^ {~} else {})},
    content(node) |> refine_states(~))
pub fn duration(image) => if (not (image is element)) 0.0 else max([0.0,
    for (phase in phases,let config=metadata(image)[phase] where config!=null) config.delay+config.duration,
    for (child in content(image)) duration(child)])
pub fn geometry_signature(image) => if (not (image is element)) image else [name(image),
    map([for (key,value in map(image) where contains(["x","y","x1","x2","y1","y2","cx","cy","r","rx","ry","width","height","d","points","transform","viewBox","font-size","text-anchor"],string(key)))
        for (part in [string(key),value]) part]),content(image) |> geometry_signature(~)]
