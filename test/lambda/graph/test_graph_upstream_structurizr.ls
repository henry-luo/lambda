// ./lambda.exe view test/lambda/graph/test_graph_upstream_structurizr.ls
import gallery: .mod_graph_gallery
gallery.page("Upstream · Structurizr", "Every declared view from the Structurizr fixtures in the pinned upstream corpus.",
    <div class:"gallery", gallery.upstream_cards("structurizr")>)
