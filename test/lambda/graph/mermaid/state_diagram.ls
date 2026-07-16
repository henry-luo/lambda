import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn descriptions(node) => [
  for (child in model.element_children(node)
    where model.tag(child) == "state-description") child
]

fn by_id(nodes, id) => [for (node in nodes where node.id == id) node][0]
fn annotation_for(values, id) => [
  for (value in values where value["owner-id"] == id) value
][0]

let source^source_error = input(
  "test/lambda/graph/mermaid/state_diagram.mmd", {type: "graph", flavor: "mermaid"})
let source_nodes = model.nodes(source)
let source_edges = model.edges(source)
let source_annotations = model.annotations(source)
let normalized = normalize.normalize(source)
let html = transform.to_html(source)
let html_nodes = children(html, "node")
let html_annotations = children(html, "annotation")
let installed = transform.install()
let rendered = transform.render_scene(source, 900, 650)
let scene_nodes = children(rendered.scene, "node")
let scene_edges = children(rendered.scene, "edge")
let scene_annotations = children(rendered.scene, "annotation")
let idle = by_id(scene_nodes, "Idle")
let processing = by_id(scene_nodes, "Processing")
let idle_note = annotation_for(scene_annotations, "Idle")
let processing_note = annotation_for(scene_annotations, "Processing")

{
  source: [source.kind, source.direction, len(source_nodes), len(source_edges),
    len(source_annotations)],
  states: [for (node in source_nodes) [node.id, node.label, node.shape,
    node["state-kind"], [for (value in descriptions(node)) value.value]]],
  transitions: [for (edge in source_edges)
    [edge.from, edge.to, edge.label, edge["arrow-head"]]],
  notes: [for (note in source_annotations)
    [note["owner-id"], note.kind, note.label]],
  canonical: [normalized.valid, len(model.nodes(normalized.graph)),
    [for (node in model.nodes(normalized.graph))
      [node.id, model.tag(model.content_element(node))]]],
  html: [len(html_nodes), len(html_annotations),
    [for (node in html_nodes) [node["data-node-id"], model.tag(node[0])]]],
  render: [installed, len(scene_nodes), len(scene_edges), len(scene_annotations),
    all([for (node in scene_nodes) node.width > 0 and node.height > 0]),
    idle_note.x < idle.x, processing_note.x > processing.x,
    contains(rendered.svg, "Waiting for work"),
    contains(rendered.svg, "External service call")]
}
