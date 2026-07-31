import model: lambda.package.graph.model
import layout: lambda.package.graph.layout
import transform: lambda.package.graph.transform

let source^source_error = input(
  "test/lambda/graph/mermaid/class_diagram.mmd", {type: "graph", flavor: "mermaid"})
let html = transform.to_html(source)
let geometry = layout.from_velmts(html, model.element_children(html), null)

{
  html: [for (child in model.element_children(html) where model.tag(child) == "edge")
    [child["data-edge-id"], child["data-from"], child["data-to"],
      child["data-marker-start"], child["data-marker-end"]]],
  nodes: [for (node in geometry.nodes) node.id],
  layout: [for (edge in geometry.edges) [edge.from, edge.to]],
  placements: [for (place in geometry.placements) [place.index, place.x, place.y]]
}
