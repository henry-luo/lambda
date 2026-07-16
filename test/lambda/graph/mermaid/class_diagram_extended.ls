import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn members(node) => [
  for (child in model.element_children(node)
    where model.tag(child) == "class-member") child
]

let source^source_error = input(
  "test/lambda/graph/mermaid/class_diagram_extended.mmd",
  {type: "graph", flavor: "mermaid"})
let nodes = model.nodes(source)
let edges = model.edges(source)
let normalized = normalize.normalize(source)
let html = transform.to_html(source)
let installed = transform.install()
let rendered = transform.render_scene(source, 1000, 800)

{
  source: [source.kind, source.direction, len(nodes), len(edges),
    len(model.subgraphs(source)), len(model.annotations(source)),
    len(model.style_rules(source)), len(model.class_assignments(source)),
    len(model.style_assignments(source)), len(model.interactions(source))],
  nodes: [for (node in nodes) [node.id, node.label, node.generic,
    [for (member in members(node)) [member.kind, member.visibility,
      member.classifier, member.value, member.display]]]],
  edges: [for (edge in edges) [edge.from, edge.to, edge.relation,
    edge["arrow-tail"], edge["arrow-head"], edge.label]],
  namespaces: [for (group in model.subgraphs(source))
    [group.id, group.label, group.namespace]],
  annotations: [for (note in model.annotations(source))
    [note["owner-kind"], note["owner-id"], note.label]],
  styles: [
    [for (rule in model.style_rules(source))
      [rule.class, rule.declarations]],
    [for (assignment in model.class_assignments(source))
      [assignment.targets, assignment.class]],
    [for (assignment in model.style_assignments(source))
      [assignment.targets, assignment.declarations]],
    [for (interaction in model.interactions(source))
      [interaction.target, interaction.action, interaction.href, interaction.callback]]],
  canonical: [normalized.valid, len(model.nodes(normalized.graph)),
    [for (node in model.nodes(normalized.graph))
      [node.id, model.tag(model.content_element(node))]]],
  html: [for (node in model.nodes(html))
    [node["data-node-id"], model.tag(node[0])]],
  render: [installed, len(children(rendered.scene, "node")),
    len(children(rendered.scene, "edge")), len(children(rendered.scene, "annotation")),
    contains(rendered.svg, "Repository&lt;List&lt;Entity&gt;&gt;"),
    contains(rendered.svg, "Caches entities"), contains(rendered.svg, "Repository contract")]
}
