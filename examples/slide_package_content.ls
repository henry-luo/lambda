// ./lambda.exe view examples/slide_package_content.ls
import slide: lambda.slide
import chart: lambda.chart.chart

let graphic = chart.render(<chart width: 560, height: 300, title: "Customer mix",
    <data values: [{category: "A", amount: 28}, {category: "B", amount: 55}, {category: "C", amount: 43}]>
    <mark kind: 'bar'>
    <encoding <x field: "category", type: 'nominal'> <y field: "amount", type: 'quantitative'>>>)
let builds = slide.paragraphs("points", ["Lambda authors the deck.", "Radiant lays out and paints it.",
    "Existing packages supply rich content."], {width: 420.0, line_height: 90.0, x: 60.0, y: 160.0})^;
slide.page(<presentation title: "Package content", width: 1280.0, height: 720.0,
    <slide layout: 'title-body', *[<text id: "title", role: 'title', "Reusable Lambda content">,
        *builds,
        <content id: "chart", x: 620.0, y: 200.0, width: 560.0, height: 300.0, graphic>,
        <cue <effect target: "chart", kind: 'fade-in', duration: 400.0>>,
        <notes "Each paragraph is an explicit text target. The chart is computed once.">
        ]
    >
>, {instance: "content-demo"})^
