import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import paint: lambda.package.graph.transform.paint
import transform: lambda.package.graph.transform

let source^err = input("test/lambda/graph/mermaid/edge_ids_markers.mmd",
  {type: "graph", flavor: "mermaid"})
let source_edges = model.edges(source)
let html = transform.to_html(source)
let html_edges = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "edge") child]
let result = layout.compute({
  nodes: [for (node in model.nodes(source)) {
    id: string(node.id), width: 40.0, height: 20.0
  }],
  edges: source_edges,
  directed: true,
  direction: "LR"
})
let layers = paint.layers(result)

{
  source: [for (edge in source_edges)
    [edge.id, edge["arrow-tail"], edge["arrow-head"]]],
  html: [for (edge in html_edges)
    [edge["data-edge-id"], edge["data-marker-start"], edge["data-marker-end"]]],
  layout: [for (edge in result.edges)
    [edge.id, edge.marker_start, edge.marker_end]],
  paint: [for (layer in layers,
    let svg = layer.content,
    let defs = svg[0],
    let path = svg[len(svg) - 1]) [
      path["data-edge-id"], path["data-graph-role"],
      path["data-marker-start"], path["data-marker-end"],
      defs[0]["data-marker-type"], defs[1]["data-marker-type"],
      string(name(defs[0][0])), string(name(defs[1][0]))
    ]]
}
