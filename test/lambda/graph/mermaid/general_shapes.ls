import model: lambda.graph.model
import transform: lambda.graph.transform

let graph = (input("test/lambda/graph/mermaid/general_shapes.mmd",
  {type: "graph", flavor: "mermaid"})) ^ { null }
let html = transform.to_html(graph)
let nodes = [for (child in model.element_children(html)
  where model.tag(child) == "node") child]

{
  count: len(nodes),
  shapes: [for (node in nodes) node["data-shape"]],
  styled: all([for (node in nodes) len(string(node.style)) > 0])
}
