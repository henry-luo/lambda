// ./lambda.exe view test/lambda/graph/test_graph_input.ls
import gallery: .mod_graph_gallery

let mermaid = ["test/input/architecture.mmd", "test/input/sequence_diagram.mmd",
    "test/input/sequence_diagram2.mmd", "test/input/simple_flowchart.mmd",
    "test/input/test_graph.mmd", "test/input/workflow.mmd"];
let dot = ["test/input/fsm.dot", "test/input/go-package.dot", "test/input/test_graph.dot"];
let d2 = ["test/input/seq_diagram.d2", "test/input/simple_diagram.d2", "test/input/system_design.d2"];
let tikz = ["test/input/tikz/diagrama_venn1.pgf", "test/input/tikz/diagrama_venn2.pgf",
    "test/input/tikz/plot_logaxis.pgf", "test/input/tikz/plot_parametric.pgf",
    "test/input/tikz/polar.pgf", "test/input/tikz/reg_components.pgf",
    "test/input/tikz/segmentada.pgf", "test/input/tikz/serie_coordinate.pgf"];

gallery.page("Input graph examples", "Twenty diagrams from test/input, grouped by source format.",
    <div class:"gallery",
        <h2 class:"section-heading","Mermaid · 6">
        gallery.input_cards(mermaid, "mermaid");
        <h2 class:"section-heading","Graphviz DOT · 3">
        gallery.input_cards(dot, "dot");
        <h2 class:"section-heading","D2 · 3">
        gallery.input_cards(d2, "d2");
        <h2 class:"section-heading","TikZ / PGF · 8">
        gallery.input_cards(tikz, "tikz")
    >)
