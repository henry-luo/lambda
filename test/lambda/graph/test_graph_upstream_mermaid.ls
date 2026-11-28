// ./lambda.exe view test/lambda/graph/test_graph_upstream_mermaid.ls
import gallery: .mod_graph_gallery
gallery.page("Upstream · Mermaid", "All Mermaid sources in the pinned upstream graph corpus.",
    <div class:"gallery", gallery.upstream_cards("mermaid")>)
