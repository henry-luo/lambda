// ./lambda.exe view test/lambda/graph/test_graph_upstream_tikz.ls
import gallery: .mod_graph_gallery
gallery.page("Upstream · TikZ / PGF", "All standalone PGF sources in the pinned upstream graph corpus.",
    <div class:"gallery", gallery.upstream_cards("tikz")>)
