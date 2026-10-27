// Run: ./lambda.exe view test/demo/procart/meadow.ls
import dom
import resources: ~~.mod_resources
import ui: .mod_ui
import meadow: .mod_meadow
import viewer: .mod_scene_view

let SOURCE = input(url_resolve(resources.entry_uri()^,"meadow.mark"),'mark')^
let LAYERS = [{id:"flower",label:"Wildflowers",note:"Cosmos, sunflowers & lavender"},
    {id:"tree",label:"Trees",note:"A branching grove"},{id:"stone",label:"Stones",note:"Weathered granite"},
    {id:"grass",label:"Grass patches",note:"Fine blades & taller tufts"}]

view <meadow_app> state st:(meadow.initial(SOURCE)^) {
    <body <div class:"app glasshouse meadow-app",
        <header class:"bar",
            <div class:"title",<h1 "Procart"> <span class:"seed","Meadow / seed "++string(st.model.seed)>>;
            ui.navigation("meadow");
            <div class:"actions",
                <button type:"button",class:"step",'data-act':"previous","‹ Previous">
                <button type:"button",class:"step primary",'data-act':"next","New meadow ›">>>
        <section class:"meadow-intro",
            <div <p class:"eyebrow","A PROCEDURAL LANDSCAPE / 01">
                <h2 "A little world, grown together.">
                <p "Wildflowers gather along a winding path. A grove gives shade, grasses bend, and weathered stones settle into the earth.">>
            <div class:"meadow-seal",<span "FIELD NOTES"> <strong "The meadow"> <span "ONE SEED · MANY LIVING FORMS">>>
        <main class:"meadow-body",
            <section class:"meadow-stage",'aria-label':"Interactive meadow",
                <div class:"meadow-stage-label",<span "A SUMMER CLEARING"> <span id:"meadow-seed","SEED "++string(st.model.seed)>>;
                viewer.interactive(meadow.viewport(st.model,st.visibility)^,meadow.home());
                <div class:"meadow-caption",<div <h3 "At the edge of the grove"> <p "A patchwork of pink, gold and violet. Every seed grows a different clearing.">>
                    <span class:"stage-label",string(len(st.model.objects))++" living & mineral forms">>>
            <aside class:"botanical-controls meadow-controls",
                <section class:"botanical-card",<p class:"eyebrow","COMPOSE THE CLEARING"> <h2 "Layers of a landscape">
                    <p "Show each part on its own, or bring the whole meadow together.">
                    <div class:"meadow-layers",for (layer in LAYERS)
                        <button type:"button",class:(if (st.visibility[layer.id]) "meadow-layer on" else "meadow-layer"),
                            'data-act':layer.id,'aria-pressed':string(st.visibility[layer.id]),
                            <span class:"layer-dot",'data-layer':layer.id>; <span class:"layer-copy",<strong layer.label> <small layer.note>>;
                            <span class:"layer-count",string(st.model.counts[layer.id])>>>>
                <section class:"botanical-card meadow-notes",<p class:"eyebrow","FIELD NOTES"> <h2 "Small rules, a shared place.">
                    <p "Flower patches, trees, stones and two kinds of grass grow from their own recipes. They share one landscape and one seed.">
                    <p "Drag to look around. Use the camera controls to find a detail, or choose a new seed to walk into another meadow.">
                    <div class:"meadow-record",<span "Seed"> <output id:"seed-value",string(st.model.seed)>>;
                    <div class:"meadow-record",<span "Flower varieties"> <output string(len(unique([for (o in st.model.objects where o.kind == "flower") o.recipe])))>>;
                    <div class:"meadow-record",<span "Grass varieties"> <output string(len(unique([for (o in st.model.objects where o.kind == "grass") o.recipe])))>>>>>
        <footer class:"botanical-footer",<span "Procart / A study in procedural landscapes">
            <a href:"plants.ls","Explore the glasshouse →">>>>
}
on click(evt) {
    let button = dom.closest(evt.target,"[data-act]")
    if (button == null) { return 'pass' }
    st = meadow.command(st,dom.get_attribute(button,"data-act"))^
    'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
        <title "Meadow — Procart">
        <link rel:"stylesheet",href:"world.css"> <link rel:"stylesheet",href:"plants.css"> <link rel:"stylesheet",href:"meadow.css">>
    apply(<meadow_app>)>
