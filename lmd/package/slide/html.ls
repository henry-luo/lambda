import c: .common
import samples: .sample

pub let css = "
html, body { margin: 0; width: 100%; min-height: 100%; }
.slide-page { background: #111827; font-family: Arial, sans-serif; }
.slide-stage { position: relative; overflow: hidden; margin: 0 auto; }
.slide-canvas { position: absolute; left: 0; top: 0; overflow: hidden; transform-origin: 0 0; }
.slide-layer { position: absolute; left: 0; top: 0; overflow: hidden; }
.slide-object { position: absolute; box-sizing: border-box; transform-origin: center center; }
.slide-effect { width: 100%; height: 100%; transform-origin: center center; }
.slide-text { width: 100%; height: 100%; white-space: pre-wrap; }
.slide-text p { margin: 0 0 0.6em; }
.slide-image { width: 100%; height: 100%; }
.slide-controls { display: flex; gap: 8px; justify-content: center; padding: 12px; color: white; }
.slide-controls button { font: inherit; padding: 6px 14px; }
"

pub fn effect_properties(v) {
    let invisible = v.visible < 0.5 or v.opacity <= 0.0
    let clip_css = if (v.clip <= 0.0) "none" else {
        let amount = c.fmt(v.clip * 100.0) ++ "%"
        "inset(" ++ (if (v.clip_direction == 'left') "0 0 0 " ++ amount
            else if (v.clip_direction == 'right') "0 " ++ amount ++ " 0 0"
            else if (v.clip_direction == 'top') amount ++ " 0 0 0" else "0 0 " ++ amount ++ " 0") ++ ")"
    }
    {opacity: c.fmt(v.opacity), visibility: if (invisible) "hidden" else "visible",
        ["pointer-events"]: if (invisible) "none" else "auto", transform: "translate(" ++
        c.px(v.tx) ++ "," ++ c.px(v.ty) ++ ") rotate(" ++ c.fmt(v.rotation) ++ "deg) scale(" ++
        c.fmt(v.sx) ++ "," ++ c.fmt(v.sy) ++ ")", ["clip-path"]: clip_css}
}

pub fn effect_style(v) => join([for (key, value in effect_properties(v)) string(key) ++ ":" ++ value ++ ";"], "")

fn shape_content(obj, visual, instance) {
    let src = obj.source
    let paint_fill = visual.paint
    let stroke = if (src.kind == 'line') paint_fill else c.value(src.stroke, "none")
    let stroke_width = c.num(src, "stroke_width", 1.0)
    let kind = c.value(src.kind, 'rect')
    let paint = {id: instance ++ "-paint-" ++ obj.dom_key, fill: paint_fill,
        stroke: stroke, ["stroke-width"]: stroke_width};
    <svg width: "100%", height: "100%", preserveAspectRatio: "none", viewBox: "0 0 " ++ c.fmt(obj.width) ++ " " ++ c.fmt(obj.height),
        if (kind == 'rect') <rect *: paint, width: obj.width, height: obj.height, rx: c.num(src, "radius", 0.0)>
        else if (kind == 'line') <line *: paint, x1: 0.0, y1: 0.0, x2: obj.width, y2: obj.height>
        else <ellipse *: paint, cx: obj.width / 2.0, cy: obj.height / 2.0,
            rx: if (kind == 'circle') min(obj.width, obj.height) / 2.0 else obj.width / 2.0,
            ry: if (kind == 'circle') min(obj.width, obj.height) / 2.0 else obj.height / 2.0>
    >
}

fn object_content(obj, visual, visuals, instance, theme) {
    let src = obj.source
    if (obj.tag == 'group') for (child in obj.children) render_object(child, visuals, instance, theme)
    else if (obj.tag == 'shape') shape_content(obj, visual, instance)
    else if (obj.tag == 'image') <img class: "slide-image", src: src.src,
        style: "object-fit:" ++ c.as_text(c.value(src.fit, 'contain')) ++ ";">
    else if (obj.tag == 'text') {
        let size = c.num(src, "font_size", if (src.role == 'title') 56.0 else 32.0)
        let color = c.as_text(c.value(visual.paint, if (theme == 'dark') "#f9fafb" else "#111827"));
        <div class: "slide-text", style: "font-size:" ++ c.px(size) ++ ";color:" ++ color ++ ";", *content(src)>
    } else for (child in content(src)) child
}

pub fn placement_properties(obj) => {left: c.px(obj.x), top: c.px(obj.y),
    width: c.px(obj.width), height: c.px(obj.height),
    transform: "rotate(" ++ c.fmt(obj.rotation) ++ "deg) scale(" ++ c.fmt(obj.scale) ++ ")"}

pub fn placement_style(obj) => join([for (key, value in placement_properties(obj))
    string(key) ++ ":" ++ value ++ ";"], "")

fn render_object(obj, visuals, instance, theme) {
    let matches = [for (v in visuals where v.id == obj.id) v]
    let v = matches[0]
    let hidden = v.visible < 0.5 or v.opacity <= 0.0
    let position = placement_style(obj);
    <div id: instance ++ "-place-" ++ obj.dom_key, class: "slide-object", style: position,
        <div id: instance ++ "-" ++ obj.dom_key, class: "slide-effect", style: effect_style(v),
            ["data-slide-object"]: obj.id, ["aria-hidden"]: if (hidden) "true" else "false", *: (if (hidden) {inert: ""} else {}),
            <div class: c.as_text(c.value(obj.source.class, "")), style: c.as_text(c.value(obj.source.style, "")),
                object_content(obj, v, visuals, instance, theme)
            >
        >
    >
}

pub fn layer(plan, scene, visuals, instance, style = "") {
    let background = c.as_text(c.value(scene.source.background, if (plan.theme == 'dark') "#111827" else "#ffffff"));
    <section id: instance ++ "-s" ++ string(scene.index), class: "slide-layer",
        ["aria-label"]: c.as_text(c.value(scene.source.title, scene.id)),
        style: "width:" ++ c.px(plan.width) ++ ";height:" ++ c.px(plan.height) ++
            ";background:" ++ background ++ ";" ++ style,
        for (obj in scene.objects) render_object(obj, visuals, instance, plan.theme)
    >
}

pub fn fit(plan, viewport_w, viewport_h) {
    let scale = min(viewport_w / plan.width, viewport_h / plan.height)
    let x = (viewport_w - plan.width * scale) / 2.0
    let y = (viewport_h - plan.height * scale) / 2.0
    {scale: scale, x: x, y: y, transform: "translate(" ++ c.px(x) ++ "," ++ c.px(y) ++ ") scale(" ++ c.fmt(scale) ++ ")"}
}

pub fn stage(plan, address, options = {}) {
    let scene = plan.slides[address.slide]
    let viewport_w = c.value(options.width, plan.width)
    let viewport_h = c.value(options.height, plan.height)
    let fitted = fit(plan, viewport_w, viewport_h)
    let instance = c.as_text(c.value(options.instance, "slide"));
    <div class: "slide-stage", style: "width:" ++ c.px(viewport_w) ++ ";height:" ++ c.px(viewport_h) ++ ";",
        <div class: "slide-canvas", style: "width:" ++ c.px(plan.width) ++ ";height:" ++ c.px(plan.height) ++
            ";transform:" ++ fitted.transform ++ ";",
            layer(plan, scene, samples.scene(scene, address.cue, address.time_ms), instance)
        >
    >
}

pub fn document(plan, body) {
    let base = if (plan.base_uri != null) <base href: plan.base_uri> else null;
    <html lang: "en", <head *[<meta charset: "UTF-8">, <title plan.title>, base, <style css>]>
        <body class: "slide-page", body>>
}
