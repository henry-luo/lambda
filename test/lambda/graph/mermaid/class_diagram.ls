import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn class_members(node) => [
  for (child in model.element_children(node)
    where model.tag(child) == "class-member") child
]

let source^source_error = input(
  "test/lambda/graph/mermaid/class_diagram.mmd", {type: "graph", flavor: "mermaid"})
let source_nodes = model.nodes(source)
let source_edges = model.edges(source)
let normalized = normalize.normalize(source)
let html = transform.to_html(source)
let html_nodes = children(html, "node")
let installed = transform.install()
let rendered = transform.render_scene(source, 800, 600)
let scene_nodes = children(rendered.scene, "node")
let scene_edges = children(rendered.scene, "edge")

{
  source: [source.kind, source.direction, len(source_nodes), len(source_edges)],
  members: [for (node in source_nodes) [node.id,
    [for (member in class_members(node)) [member.kind, member.visibility, member.value]]]],
  relations: [for (edge in source_edges) [edge.from, edge.to, edge.relation,
    edge["arrow-tail"], edge["arrow-head"], edge.style, edge.label,
    edge["from-cardinality"], edge["to-cardinality"]]],
  canonical: [normalized.valid,
    [for (node in model.nodes(normalized.graph))
      [node.id, model.tag(model.content_element(node))]]],
  html: [for (node in html_nodes)
    [node["data-node-id"], node["data-label"], model.tag(node[0])]],
  render: [installed, len(scene_nodes), len(scene_edges),
    all([for (node in scene_nodes) node.width > 0 and node.height > 0]),
    [for (edge in scene_edges) [edge.from, edge.to,
      edge["marker-start"], edge["marker-end"]]],
    contains(rendered.svg, "move(distance) void")]
}
