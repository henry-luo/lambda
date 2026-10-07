import slide: lambda.slide
let deck = <presentation width: 400.0, height: 240.0,
    <slide <content id: "control", x: 20.0, y: 20.0, width: 300.0, height: 100.0,
        <button type: "button", ["data-slide-command"]: "invalid", "Invalid command">>>>
slide.page(deck, {instance: "failure", width: 400.0, height: 240.0})^
