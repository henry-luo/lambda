import slide: lambda.slide
let deck = <presentation width: 300.0, height: 180.0,
    <slide <text id: "title", "Autostart">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 100.0>>>>
slide.page(deck, {instance: "auto", autostart: true})^
