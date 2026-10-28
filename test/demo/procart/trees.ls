// Run: ./lambda.exe view test/demo/procart/trees.ls
import dom
import resources: ~~.mod_resources
import ui: .mod_ui
import T: .mod_trees
import viewer: .mod_scene_view
let DOCUMENTS=map([for (item in T.CATALOG) (item.id,input(url_resolve(resources.entry_uri()^,"samples/"++item.file),'mark')^)])
view <tree_studio> state st:(T.initial(DOCUMENTS)^) {
 <body <div class:"app grammar-app tree-app",
  <header class:"bar",<div class:"title",<h1 "Procart"> <span class:"seed","Tree studio / seed "++string(st.seed)>>;
   ui.navigation("trees");
   <div class:"actions",<button type:"button",'data-act':"previous","‹ Previous"> <button type:"button",class:"primary",'data-act':"next","New seed ›">>>
  <section class:"grammar-intro",<div <p class:"eyebrow","A STUDY IN GROWING FORM"> <h2 "From a bud to a canopy.">
   <p "Shape the branches. Follow the light. Keep the structure as the details change.">>;
   <div class:"grammar-edition",<span "TREE STUDIO"> <strong "P8"> <span "GROWTH / SKELETON / LEAF">>>
  <nav class:"grammar-tabs",'aria-label':"Plant architectures",for (item in T.CATALOG)
   <button type:"button",'data-act':item.id,'aria-pressed':string(st.id==item.id),class:(if (st.id==item.id) "sample-tab active" else "sample-tab"),
    <span item.tag> <strong item.title>>>
  <main class:"grammar-body",
   <section class:"grammar-stage",
    <div class:"grammar-stage-bar",<span T.description(st.id).tag> <span id:"tree-seed","SEED "++string(st.seed)>>;
    viewer.interactive(T.viewport(st),T.home());
    <div class:"grammar-caption",<div <h3 id:"tree-title",T.description(st.id).title> <p T.description(st.id).note>>;
     <div class:"grammar-count",<strong id:"branch-count",string(st.model.stats.branches)> <span "BRANCHES">>>>
   <aside class:"grammar-sidebar",
    <section class:"grammar-card",<p class:"eyebrow","GROWTH CONTROLS"> <h3 "Give it room to grow.">
     <p "Branch levels shape the crown. Directional force bends each new section as it grows.">
     <div class:"depth-control",<button type:"button",'data-act':"prune",'aria-label':"Prune a level","−">;
      <div <output id:"tree-depth",string(T.depth(st))> <span (if (st.id == "apex") "GENERATIONS" else "BRANCH LEVELS")>>;
      <button type:"button",'data-act':"grow",'aria-label':"Grow a level","+">>;
     <div class:"depth-control",<button type:"button",'data-act':"bend-down",'aria-label':"Reduce directional force","−">;
      <div <output id:"tree-force",string(round(st.document.parameters.force.strength*100.0)/100.0)> <span "DIRECTIONAL FORCE">>;
      <button type:"button",'data-act':"bend-up",'aria-label':"Increase directional force","+">>>;
    <section class:"grammar-card",<p class:"eyebrow","ONE STRUCTURE, TWO DETAILS"> <h3 "Keep the silhouette.">
     <p "Fewer rings and larger, fewer leaves. Both versions share every branch and attachment point.">
     <div class:"tree-detail",for (item in [{id:"full",title:"Full detail"},{id:"coarse",title:"Distant detail"}])
      <button type:"button",'data-act':item.id,'aria-pressed':string(st.detail==item.id),item.title>>;
     <button type:"button",class:"tree-leaves",'data-act':"leaves",'aria-pressed':string(st.leaves),(if (st.leaves) "Hide leaves" else "Show leaves")>;
     <div class:"grammar-stat",<span "Leaves"> <output id:"leaf-count",string(st.model.stats.leaves)>>;
     <div class:"grammar-stat",<span "Vertices"> <output id:"tree-vertices",string(st.model.stats.vertices)>>>;
    <section class:"grammar-card recipe-card",<p class:"eyebrow","THE SAVED SPECIMEN"> <h3 "A plant, described as data.">
     <p "The recipe and seed regenerate its structure. Viewing detail and camera remain separate.">
     <pre id:"tree-source",format(st.document,'mark')^>>>>;
  <footer class:"grammar-footer",<span "PROCART / TREE STUDIO"> <a href:"plants.ls","Explore flowers & foliage →">>>>
}
on click(evt) {
 let button=dom.closest(evt.target,"[data-act]")
 if (button == null) { return 'pass' }
 st=T.command(st,dom.get_attribute(button,"data-act"))^
 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
 <title "Tree studio — Procart"> <link rel:"stylesheet",href:"world.css"> <link rel:"stylesheet",href:"plants.css">
 <link rel:"stylesheet",href:"grammar.css"> <link rel:"stylesheet",href:"trees.css">>;apply(<tree_studio>)>
