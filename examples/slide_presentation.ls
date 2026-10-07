// ./lambda.exe view examples/slide_presentation.ls
import slide: lambda.slide

let deck = <presentation title: "Lambda Slides", width: 960.0, height: 540.0,
    <slide id: "welcome", background: "#eff6ff",
        <text id: "title", x: 70.0, y: 80.0, width: 820.0, height: 90.0,
            role: 'title', "Lambda Slides">
        <text id: "subtitle", x: 70.0, y: 200.0, width: 800.0, height: 80.0,
            "Slides, effects and orchestration in Lambda Script.">
        <shape id: "dot", kind: 'circle', x: 70.0, y: 340.0, width: 90.0, height: 90.0,
            fill: "#2563eb", morph_id: "dot">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 500.0>>
        <cue <parallel <effect target: "subtitle", kind: 'fly-in', duration: 500.0, from: 'left'>
            <effect target: "dot", kind: 'zoom-in', duration: 500.0>>>
        <notes "Play the title; Next reveals the subtitle and circle.">
    >
    <slide id: "effects", transition: 'push', transition_duration: 500.0,
        <text id: "title", x: 70.0, y: 80.0, width: 820.0, height: 90.0,
            role: 'title', "Click to build">
        <shape id: "dot", kind: 'circle', x: 500.0, y: 300.0, width: 140.0, height: 140.0,
            fill: "#2563eb", morph_id: "dot">
        <text id: "body", x: 70.0, y: 210.0, width: 390.0, height: 150.0,
            "Fade, fly, zoom, spin, pulse and wipe.">
        <cue <effect target: "body", kind: 'wipe-in', duration: 700.0>>
        <cue <effect target: "dot", kind: 'pulse', duration: 500.0, repeat: 2>>
    >
>
slide.page(deck, {instance: "demo", width: 960.0, height: 540.0})^
