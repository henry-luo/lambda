import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

let graph^err = input("test/lambda/graph/mermaid/general_shapes.mmd",
  {type: "graph", flavor: "mermaid"})
let html = transform.to_html(graph)
let nodes = [for (child in model.element_children(html)
  where model.tag(child) == "node") child]

{
  count: len(nodes),
  shapes: [for (node in nodes) node["data-shape"]],
  styled: all([for (node in nodes) len(string(node.style)) > 0])
}
