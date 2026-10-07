import slide: lambda.slide
import html: lambda.slide.html

let deck = <presentation width: 360.0, height: 200.0,
    <slide id: "first", background: "#ffffff",
        <shape id: "dot", morph_id: "dot", kind: 'circle', x: 20.0, y: 20.0,
            width: 40.0, height: 40.0, fill: "#0000ff">
        <text id: "paragraph", x: 20.0, y: 100.0, width: 320.0, height: 70.0, font_size: 20.0, "First paragraph">
        <cue start: 'entry', <effect target: "paragraph", kind: 'fade-in', duration: 100.0>>
        <cue <parallel
            <effect target: "dot", kind: 'motion', path: [[0.0, 0.0, 100.0, 0.0]], duration: 200.0>
            <effect target: "dot", kind: 'highlight', color: "#ff0000", duration: 200.0>>>
    >
    <slide id: "second", background: "#000000", transition: 'morph', transition_duration: 300.0,
        <shape id: "dot2", morph_id: "dot", kind: 'circle', x: 200.0, y: 40.0,
            width: 80.0, height: 80.0, fill: "#ff0000">
        <text id: "paragraph2", x: 20.0, y: 130.0, width: 320.0, height: 60.0,
            font_size: 20.0, color: "#ffffff", "Second paragraph">
        <cue <effect target: "paragraph2", kind: 'wipe-in', from: 'left', duration: 150.0>>
    >
>;
<html <head <style html.css>> <body class: "slide-page",
    *[slide.player(deck, {instance: "first-player"})^,
      slide.player(deck, {instance: "second-player", reduced_motion: true})^]
>>
