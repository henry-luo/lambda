pub let demos = [
    {id:"domino",title:"Domino circuit",caption:"One nudge. Thirty-two collisions.",file:"domino.ls",tag:"CHAIN REACTION"},
    {id:"salvage",title:"Salvage bay",caption:"Compound shapes, drifting in zero gravity.",file:"salvage.ls",tag:"MASS & INERTIA"},
    {id:"sorter",title:"Marble sorter",caption:"A rotating paddle and three live sensor gates.",file:"sorter.ls",tag:"KINEMATICS & SENSORS"},
    {id:"bounce",title:"Bounce lab",caption:"Five materials. One drop. Compare the traces.",file:"bounce.ls",tag:"SIMULATION & ANALYSIS"}
]

pub fn head(title) element => <head <meta charset:"utf-8"> <meta name:"viewport",content:"width=device-width,initial-scale=1">
    <title title++" / Physics playground"> <link rel:"stylesheet",href:"physics.css">
    <script type:"importmap", "{\"imports\":{\"three\":\"../scene3d/vendor/three/three.module.js\"}}">>

pub fn navigation(active) element => <nav class:"navigation",'aria-label':"Physics demonstrations",
    <a href:"index.ls",class:(if (active == "index") "active" else ""), "All experiments">
    for (demo in demos) <a href:demo.file,class:(if (active == demo.id) "active" else ""),demo.title>>

fn action_button(action) element => <button id:action.id,type:"button",class:(if (action.primary == true) "primary" else ""),
    'data-request':format(action.request,'json'),'data-restart':string(action.restart == true),
    'data-play-after':string(action.play == true),action.label>

pub fn page(kind, scene, options) element => <html
    head(options.title);
    <body <main
        <header class:"page-header", <div <p class:"eyebrow", "LAMBDA PHYSICS / PLAYGROUND"> <h1 options.title>>
            <a class:"edition",href:"index.ls", "FOUR EXPERIMENTS · 3D">>
        navigation(kind);
        <p class:"intro",options.description>
        <div class:"toolbar",role:"group",'aria-label':"Simulation controls",
            for (action in options.actions) action_button(action);
            <span class:"toolbar-divider">
            <button id:"play-toggle",type:"button", "Pause / Play">
            <button id:"step",type:"button", "Step 1/60 s">
            <button id:"reset",type:"button", "Reset">
            <span class:"toolbar-divider">
            <button id:"camera-home",type:"button", "Overview">
            <button id:"camera-top",type:"button", "Top view">>
        <div class:"stage",scene>
        <div class:"metrics",
            <div class:"metric", <span class:"metric-label",options.labels[0]> <strong id:"metric-primary", "—">>
            <div class:"metric", <span class:"metric-label",options.labels[1]> <strong id:"metric-secondary", "—">>
            <div class:"metric", <span class:"metric-label",options.labels[2]> <strong id:"metric-tertiary", "—">>
            <div class:"metric clock", <span class:"metric-label", "SIMULATION TIME"> <strong id:"clock", "0.00 s">>>
        <div class:"scene-caption", <p options.note> <p class:"hint", "Drag to orbit · Shift-drag to pan · Scroll to zoom · Space to pause">>
        if (options.chart != null) <section class:"plot-card", <h2 "The same experiment, measured"> <p options.chart_note> options.chart>;
        <p id:"status",role:"status", "Preparing experiment…">
    > <script type:"module",src:"playground.js">>
>
