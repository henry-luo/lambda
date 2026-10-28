// Run: ./lambda.exe view test/demo/procart/meadow.ls
import dom
import resources: ~~.mod_resources
import ui: .mod_ui
import meadow: .mod_meadow
import viewer: .mod_scene_view
import environment: lambda.procart.environment

let SOURCE = input(url_resolve(resources.entry_uri()^,"meadow.mark"),'mark')^
let INITIAL=meadow.initial(SOURCE)^
let LAYERS = [{id:"flower",label:"Wildflowers",note:"Six species, from poppies to lilies"},
    {id:"tree",label:"Trees",note:"A branching grove"},{id:"stone",label:"Stones",note:"Weathered granite"},
    {id:"grass",label:"Grass & reeds",note:"Fine blades, tufts & pond reeds"},{id:"pond",label:"Stillwater pond",note:"Water, lily pads & a sandy bank"}]

view <meadow_app> state st:INITIAL {
    <body <div class:"app glasshouse meadow-app",
        <header class:"bar",
            <div class:"title",<h1 "Procart"> <span class:"seed","Meadow / seed "++string(st.model.seed)>>;
            ui.navigation("meadow");
            <div class:"actions",
                <button type:"button",class:"step",'data-act':"previous","‹ Previous">
                <button type:"button",class:"step primary",'data-act':"next","New meadow ›">>>
        <section class:"meadow-intro",
            <div <p class:"eyebrow","A PROCEDURAL LANDSCAPE / 01">
                <h2 "A wider world, alive with weather.">
                <p "A path through the wildflowers. A quiet pond at the edge of the grove. Turn the day, bring the rain, and watch the meadow respond.">>
            <div class:"meadow-seal",<span "FIELD NOTES"> <strong "The meadow"> <span "ONE SEED · MANY LIVING FORMS">>>
        <main class:"meadow-body",
            <section class:"meadow-stage",'aria-label':"Interactive meadow",
                <div class:"meadow-stage-label",<span "A LIVING SUMMER CLEARING"> <span id:"meadow-seed","SEED "++string(st.model.seed)>>;
                viewer.interactive(meadow.viewport(st.model,st.visibility,st.motion,st.camera)^,meadow.home(),st.camera);
                <div class:"meadow-caption",<div <h3 "Between the grove and the water"> <p "Sunflowers turn toward the light. Water lilies fold for the night. Grasses move with the wind.">>
                    <span class:"stage-label",string(len(st.model.objects))++" living & mineral forms">>>
            <aside class:"botanical-controls meadow-controls",
                <section class:"botanical-card environment-card",<p class:"eyebrow","THE AIR AROUND IT"> <h2 "A day in the meadow">
                    <div class:"environment-reading",<strong id:"environment-clock",meadow.clock(st.model.environment.config.time)>;
                        <span id:"environment-weather",st.model.environment.config.weather>>;
                    <label class:"environment-control",'for':"env-time",<span "Solar time">;
                        <input id:"env-time",type:"range",min:"0",max:"24",step:"0.25",value:string(st.model.environment.config.time),'data-env':"time">>;
                    <div class:"environment-times",for (period in [{hour:8.0,title:"Morning"},{hour:12.0,title:"Noon"},{hour:18.0,title:"Evening"},{hour:0.0,title:"Night"}])
                        <button type:"button",'data-hour':string(period.hour),period.title>>;
                    <div class:"environment-weather",role:"group",'aria-label':"Weather",for (weather in environment.WEATHER)
                        <button type:"button",'data-weather':weather,'aria-pressed':string(st.model.environment.config.weather==weather),weather>>;
                    <label class:"environment-control",'for':"env-wind",<span "Wind speed"> <output id:"wind-value",string(st.model.environment.config.wind_speed)++" m/s">;
                        <input id:"env-wind",type:"range",min:"0",max:"15",step:"0.5",value:string(st.model.environment.config.wind_speed),'data-env':"wind_speed">>;
                    <label class:"environment-control",'for':"env-direction",<span "Wind direction"> <output string(st.model.environment.config.wind_direction)++"°">;
                        <input id:"env-direction",type:"range",min:"0",max:"360",step:"10",value:string(st.model.environment.config.wind_direction),'data-env':"wind_direction">>;
                    <label class:"environment-control",'for':"env-humidity",<span "Humidity"> <output id:"humidity-value",string(round(st.model.environment.config.humidity*100.0))++"%">;
                        <input id:"env-humidity",type:"range",min:"0",max:"1",step:"0.05",value:string(st.model.environment.config.humidity),'data-env':"humidity">>;
                    <div class:"environment-location",
                        <label 'for':"env-latitude","Latitude °";<input id:"env-latitude",type:"number",min:"-90",max:"90",step:"1",value:string(st.model.environment.config.latitude),'data-env':"latitude">>;
                        <label 'for':"env-altitude","Altitude m";<input id:"env-altitude",type:"number",min:"-500",max:"9000",step:"50",value:string(st.model.environment.config.altitude),'data-env':"altitude">>;
                        <label 'for':"env-day","Day of year";<input id:"env-day",type:"number",min:"1",max:"365",step:"1",value:string(st.model.environment.config.day),'data-env':"day">>>;
                    <label class:"environment-control",'for':"env-sun",<span "Sunlight"> <output string(st.model.environment.config.lighting.sun)++"×">;
                        <input id:"env-sun",type:"range",min:"0",max:"2",step:"0.1",value:string(st.model.environment.config.lighting.sun),'data-env':"sun">>;
                    <div class:"environment-actions",<button type:"button",'data-act':"motion",'aria-pressed':string(st.motion),(if (st.motion) "Pause motion" else "Resume motion")>;
                        <button type:"button",'data-act':"reset-environment","Reset day">>;
                    <p class:"environment-note","Local solar time · wind blows toward the selected bearing. Conditions change each plant’s pose; its seed stays the same.">>
                <section class:"botanical-card",<p class:"eyebrow","COMPOSE THE CLEARING"> <h2 "Layers of a landscape">
                    <p "Show each part on its own, or bring the whole meadow together.">
                    <div class:"meadow-layers",for (layer in LAYERS)
                        <button type:"button",class:(if (st.visibility[layer.id]) "meadow-layer on" else "meadow-layer"),
                            'data-act':layer.id,'aria-pressed':string(st.visibility[layer.id]),
                            <span class:"layer-dot",'data-layer':layer.id>; <span class:"layer-copy",<strong layer.label> <small layer.note>>;
                            <span class:"layer-count",string(if (layer.id=="pond") 1 else st.model.counts[layer.id])>>>>
                <section class:"botanical-card meadow-notes",<p class:"eyebrow","FIELD NOTES"> <h2 "Small rules, a shared place.">
                    <p "Six flower varieties, a grove, weathered stones, grasses and reeds share a 28 × 20 landscape. The pond gives lilies a place to grow.">
                    <p "Drag to look around. Use the camera controls to find a detail, or choose a new seed to walk into another meadow.">
                    <div class:"meadow-record",<span "Seed"> <output id:"seed-value",string(st.model.seed)>>;
                    <div class:"meadow-record",<span "Flower varieties"> <output string(len(unique([for (o in st.model.objects where o.kind == "flower") o.recipe])))>>;
                    <div class:"meadow-record",<span "Grass varieties"> <output string(len(unique([for (o in st.model.objects where o.kind == "grass") o.recipe])))>>>>>
        <footer class:"botanical-footer",<span "Procart / A study in procedural landscapes">
            <a href:"plants.ls","Explore the glasshouse →">>>>
}
on click(evt) {
    let weather=dom.closest(evt.target,"button[data-weather]")
    let hour=dom.closest(evt.target,"[data-hour]")
    if (weather != null) { let pose=viewer.snapshot(evt.target,"meadow-camera",meadow.home())^; st={*:meadow.edit_environment(st,"weather",dom.get_attribute(weather,"data-weather"))^,camera:pose}; return 'handled' }
    if (hour != null) { let pose=viewer.snapshot(evt.target,"meadow-camera",meadow.home())^; st={*:meadow.edit_environment(st,"time",float(dom.get_attribute(hour,"data-hour")))^,camera:pose}; return 'handled' }
    let button = dom.closest(evt.target,"[data-act]")
    if (button == null) { return 'pass' }
    let pose=viewer.snapshot(evt.target,"meadow-camera",meadow.home())^
    st = {*:meadow.command(st,dom.get_attribute(button,"data-act"))^,camera:pose}
    'handled'
}

on change(evt) {
    let key=dom.get_attribute(evt.target,"data-env")
    if (key == null) { return 'pass' }
    let value=float(dom.get_state(evt.target,"value"))
    let pose=viewer.snapshot(evt.target,"meadow-camera",meadow.home())^
    st={*:(meadow.edit_environment(st,key,if (key=="day") int(value) else value) ^ { st }),camera:pose}
    'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
        <title "Meadow — Procart">
        <link rel:"stylesheet",href:"world.css"> <link rel:"stylesheet",href:"plants.css"> <link rel:"stylesheet",href:"meadow.css">>
    apply(<meadow_app>)>
