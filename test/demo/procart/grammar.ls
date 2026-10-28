// Run: ./lambda.exe view test/demo/procart/grammar.ls
import dom
import resources: ~~.mod_resources
import ui: .mod_ui
import G: .mod_grammar
import viewer: .mod_scene_view
let CATALOG = [
 {id:"primitives",title:"The nine forms",tag:"01 / VOCABULARY",note:"A box, a stroke, a sphere. Nine small building blocks, each with its own character.",file:"primitives.mark"},
 {id:"ribbons",title:"Turning ribbons",tag:"02 / REPETITION",note:"One tile. A turn, a shift, a little less scale. The same rule becomes a trail of color.",file:"ribbons.mark"},
 {id:"branchwork",title:"Branchwork",tag:"03 / RECURSION",note:"A branch becomes two. Smaller frames carry the next generation into new directions.",file:"branchwork.mark"},
 {id:"garden",title:"A sculpted garden",tag:"04 / COMPOSITION",note:"Living recipes and simple geometry share a frame, a palette, and a place to grow.",file:"garden.mark"},
 {id:"surfaces",title:"Grain, bark & veins",tag:"05 / SURFACES",note:"Mineral veins follow the stone. Bark follows each branch. Leaf veins run from base to tip.",file:"surfaces.mark"}]
let DOCUMENTS = map([for (item in CATALOG) (item.id,input(url_resolve(resources.entry_uri()^,"samples/"++item.file),'mark')^)])
fn description(id) => [for (item in CATALOG where item.id == id) item][0]
view <grammar_app> state st:(G.initial(DOCUMENTS)^) {
 <body <div class:"app grammar-app",
  <header class:"bar",
   <div class:"title",<h1 "Procart"> <span class:"seed", "Grammar / seed "++string(st.seed)>>;
   ui.navigation("grammar");
   <div class:"actions",<button type:"button",'data-act':"previous","‹ Previous"> <button type:"button",class:"primary",'data-act':"next","New seed ›">>>
  <section class:"grammar-intro",<div <p class:"eyebrow","A STUDY IN GENERATIVE FORM"> <h2 "Small rules. Endless forms.">
   <p "Compose a shape. Adjust its frame. Let a rule carry the idea further.">>;
   <div class:"grammar-edition",<span "FORM STUDIES"> <strong "01—05"> <span "SEED / FRAME / SURFACE">>>
  <nav class:"grammar-tabs",'aria-label':"Grammar samples",for (item in CATALOG)
   <button type:"button",'data-act':item.id,'aria-pressed':string(st.id == item.id),class:(if (st.id == item.id) "sample-tab active" else "sample-tab"),
    <span item.tag> <strong item.title>>>
  <main class:"grammar-body",
   <section class:"grammar-stage",
    <div class:"grammar-stage-bar",<span id:"sample-tag",description(st.id).tag> <span id:"grammar-seed","SEED "++string(st.seed)>>;
    viewer.interactive(G.viewport(st.model,st.id),G.home(st.id));
    <div class:"grammar-caption",<div <h3 id:"sample-title",description(st.id).title> <p id:"sample-note",description(st.id).note>>;
     <div class:"grammar-count",<strong id:"object-count",string(st.model.stats.objects)> <span "FORMS">>>>
   <aside class:"grammar-sidebar",
    <section class:"grammar-card",<p class:"eyebrow","GLOBAL SETTINGS"> <h3 "How far does it grow?">
     <p "Limit the number of nested rule calls. Each level gives the composition another chance to unfold.">
     <div class:"depth-control",<button type:"button",'data-act':"less-depth",'aria-label':"Decrease maximum depth","−">;
      <div <output id:"max-depth",string(st.model.settings.max_depth)> <span "MAX DEPTH">>;
      <button type:"button",'data-act':"more-depth",'aria-label':"Increase maximum depth","+">>;
     <div class:"grammar-stat",<span "Vertices"> <output id:"vertex-count",string(st.model.stats.vertices)>>;
     <div class:"grammar-stat",<span "Retired branches"> <output id:"retired-count",string(st.model.stats.depth_retired+st.model.stats.size_retired)>>>;
    <section class:"grammar-card recipe-card",<p class:"eyebrow","THE COMPOSITION"> <h3 "A description, kept as data.">
     <p "Scale, color and placement travel through the hierarchy. Each shape receives its own adjusted frame.">
     <pre id:"grammar-source",format(st.document,'mark')^>>;
    <section class:"grammar-card grammar-key",<p class:"eyebrow","DRAWING VOCABULARY"> <div class:"primitive-key",for (kind in ["box","grid","sphere","line","point","triangle","mesh","cylinder","tube"]) <span kind>>>>>;
  <footer class:"grammar-footer",<span "PROCART / FORM STUDIES"> <a href:"meadow.ls","Step into the meadow →">>>>
}
on click(evt) {
 let button=dom.closest(evt.target,"[data-act]")
 if (button == null) { return 'pass' }
 st=G.command(st,dom.get_attribute(button,"data-act"))^
 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
 <title "Form studies — Procart"> <link rel:"stylesheet",href:"world.css"> <link rel:"stylesheet",href:"plants.css"> <link rel:"stylesheet",href:"grammar.css">>;
 apply(<grammar_app>)>
