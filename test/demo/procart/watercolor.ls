// Run: ./lambda.exe view test/demo/procart/watercolor.ls
// Live watercolor: Curtis et al.'s fluid, pigment and paper model with Kubelka-Munk glazing, ported
// from the Washes engine (github.com/castavridis/washes-js) into lambda.procart.watercolor. Seeded
// studies paint themselves; drag on the paper to paint. The sheet advances in a frame handler,
// because each time step updates it in place (S12.1.1v3 / S12.1.3).
import dom
import resources: ~~.mod_resources
import W: .mod_watercolor
import wc: lambda.procart.watercolor
import ui: .mod_ui
import workshop: .mod_workshop

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "watercolor.css"), 'text')^

fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target), "watercolor")
fn paper_of(evt) => dom.get_element_by_id(dom.root_node(evt.target), "watercolor-paper")

// the frame clock runs only while the sheet can still change.
pn wake(owner, st) map^ {
    if (st.token == 0 and W.busy(st)^) { return {*:st,token:dom.request_frame(owner, "watercolor_frame")} }
    return st
}

fn toggle(act, text, on) element => <button type:"button",'data-act':act,'aria-pressed':string(on),
    class:(if (on) "visibility on" else "visibility"),text>

fn tool_button(st, i: int) element {
    let tool = W.TOOLS[i]
    let chip = if (tool.kind == "pigment") <span class:"chip",
        style:"background:" ++ (if (len(st.swatches) > i) st.swatches[i] else "#ddd8c8")>
        else <span class:"chip " ++ tool.kind>;
    <button type:"button",'data-act':tool.id,'aria-pressed':string(st.tool == i),title:((W.tool_title(st, i)) ^ { tool.id }),
        class:(if (st.tool == i) "on" else ""),chip;<span ((W.tool_label(st, i)) ^ { tool.id })>>
}

fn status(st) string => if (st.drag != null) "Painting" else if (st.paused) "Drying paused"
    else if (st.sheet.wet_cells > 0) "Wet" else "Dry"

view <watercolor_app>() any^ state st: (W.initial()^) {
    <body <div class:"app glasshouse workshop",id:"watercolor",'data-workshop':"watercolor",
        'data-study':st.id,'data-tool':W.TOOLS[st.tool].id,
        <header class:"bar",
            <div class:"title",<h1 "Watercolor"> <span class:"seed","seed " ++ string(st.seed)>>
            ui.navigation("watercolor");
            <div class:"actions",<button type:"button",class:"step",'data-act':"previous","‹ Previous">
                <button type:"button",class:"step primary",'data-act':"next","New seed ›">>>
        <section class:"botanical-intro",<p class:"eyebrow","LAMBDA / SIMULATED WATERCOLOR">
            <h2 "Paint that flows, pools and dries."> <p "Water carries pigment over textured paper, darkens at its drying edges and settles into the grain; transparent layers mix optically.">
            <div class:"species-tabs",role:"group",'aria-label':"Watercolor studies",
                for (item in W.STUDIES) <button type:"button",'data-act':item.id,
                    class:(if (st.id == item.id) "species active" else "species"),'aria-pressed':string(st.id == item.id),
                    <span class:"species-tag",item.tag> <strong item.title>>>>
        <main class:"botanical-body",
            <section class:"specimen-card",
                <div class:"specimen-heading",<span class:"eyebrow",upper(W.study(st.id).title)>
                    <span class:"plate-number","NO. " ++ string(st.seed)>>
                <div class:"watercolor-frame",
                    <canvas id:"watercolor-paper",width:W.SHEET_WIDTH,height:W.SHEET_HEIGHT,'aria-label':"Watercolor paper">>
                <p class:"watercolor-help","Drag on the paper to paint with the selected brush. Hold still to let paint pool.">
                <div class:"specimen-caption",<div <h2 W.study(st.id).title> <p W.study(st.id).note>>
                    <span class:"stage-label",id:"status",status(st)>>>
            <aside class:"botanical-controls",
                <section class:"botanical-card",<h2 "Brush">
                    <div class:"watercolor-tools",role:"group",'aria-label':"Brushes",for (i in 0 to (len(W.TOOLS) - 1)) tool_button(st, i)>
                    <div class:"control-label",<span "Brush size"> <output id:"size",string(st.size) ++ " / " ++ string(len(W.SIZES))>>
                    <div class:"control-pair",workshop.button("smaller","− Smaller",st.size == 1); workshop.button("larger","Larger +",st.size == len(W.SIZES))>
                    <div class:"control-label",<span "Palette"> <output id:"palette",W.palette(st).title>>
                    <div class:"control-pair",workshop.button("palette","Change palette")>>
                <section class:"botanical-card",<h2 "Paper & water">
                    <p "Wet the whole sheet for soft wet-in-wet strokes; dry it to glaze a new layer over the last.">
                    <div class:"control-pair",workshop.button("wet","Wet the sheet"); workshop.button("dry","Dry now")>
                    <div class:"control-label",<span "Drying speed"> <output id:"drying",W.DRYING[st.drying].title>>
                    <div class:"control-pair",workshop.button("drying","Change speed"); toggle("pause","Pause drying",st.paused)>
                    <div class:"control-label",<span "Physics">>
                    <div class:"control-pair",toggle("edges","Dark edges",st.edges); toggle("tilt","Tilt paper",st.tilt)>
                    <div class:"control-label",<span "Inspect">>
                    <div class:"control-pair",toggle("show-water","Show water",st.water); workshop.button("replay","Replay")>
                    <div class:"control-pair",workshop.button("clear","Clear the sheet")>>
                <section class:"botanical-card stats-card",<h2 "Simulation record">
                    for (metric in st.record) <div class:"stat",<span metric.label> <output string(metric.value)>>>
                <section class:"botanical-card recipe-card",<h2 "Behind the paint">
                    <p "A port of the Washes engine, after Curtis et al., Computer-Generated Watercolor (SIGGRAPH 1997). A shallow-water layer moves over a paper height field; pigment is carried, diffuses, settles into the paper's hollows and lifts again; lowered pressure at drying edges darkens them. Kubelka-Munk optics glaze the layers over the paper.">
                    <p class:"quiet","The same seed replays the same study, stroke for stroke.">>>>
        <footer class:"botanical-footer",<span "Generated in Lambda · One seed, many possibilities">
            <a href:"painterly.ls","Explore painterly →">>>>
}
on load(evt) {
    let owner = owner_of(evt)
    if (owner == null) { return 'pass' }
    st = wake(owner, st)^
    'handled'
}
on watercolor_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != st.token) { return 'pass' }
    let next = W.advance(st)^
    dom.set_canvas_pixels(paper_of(evt), wc.pixels(next.sheet, next.rgba))^
    st = {*:next,token:if (W.busy(next)^) dom.request_frame(evt.target, "watercolor_frame") else 0}
    'handled'
}
on pointerdown(evt) {
    let canvas = dom.closest(evt.target, "#watercolor-paper")
    if (canvas == null or evt.button != 0) { return 'pass' }
    let at = W.cell(dom.bounding_box(canvas), evt.clientX, evt.clientY)
    dom.set_pointer_capture(canvas, evt.pointerId)
    st = wake(owner_of(evt), W.press(st, at[0], at[1]))^
    'prevent-default'
}
on pointermove(evt) {
    if (st.drag == null) { return 'pass' }
    let at = W.cell(dom.bounding_box(paper_of(evt)), evt.clientX, evt.clientY)
    st = W.move(st, at[0], at[1])^
    'prevent-default'
}
on pointerup(evt) {
    if (st.drag == null) { return 'pass' }
    st = W.release(st)
    'handled'
}
on pointercancel(evt) {
    if (st.drag == null) { return 'pass' }
    st = W.release(st)
    'handled'
}
on click(evt) {
    let target = dom.closest(evt.target, "[data-act]")
    if (target == null) { return 'pass' }
    st = wake(owner_of(evt), W.command(st, dom.get_attribute(target, "data-act"))^)^
    'handled'
}
on closerequest(evt) {
    if (st.token > 0) { dom.cancel_frame(owner_of(evt), st.token) }
    st = {*:st,token:0,drag:null}
    'pass'
}

<html lang:"en",
    <head <meta charset:"UTF-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
        <title "Watercolor — Procart"> <link rel:"stylesheet",href:"world.css">
        <link rel:"stylesheet",href:"plants.css"> <style CSS>>
    apply(<watercolor_app>)>
