// ./lambda.exe view test/lambda/graph/test_graph_upstream_dot.ls
import gallery: .mod_graph_gallery
gallery.page("Upstream · Graphviz DOT", "All Graphviz DOT sources in the pinned upstream graph corpus.",
    <div class:"gallery", gallery.upstream_cards("dot")>)
