// Run: ./lambda.exe view test/demo/procart/plants.ls
import dom
import ui: .mod_ui
import botany: .mod_plants
import draw: .mod_botanical
import three: .mod_botanical3d

// HTML boolean attributes are present only when enabled, including in native view.
fn action(id, text, disabled = false) element => <button *:(if (disabled) {disabled:true} else {}),type:"button",'data-act':id,text>

fn visibility(id, text, enabled, disabled = false) element => <button *:(if (disabled) {disabled:true} else {}),type:"button",class:(if (enabled) "visibility on" else "visibility"),
    'data-act':id,'aria-pressed':string(enabled),text>

view <plant_app> state st: botany.initial() {
    let tips = if (st.mode == "3d") three.tip_count(st.plant) else len(st.plant.tips);
    let details = if (st.plant.detail_count != null) st.plant.detail_count else tips;
    <body <div class:"app glasshouse",
        <header class:"bar",
            <div class:"title",<h1 "The glasshouse"> <span class:"seed","seed "++string(st.seed)>>
            ui.navigation("plants");
            <div class:"actions",
                <button type:"button",class:"step",'data-act':"previous","‹ Previous">
                <button type:"button",class:"step primary",'data-act':"next","New seed ›">>>
        <section class:"botanical-intro",
            <p class:"eyebrow","LAMBDA / PROCEDURAL BOTANY">
            <h2 "A seed. A living form.">
            <p "From feathered ferns to layered blooms and pond flowers. Explore "++string(len(botany.SPECIES))++" seeded forms, in illustration or native 3D.">
            <div class:"collection-tabs",role:"group",'aria-label':"Plant collections",
                visibility("group-garden","Garden plants & flowers",st.section == "garden");
                visibility("group-foliage","Leaves & foliage",st.section == "foliage")>
            <div class:"species-tabs",role:"group",'aria-label':"Plant species",
                for (s in botany.visible(st)) <button type:"button",'data-act':s.id,class:(if (st.id == s.id) "species active" else "species"),
                    'aria-pressed':string(st.id == s.id),<span class:"species-tag",s.tag> <strong s.title>>>>
        <main class:"botanical-body",
            <section class:"specimen-card",
                <div class:"specimen-heading",<span class:"eyebrow","BOTANICAL STUDY / "++upper(st.id)>
                    <div class:"view-modes",role:"group",'aria-label':"Specimen view",
                        visibility("view2d","Illustration",st.mode == "2d"); visibility("view3d","3D",st.mode == "3d")>>;
                (if (st.mode == "3d") three.specimen(st.plant,st.leaves,st.blooms) else draw.specimen(st.plant,st.leaves,st.blooms));
                <div class:"specimen-caption",<div <h2 st.plant.recipe.title> <p st.plant.recipe.note>>
                    <span class:"stage-label","Generation "++string(st.order)>>>
            <aside class:"botanical-controls",
                <section class:"botanical-card",<h2 "Shape the specimen">
                    <p "Small rules. Repeated growth. A different plant from every seed.">
                    <div class:"control-label",<span "Growth"> <output id:"growth",string(st.order)++" / 5">>
                    <div class:"control-pair",action("prune","− Less growth",st.order == 1); action("grow","More growth +",st.order == 5)>
                    <div class:"control-label",<span (if (st.plant.recipe.kind == "layered") "Petal opening" else "Branch angle")>
                        <output id:"angle",string(st.angle)++"°">>
                    <div class:"control-pair",action("narrower","− Narrower",st.angle == 15); action("wider","Wider +",st.angle == 40)>
                    <div class:"control-label",<span "Show organs">>
                    <div class:"control-pair",visibility("leaves",(if (st.id == "cactus") "Spines" else if (st.id == "fungus") "Cap spots" else "Leaves"),st.leaves);
                        visibility("blooms",(if (st.plant.recipe.detail_label != null) st.plant.recipe.detail_label else "Flowers"),st.blooms and st.plant.recipe.petals > 0,st.plant.recipe.petals == 0)>>
                <section class:"botanical-card stats-card",<h2 "Growth record">
                    <div class:"stat",<span "Stem segments"> <output id:"segments",string(len(st.plant.segments))>>
                    <div class:"stat",<span (if (st.section == "foliage") "Leaf blades" else if (st.id == "fungus") "Mushroom caps" else "Growing tips")> <output id:"tips",string(tips)>>
                    <div class:"stat",<span (if (st.plant.recipe.detail_label != null) st.plant.recipe.detail_label else if (st.id == "lavender") "Flower spikes" else "Flowers")>
                        <output id:"flowers",string(if (st.blooms and st.plant.recipe.petals > 0) details else 0)>>>
                <section class:"botanical-card recipe-card",<h2 "Inside the growth">
                    <p (if (st.plant.recipe.kind == null) "An L-system rewrites X into stems and branches. Each generation repeats the recipe."
                        else "Seeded solid organs build each specimen. Growth adds layers, shoots or caps; angle changes how they open and spread.")>
                    <p class:"recipe-key",(if (st.plant.recipe.kind == null) "F → grow · + / − → turn · [ ] → branch" else "Seed → dimensions · Growth → organs · Angle → spread")>
                    <p "Explore solid leaves, radial petals, cactus ribs and spines, or mushroom caps and gills. The 3D view grows branches around the stem.">
                    <p class:"quiet","The same seed and settings regrow the same specimen.">>>>
        <footer class:"botanical-footer",<span "Grown with lambda.procgen.lsystem + keyed randomness">
            <a href:"world.ls","Explore terrain generation →">>>>
}
on click(evt) {
    let button = dom.closest(evt.target,"[data-act]")
    if (button == null) { return 'pass' }
    st = botany.command(st,dom.get_attribute(button,"data-act"))
    'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
        <title "The glasshouse — Procart">
        <link rel:"stylesheet",href:"world.css"> <link rel:"stylesheet",href:"plants.css">>
    apply(<plant_app>)>
