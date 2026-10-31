// Run: ./lambda.exe view test/demo/procart/painterly.ls
// Photographs repainted with brush strokes: the four modes of github.com/weimegan/painterly,
// three procedural brushes and two bundled photos. Painting runs in the load and click
// handlers because stamping strokes into the canvas is a procedure (S12.1.1v3 / S12.1.3).
import dom
import resources: ~~.mod_resources
import P: .mod_painterly
import ui: .mod_ui
import workshop: .mod_workshop

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "painterly.css"), 'text')^

fn initial() map => {seed:2026,id:"oriented",detail:3,brush:0,photo:0,original:false,field:null,model:null,note:null}

let PHOTOS = [for (photo in P.SOURCES) url_resolve(BASE, photo.path)^]

// settings change here; a changed painting is repainted by the handler.
fn command(st, action) map {
    if (action == "original") {*:st,original:not st.original}
    else {*:st,
        seed:if (action == "next") st.seed + 1 else if (action == "previous") max(0, st.seed - 1) else st.seed,
        id:if (any([for (item in P.CATALOG) item.id == action])) action else st.id,
        detail:if (action == "more") min(5, st.detail + 1) else if (action == "less") max(1, st.detail - 1) else st.detail,
        brush:if (action == "brush") (st.brush + 1) % len(P.BRUSHES) else st.brush,
        photo:if (action == "photo") (st.photo + 1) % len(P.SOURCES) else st.photo}
}

fn same_painting(a, b) bool => a.seed == b.seed and a.id == b.id and a.detail == b.detail and a.brush == b.brush and a.photo == b.photo

// the photo's analysis is kept while only the strokes change.
pn repaint(st) map^ {
    let field = if (st.field != null and st.field.photo == st.photo) st.field
        else {*:P.analyze(P.source(input(PHOTOS[st.photo], 'binary')^)^),photo:st.photo}
    let plan = P.plan(field, st.seed, st.id, st.detail)
    let image = P.encode(plan, P.paint(plan, P.brush(P.BRUSHES[st.brush].id)))^
    return {*:st,field:field,note:null,model:{recipe:plan.recipe,uri:image.uri,width:plan.width,height:plan.height,
        metrics:[{label:"Canvas",value:string(plan.width) ++ " × " ++ string(plan.height)},
            {label:"Coarse strokes",value:len(plan.layers[0].pixels)},
            {label:"Detail strokes",value:if (len(plan.layers) == 2) len(plan.layers[1].pixels) else 0},
            {label:"Brush size",value:string(P.BRUSH_SIZE) ++ " px" ++ (if (len(plan.layers) == 2) " + " ++ string(P.BRUSH_SIZE div 4) ++ " px" else "")},
            {label:"Directions",value:if (plan.recipe.oriented) P.ANGLES else 1}]}}
}

pn painted(st) map {
    let next: map | error = repaint(st)
    if (next is error) {*:st,note:"Cannot paint this photo: " ++ next.message} else next
}

fn recipe(st) map => [for (item in P.CATALOG where item.id == st.id) item][0]

fn canvas(st) element {
    if (st.original) <img class:"specimen painterly-canvas",src:PHOTOS[st.photo],alt:P.SOURCES[st.photo].title>
    else if (st.model == null) <div class:"painterly-pending", if (st.note == null) "Painting…" else st.note>
    else <img class:"specimen painterly-canvas",src:st.model.uri,width:st.model.width,height:st.model.height,
        alt:recipe(st).title ++ " painting of " ++ P.SOURCES[st.photo].title>
}

view <painterly_app> state st: initial() {
    <body <div class:"app glasshouse workshop",'data-workshop':"painterly",
        <header class:"bar",
            <div class:"title",<h1 "Painterly"> <span class:"seed","seed " ++ string(st.seed)>>
            ui.navigation("painterly");
            <div class:"actions",<button type:"button",class:"step",'data-act':"previous","‹ Previous">
                <button type:"button",class:"step primary",'data-act':"next","New seed ›">>>
        <section class:"botanical-intro",<p class:"eyebrow","LAMBDA / NON-PHOTOREALISTIC RENDERING">
            <h2 "Photographs, repainted."> <p "Thousands of jittered brush strokes rebuild a photo, coarse to fine, optionally turned along its edges.">
            <div class:"species-tabs",role:"group",'aria-label':"Painting modes",
                for (item in P.CATALOG) <button type:"button",'data-act':item.id,
                    class:(if (st.id == item.id) "species active" else "species"),'aria-pressed':string(st.id == item.id),
                    <span class:"species-tag",item.tag> <strong item.title>>>>
        <main class:"botanical-body",
            <section class:"specimen-card",
                <div class:"specimen-heading",<span class:"eyebrow",upper(if (st.original) "Source photo" else recipe(st).title)>
                    <span class:"plate-number","NO. " ++ string(st.seed)>>;
                canvas(st);
                <div class:"specimen-caption",<div <h2 recipe(st).title> <p recipe(st).note>>
                    <span class:"stage-label","Density " ++ string(st.detail)>>>
            <aside class:"botanical-controls",
                <section class:"botanical-card",<h2 "Make it your own">
                    <p "Change the stroke density, photo or brush. Show photo compares the painting with its source.">
                    <div class:"control-label",<span "Stroke density"> <output id:"detail",string(st.detail) ++ " / 5">>
                    <div class:"control-pair",workshop.button("less","− Less",st.detail == 1); workshop.button("more","More +",st.detail == 5)>
                    <div class:"control-label",<span "Photo"> <output id:"photo",P.SOURCES[st.photo].title>>
                    <div class:"control-pair",workshop.button("photo","Change photo")>
                    <div class:"control-label",<span "Brush"> <output id:"brush",P.BRUSHES[st.brush].title>>
                    <div class:"control-pair",workshop.button("brush","Change brush")>
                    <div class:"control-label",<span "Inspect">>
                    <div class:"control-pair",<button type:"button",'data-act':"original",'aria-pressed':string(st.original),
                        class:(if (st.original) "visibility on" else "visibility"),"Show photo">>>
                <section class:"botanical-card stats-card",<h2 "Generation record">
                    for (metric in (if (st.model == null) [] else st.model.metrics)) <div class:"stat",<span metric.label> <output string(metric.value)>>>
                <section class:"botanical-card recipe-card",<h2 "Behind the form">
                    <p "A port of weimegan/painterly. Strokes are importance-sampled toward sharp detail and tinted from the photo with color jitter. Oriented modes turn each stroke along the edge direction of the image's structure tensor. Stroke plans are pure; only stamping the strokes into the canvas is a procedure.">
                    <p class:"quiet","The same seed and settings recreate the same painting.">>>>
        <footer class:"botanical-footer",<span "Generated in Lambda · One seed, many possibilities">
            <a href:"textures.ls","Explore textures & materials →">>>>
}
on load(evt) {
    // the first painting needs the procedural stamp pass, which the fn initializer cannot run
    if (st.model != null or st.note != null) { return 'pass' }
    st = painted(st)
    'handled'
}
on click(evt) {
    let target = dom.closest(evt.target, "[data-act]")
    if (target == null) { return 'pass' }
    let next = command(st, dom.get_attribute(target, "data-act"))
    st = if (same_painting(next, st) and st.model != null) next else painted(next)
    'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
        <title "Painterly — Procart"> <link rel:"stylesheet",href:"world.css">
        <link rel:"stylesheet",href:"plants.css"> <style CSS>>
    apply(<painterly_app>)>
