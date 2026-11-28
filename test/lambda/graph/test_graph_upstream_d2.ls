// ./lambda.exe view test/lambda/graph/test_graph_upstream_d2.ls
import gallery: .mod_graph_gallery
gallery.page("Upstream · D2", "All D2 sources in the pinned upstream graph corpus.",
    <div class:"gallery", gallery.upstream_cards("d2")>)
