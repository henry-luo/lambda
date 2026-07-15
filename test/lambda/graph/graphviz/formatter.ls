import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn property_pairs(value) => [
  for (group in children(value, "properties"), property in children(group, "property"))
    [property.name, property.value]
]

fn members(value) => [for (member in children(value, "member")) member.node]

fn signature(graph) => {
  graph: [graph.id, graph.directed, graph.strict, graph.direction,
    property_pairs(graph)],
  nodes: [for (node in model.nodes(graph)) [node.id, model.label_source(node),
    node.shape, node.stroke, property_pairs(node)]],
  edges: [for (edge in model.edges(graph)) [edge.from, edge.to,
    model.label_source(edge), edge.stroke, edge.weight, edge["from-port"],
    edge["to-port"], edge["from-compass"], edge["to-compass"],
    property_pairs(edge)]],
  groups: [for (group in model.subgraphs(graph)) [group.id, group.role,
    members(group), property_pairs(group)]]
}

let source^source_error = input("test/lambda/graph/graphviz/canonical.dot",
  {type: "graph", flavor: "dot"})
let canonical = normalize.normalize(source).graph
let source_text = format(source, {type: "graph", flavor: "dot"})
let canonical_text = format(canonical, {type: "graph", flavor: "dot"})
let source_round^source_round_error = parse(source_text,
  {type: "graph", flavor: "dot"})
let canonical_round^canonical_round_error = parse(canonical_text,
  {type: "graph", flavor: "dot"})
let source_normalized = normalize.normalize(source_round)
let canonical_normalized = normalize.normalize(canonical_round)
let canonical_signature = signature(canonical)
let round_signature = signature(canonical_normalized.graph)

let grammar^grammar_error = input("test/lambda/graph/graphviz/grammar.dot",
  {type: "graph", flavor: "dot"})
let grammar_text = format(grammar, {type: "graph", flavor: "dot"})
let grammar_round^grammar_round_error = parse(grammar_text,
  {type: "graph", flavor: "dot"})

{
  source: [source_normalized.valid,
    source_text == format(source_round, {type: "graph", flavor: "dot"}),
    signature(canonical) == signature(source_normalized.graph)],
  canonical: [canonical_normalized.valid,
    canonical_signature == round_signature],
  lexical: grammar_text == format(grammar_round, {type: "graph", flavor: "dot"})
}
