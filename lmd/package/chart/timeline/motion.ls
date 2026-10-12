// Retain displayed geometry and SVG so interruption starts at the visible frame (S12.1.3).
import c: .common
import util: lambda.chart.util
import geometry: lambda.chart.geometry
import animation: lambda.chart.animation
import svg: lambda.chart.svg
import index: .index
import dates: .date
import time_map: .time_map

pub fn configure(raw) {
    let options=if (raw is map) raw else {};
    let config={enabled:raw!=null and raw!=false,duration:300,delay:0,easing:'linear',enter:true,update:true,exit:true,guides:true,*:options};
    let failure=util.first_error([
        if (raw!=null and not (raw is map or raw is bool)) c.fail("animate","expected a boolean or animation options"),
        c.properties(options,["duration","delay","easing","enter","update","exit","guides"],"animate"),
        for (key in ["duration","delay"]) if (not util.finite_number(config[key]) or config[key]<0) c.fail("animate." ++ key,"expected nonnegative milliseconds"),
        for (key in ["enter","update","exit","guides"]) if (not (config[key] is bool)) c.fail("animate." ++ key,"expected a boolean"),
        animation.progress(config,0)]);
    if (failure is error) failure else config
}
fn enabled(spec) => spec.motion=="full" and spec.animate.enabled==true and spec.animate.duration+spec.animate.delay>0
fn rows(plan) array | error => sort([for (item in plan.events) {id:item.id,item:item}],{by:(row)=>row.id})
fn between(a, b, t) => map([for (key,value in b) (key,util.lerp(a[key],value,t))])
fn at(a,b,t) => dates.offset(a,decimal(b-a)*decimal(t))
fn item_frame(a, b, t, options) {
    let phase=if (b==null) "exit" else if (a==null) "enter" else "update";
    let amount=if (options[phase]==true) t else 1.0;
    let value=if (b==null) a else b;
    let opacity=if (a.opacity!=null) a.opacity else if (a==null) 0.0 else 1.0;
    let target=if (b==null) 0.0 else 1.0;
    let sampled=if (a==null or b==null or options.update!=true) value else {*:b,
        point:geometry.interpolate(a.point,b.point,amount),end:geometry.interpolate(a.end,b.end,amount),
        card:geometry.interpolate(a.card,b.card,amount),bounds:between(a.bounds,b.bounds,amount),
        fraction:util.lerp(a.fraction,b.fraction,amount),offset:util.lerp(a.offset,b.offset,amount)};
    if (b==null and amount>=1) null else {*:sampled,opacity:util.lerp(opacity,target,amount),exiting:b==null,
        target_bounds:if (b.target_bounds!=null) b.target_bounds else value.bounds}
}

// Metadata changes only timing; retained SVG geometry remains the interruption source.
fn timed(node, progress, options) {
    if (not (node is element)) node else {
        let event=node["data-timeline-id"]!=null or node["data-timeline-cluster"]!=null or node["data-chart-animation"]!=null;
        let guide=node["data-timeline-axis"]!=null or node["data-timeline-annotation"]!=null;
        let phases=if (guide and not options.guides) {disabled:true}
            else if (event or guide) map([for (phase in ["enter","update","exit"])
                (phase,{type:if (phase=="update") "morph" else "fade",progress:if (options[phase]) progress else 1.0})]) else null;
        svg.rebuild(name(node),{*:map(node),*:(if (phases!=null) {'data-chart-animation':format(phases,'json')} else {})},
            [for (child in content(node)) timed(child,progress,options)])
    }
}
pub fn sample(previous, target, elapsed, spec) {
    let options=spec.animate;
    let progress=if (not enabled(spec)) 1.0 else animation.progress(options,elapsed);
    if (not util.finite_number(elapsed) or elapsed<0) c.fail("animate.time","expected nonnegative elapsed milliseconds")
    else if (progress is error) progress else if (progress>=1) target else {
        let aa=rows(previous.plan);
        let bb=rows(target.plan);
        let events=[for (item in target.plan.events) item_frame(index.lookup(aa,item.id).item,item,progress,options),
            for (item in previous.plan.events where index.lookup(bb,item.id)==null)
                item_frame(item,null,progress,options)] |: ~!=null;
        let window=if (previous.plan.window!=null and options.update) {
            from:at(previous.plan.window.from,target.plan.window.from,progress),
            to:at(previous.plan.window.to,target.plan.window.to,progress)} else target.plan.window;
        let plan={*:target.plan,events:events,window:window,time_map:time_map.configure(spec.scale,window)};
        let image=if (target.svg==null) null else animation.sample(timed(previous.svg,progress,options),timed(target.svg,progress,options),elapsed,
            {duration:options.duration,progress:progress});
        if (image is error) image else {plan:plan,svg:image}
    }
}
pub fn advance(previous, target, now, spec) {
    let changed=previous==null or previous.target!=target;
    let source=if (previous==null) {plan:{*:target.plan,events:[]},svg:null} else previous.displayed;
    let elapsed=if (changed) 0.0 else max(0,now-previous.anchor);
    let origin=if (changed) source else previous.source;
    let sampled=sample(origin,target,elapsed,spec);
    if (sampled is error) sampled else {target:target,source:origin,displayed:sampled,
        anchor:if (changed) now else previous.anchor,elapsed:elapsed,
        running:enabled(spec) and elapsed<spec.animate.delay+spec.animate.duration}
}
