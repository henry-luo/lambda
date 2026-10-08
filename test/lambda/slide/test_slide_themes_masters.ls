import slide: lambda.slide
import theme: lambda.slide.theme
import html: lambda.slide.html

let deck = <presentation theme: {base: 'midnight', accent: "#ffaa00", title_size: 48.0}, master: "brand",
    <layout id: "sidebar", <placeholder role: 'title', x: 40.0, y: 30.0, width: 800.0, height: 100.0>
        <placeholder role: 'sidebar', x: 900.0, y: 180.0, width: 300.0, height: 420.0>>
    <master id: "base", layout: 'title-body', <text id: "footer", role: 'footer', "Example Corp">
        <shape id: "rule", x: 40.0, y: 680.0, width: 1200.0, height: 2.0>>
    <master id: "brand", extends: "base", <text id: "title", role: 'title', "Default title">>
    <slide id: "one", title: "Opening", <text id: "title", role: 'title', "A new title">
        <cue <effect target: "title", kind: 'fade-in', duration: 100.0>>>
    <slide id: "two", layout: "sidebar", theme: 'paper',
        <text id: "title", role: 'title', "A paper slide">
        <text id: "side", role: 'sidebar', x: 920.0, "Side content">>
    <slide id: "three", master: false, <text id: "only", "No master">>
>
let plan = slide.compile(deck)^
let first = plan.slides[0]
let second = plan.slides[1]
let image = format(slide.snapshot(deck, {slide: 1, cue: 'final'})^, 'html')
let morphed = slide.compile(<presentation
    <slide theme: 'paper', <text id: "a", morph_id: "heading", role: 'title', "Same text">>
    <slide theme: 'dark', transition: 'morph', <text id: "b", morph_id: "heading", role: 'title', "Same text">>
>)^
fn problem(deck) => slide.compile(deck) ^ { ^.message };
[
    len(plan.slides) == 3,
    len(first.targets) == 3,
    first.targets[0].id == "footer" and first.targets[2].id == "title",
    content(first.targets[2].source)[0] == "A new title",
    first.targets[1].paint == "#ffaa00",
    first.palette.title_size == 48.0,
    first.cues[0].tracks[0].target == "title",
    second.palette.background == "#faf7ef",
    second.targets[2].x == 40.0 and second.targets[2].height == 100.0,
    second.targets[3].x == 920.0 and second.targets[3].width == 300.0,
    len(plan.slides[2].targets) == 1,
    contains(image, "Georgia, serif") and contains(image, "#faf7ef"),
    contains(image, "Example Corp") and contains(image, "A paper slide"),
    theme.bounds('three-column', 'center', 1280.0, 720.0, 2).x > theme.bounds('three-column', 'left', 1280.0, 720.0, 1).x,
    theme.resolve('corporate')^.accent == "#0057b8",
    contains(problem(<presentation master: "absent", <slide>>), "unknown definition absent"),
    contains(problem(<presentation <master id: "a", extends: "b"> <master id: "b", extends: "a"> <slide>>), "inheritance cycle"),
    contains(problem(<presentation <master id: "a"> <master id: "a"> <slide>>), "duplicate ID a"),
    contains(problem(<presentation <master id: "a", <cue>> <slide>>), "master accepts only slide objects"),
    contains(problem(<presentation <layout id: "x", <placeholder role: 'body'> <placeholder role: 'body'>> <slide>>), "duplicate ID body"),
    contains(problem(<presentation <layout id: "blank", <placeholder role: 'body'>> <slide>>), "shadows a built-in"),
    contains(problem(<presentation theme: {base: 'light', body_size: 0.0}, <slide>>), "invalid numeric body_size"),
    contains(problem(<presentation theme: {base: 'light', typo: 1}, <slide>>), "unsupported attribute typo"),
    contains(problem(<presentation <slide theme: 'unknown'>>), "unsupported theme"),
    contains(problem(<presentation <slide <text role: 'unknown'>>>), "invalid role"),
    first.targets[2].font_size == 48.0,
    not morphed.slides[1].morph.pairs[0].compatible,
    theme.bounds('image-left', 'body', 1280.0, 720.0, 1).x > theme.bounds('image-right', 'body', 1280.0, 720.0, 1).x,
    contains(problem(<presentation <master id: "unused", <text width: 0.0>> <slide>>), "master.unused.o0"),
    contains(problem(<presentation master: "base", <master id: "base", <text id: "title">>
        <slide <text id: "title"> <text id: "title">>>), "duplicate ID title"),
    theme.resolve("paper")^.background == "#faf7ef",
    second.targets[0].y == first.targets[0].y and second.targets[0].y > 600.0
]
