// A raw CSS paint string reaches the live host validation boundary.
import slide: lambda.slide
let deck = <presentation width: 400.0, height: 240.0,
    <slide <text id: "bad", x: 20.0, y: 20.0, width: 300.0, height: 100.0,
        color: "not-a-color", "Host rejects this paint">>>
slide.page(deck, {instance: "rejection", width: 400.0, height: 240.0})^
