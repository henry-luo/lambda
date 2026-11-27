// ./lambda.exe view test/lambda/graph/test_graph.ls
import gallery: .mod_graph_gallery

gallery.page("Graphs", "Inspect the graph examples from test/input and the complete upstream rendering corpus. Choose a format to see every source rendered as a diagram.",
    <div class:"gallery",
        <h2 class:"section-heading","Examples from test/input">
        <a class:"section-card",href:"test_graph_input.ls",
            <h2 "20 diagrams · four formats">; <p "Mermaid, Graphviz DOT, D2, and TikZ / PGF examples from the shared input fixtures.">; <span "Explore all input examples ↗">>
        <h2 class:"section-heading","Upstream rendering corpus">
        <a class:"section-card",href:"test_graph_upstream_mermaid.ls",
            <h2 "Mermaid · 29 sources">; <p "Flowcharts, state diagrams, class diagrams, and subgraph edge cases.">; <span "Explore Mermaid corpus ↗">>
        <a class:"section-card",href:"test_graph_upstream_dot.ls",
            <h2 "Graphviz DOT · 30 sources">; <p "Canonical Graphviz examples, clusters, records, labels, and routing.">; <span "Explore DOT corpus ↗">>
        <a class:"section-card",href:"test_graph_upstream_d2.ls",
            <h2 "D2 · 16 sources">; <p "Shapes, strings, directions, connections, and styles.">; <span "Explore D2 corpus ↗">>
        <a class:"section-card",href:"test_graph_upstream_structurizr.ls",
            <h2 "Structurizr · 15 sources">; <p "System context, container, deployment, dynamic, and custom views.">; <span "Explore Structurizr corpus ↗">>
        <a class:"section-card",href:"test_graph_upstream_tikz.ls",
            <h2 "TikZ / PGF · 12 sources">; <p "Paths, coordinates, transforms, axes, and fill behavior.">; <span "Explore TikZ corpus ↗">>
    >)
