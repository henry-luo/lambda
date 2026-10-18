// ./lambda.exe view test/demo/chart/graphs.ls
import chart: lambda.chart.chart
import timeline: lambda.chart.timeline
import graph: lambda.graph.transform
import tikz: lambda.doc.tikz.tikz
import gallery: .mod_gallery
import catalog: .mod_catalog

let history = {id:"lambda-history",view:"axis",width:1040,height:380,cards:{width:180},
    toolbar:{buttons:["first","previous","next","last","fit","zoom_in","zoom_out","reset","layout"]},
    window:{from:"2024",to:"2028"},
    events:[{id:"language",at:"2024-03-01",title:"Language",content:"Values, functions, and data."},
        {id:"documents",at:"2025-02-01",title:"Documents",content:"Read, transform, and publish."},
        {id:"charts",at:"2026-01-01",title:"Charts",content:"Patterns become pictures."},
        {id:"interaction",at:"2027-03-01",title:"Interaction",content:"Select, explore, and animate."}]};
fn diagram(source, flavor) => graph.to_html(parse(source,{type:"graph",flavor:flavor})^,{theme:"light",node_sep:24,rank_sep:28});
let flow = "flowchart LR\n A[Read data] --> B{Validate}\n B -->|Valid| C[Transform]\n B -->|Retry| A\n C --> D[Render chart]";
let sequence = "sequenceDiagram\n participant U as Reader\n participant C as Chart\n participant D as Data\n U->>C: Select a mark\n C->>D: Filter records\n D-->>C: Matching rows\n C-->>U: Update the view";
let dot = "digraph Pipeline { rankdir=LR; node [shape=box]; Source -> Parse; Parse -> Layout; Layout -> SVG; Layout -> PDF; Layout -> PNG; }";
let picture = "\\begin{tikzpicture}\n" ++
    "\\begin{axis}[width=11cm,height=6cm,xlabel={Time},ylabel={Response},grid=major]\n" ++
    "\\addplot[blue,thick,mark=*] coordinates {(0,0) (1,1) (2,1.7) (3,2.1) (4,2.3)};\n" ++
    "\\addlegendentry{Measured}\n" ++
    "\\addplot[red,thick] coordinates {(0,0) (1,0.8) (2,1.6) (3,2.4) (4,3.2)};\n" ++
    "\\addlegendentry{Reference}\n\\end{axis}\n\\end{tikzpicture}";
let examples = catalog.charts |: catalog.graph(~);

gallery.page("Graphs & diagrams","Words, events, relationships, and authored diagrams. Explore an interactive timeline alongside Mermaid, Graphviz, TikZ, and the dashboard's graph families.",
    <div
        gallery.panel("timeline","Timeline","An illustrative project timeline. Click events, use the toolbar, or switch between axis and story views.",timeline.interactive(history,{width:1040,height:380}));
        <div class:"gallery graph-gallery",
        gallery.panel("mermaid-flow","Mermaid flowchart","A decision and a feedback loop, laid out from Mermaid source.",diagram(flow,"mermaid"));
        gallery.panel("mermaid-sequence","Mermaid sequence diagram","Messages between a reader, a chart, and its data.",diagram(sequence,"mermaid"));
        gallery.panel("tikz","TikZ / PGFPlots","Measured and reference series, rendered from a TikZ axis specification.",tikz.render(picture)^);
        gallery.panel("graphviz","Graphviz dependency graph","One layout feeds several document outputs.",diagram(dot,"dot"));
        for (i,entry in examples) gallery.panel("graph-" ++ string(i),entry.label,
            "Hover or focus to inspect; click to select; double-click to clear.",chart.interactive(catalog.live(catalog.spec(entry))))
    >>)
