import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr
import expression: lambda.package.graph.structurizr.expressions

fn direct(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn ids(values) => [for (value in values) string(value.id)]

fn edge_pairs(graph) => [
  for (edge in model.edges(graph)) [string(edge.from), string(edge.to)]
]

fn tags(value) => [for (tag in direct(value, "tag")) string(tag.name)]

let source^err = input("test/lambda/graph/structurizr/stage4c.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let c4_model = direct(workspace, "c4-model")[0]
let all_elements = direct(c4_model, "c4-element")
let all_relationships = direct(c4_model, "c4-relationship")
let custom_entries = [for (entry in direct(c4_model, "c4-element")
  where entry.kind == "custom") entry]
let critical = [for (relation in direct(c4_model, "c4-relationship")
  where contains(tags(relation), "Critical")) relation][0]
let context_all = structurizr.project(workspace, "ContextAll")
let context_reluctant = structurizr.project(workspace, "ContextReluctant")
let components = structurizr.project(workspace, "Components")
let coupled = structurizr.project(workspace, "Coupled")
let custom = structurizr.project(workspace, "Integration")
let core = structurizr.project(workspace, "CoreOnly")
let external = structurizr.project(workspace, "NoExternal")
let html = structurizr.to_html(workspace, "Integration")
let coupled_html = structurizr.to_html(workspace, "Coupled")
let html_nodes = direct(coupled_html, "node")
let html_edges = direct(coupled_html, "edge")

{
  keys: structurizr.view_keys(workspace),
  canonical: [
    [for (entry in custom_entries) [entry.id, entry.name, entry.metadata, tags(entry)]],
    [critical.source, critical.destination, tags(critical)]
  ],
  wildcard: [edge_pairs(context_all), edge_pairs(context_reluctant)],
  expressions: [
    expression.element_ids(all_elements, all_relationships, "element.type==SoftwareSystem"),
    expression.element_ids(all_elements, all_relationships, "element.technology==Lambda"),
    expression.element_ids(all_elements, all_relationships,
      "element.tag==API || element.tag==Internal"),
    expression.element_ids(all_elements, all_relationships, "element.tag!=Hidden"),
    [expression.relationship_matches(all_elements, critical, "relationship.tag==Critical"),
      expression.relationship_matches(all_elements, critical,
        "relationship.source==store.web.api"),
      expression.relationship_matches(all_elements, critical,
        "relationship==store.web.api->store.web.repo"),
      expression.relationship_matches(all_elements, critical,
        "relationship.tag!=Critical")]
  ],
  components: [ids(model.nodes(components)), edge_pairs(components)],
  coupled: [ids(model.nodes(coupled)), edge_pairs(coupled)],
  custom: [
    [for (node in model.nodes(custom)) [node.id, node.label, node.metadata, tags(node)]],
    edge_pairs(custom)
  ],
  filtered: [
    [core["base-view-key"], core.direction, ids(model.nodes(core)), edge_pairs(core)],
    [external["base-view-key"], ids(model.nodes(external)), edge_pairs(external)]
  ],
  diagnostics: [for (value in model.diagnostics(workspace))
    [value.code, value.severity]],
  styles: [
    [for (assignment in model.style_assignments(coupled))
      [assignment["target-kind"], assignment.targets, assignment.declarations]],
    [for (node in html_nodes) [node["data-node-id"], node["data-style-declarations"]]],
    [for (edge in html_edges) [edge["data-edge-id"], edge["data-style-declarations"],
      edge["data-stroke"], edge["data-stroke-width"], edge["data-dash-array"]]]
  ],
  html: [string(name(html)), html.class, len(html)]
}
