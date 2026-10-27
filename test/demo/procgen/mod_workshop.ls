// three workshops share a seeded state machine and presentation (S12.1.1v3 / S12.1.3).
import dom
import ui: .mod_ui

pub fn initial(config) map => {seed:2026,id:config.catalog[0].id,detail:3,palette:0,overlay:false,
    model:config.generate(2026,config.catalog[0].id,3,0)^}

pub fn command(config,st,action) map {
    if (action == "overlay") {*:st,overlay:not st.overlay}
    else {
        let seed = if (action == "next") st.seed+1 else if (action == "previous") max(0,st.seed-1) else st.seed
        let id = if (any([for (item in config.catalog) item.id == action])) action else st.id
        let detail = if (action == "more") min(5,st.detail+1) else if (action == "less") max(1,st.detail-1) else st.detail
        let palette = if (action == "palette") (st.palette+1)%len(config.colorways) else st.palette;
        if (seed == st.seed and id == st.id and detail == st.detail and palette == st.palette) st
        else {*:st,seed:seed,id:id,detail:detail,palette:palette,model:config.generate(seed,id,detail,palette)^}
    }
}

fn button(action,text,disabled=false) element => <button *:(if (disabled) {disabled:true} else {}),type:"button",'data-act':action,text>

view <workshop> state st:initial(~.config) {
    <body <div class:"app glasshouse workshop",'data-workshop':~.config.id,
        <header class:"bar",
            <div class:"title",<h1 ~.config.title> <span class:"seed","seed "++string(st.seed)>>
            ui.navigation(~.config.id);
            <div class:"actions",<button type:"button",class:"step",'data-act':"previous","‹ Previous">
                <button type:"button",class:"step primary",'data-act':"next","New seed ›">>>
        <section class:"botanical-intro",<p class:"eyebrow",~.config.eyebrow>
            <h2 ~.config.heading> <p ~.config.description>
            <div class:"species-tabs",role:"group",'aria-label':~.config.title,
                for (item in ~.config.catalog) <button type:"button",'data-act':item.id,
                    class:(if (st.id == item.id) "species active" else "species"),'aria-pressed':string(st.id == item.id),
                    <span class:"species-tag",item.tag> <strong item.title>>>>
        <main class:"botanical-body",
            <section class:"specimen-card",
                <div class:"specimen-heading",<span class:"eyebrow",upper(st.model.recipe.title)>
                    <span class:"plate-number","NO. "++string(st.seed)>>;
                ~.config.draw(st.model,st.overlay);
                <div class:"specimen-caption",<div <h2 st.model.recipe.title> <p st.model.recipe.note>>
                    <span class:"stage-label","Detail "++string(st.detail)>>>
            <aside class:"botanical-controls",
                <section class:"botanical-card",<h2 "Make it your own"> <p ~.config.help>
                    <div class:"control-label",<span ~.config.detail_label> <output id:"detail",string(st.detail)++" / 5">>
                    <div class:"control-pair",button("less","− Less",st.detail == 1); button("more","More +",st.detail == 5)>
                    <div class:"control-label",<span "Colorway"> <output id:"palette",~.config.colorways[st.palette]>>
                    <div class:"control-pair",button("palette","Change colorway")>
                    <div class:"control-label",<span "Inspect">>
                    <div class:"control-pair",<button type:"button",'data-act':"overlay",'aria-pressed':string(st.overlay),
                        class:(if (st.overlay) "visibility on" else "visibility"),~.config.overlay_label>>>
                <section class:"botanical-card stats-card",<h2 "Generation record">
                    for (metric in st.model.metrics) <div class:"stat",<span metric.label> <output string(metric.value)>>>
                <section class:"botanical-card recipe-card",<h2 "Behind the form"> <p ~.config.method>
                    <p class:"quiet","The same seed and settings recreate the same result.">>>>
        <footer class:"botanical-footer",<span "Generated in Lambda · One seed, many possibilities">
            <a href:"plants.ls","Explore plants & foliage →">>>>
}
on click(evt) {
    let target = dom.closest(evt.target,"[data-act]")
    if (target == null) { return 'pass' }
    st = command(~.config,st,dom.get_attribute(target,"data-act"))
    'handled'
}

pub fn page(config) element => <html lang:"en",
    <head <meta charset:"UTF-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
        <title config.title++" — lambda.procgen"> <link rel:"stylesheet",href:"world.css">
        <link rel:"stylesheet",href:"plants.css">>
    apply(<workshop config:config>)>
