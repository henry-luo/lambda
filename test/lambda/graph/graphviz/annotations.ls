import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn annotations(value, owner_kind, owner_id) => [for (entry in model.annotations(value)
  where entry["owner-kind"] == owner_kind and entry["owner-id"] == owner_id) [
  entry.kind, entry.label, entry["label-format"]
]]

fn placement(result, index) {
  let matches = [for (entry in result.placements where entry.index == index) entry];
  if (len(matches) > 0) { [matches[0].x, matches[0].y] } else null
}

let source^source_error = input(
  "test/lambda/graph/graphviz/annotations.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let node = model.nodes(graph)[0]
let edge = model.edges(graph)[0]
let html = transform.to_html(graph)
let measured = layout.from_velmts(
  {attrs: {'data-direction': "TB", 'data-rank-sep': "70"}},
  [
    {tag: "node", index: 0, width: 80, height: 40,
      attrs: {'data-node-id': "a", 'data-shape': "box"}},
    {tag: "node", index: 1, width: 80, height: 40,
      attrs: {'data-node-id': "b", 'data-shape': "box"}},
    {tag: "edge-label", index: 2, width: 42, height: 16,
      attrs: {'data-edge-id': edge.id}},
    {tag: "edge", index: 3, width: 0, height: 0,
      attrs: {'data-edge-id': edge.id, 'data-from': "a", 'data-to': "b"}},
    {tag: "annotation", index: 4, width: 50, height: 16,
      attrs: {'data-owner-kind': "node", 'data-owner-id': "a",
        'data-annotation-kind': "external"}},
    {tag: "annotation", index: 5, width: 48, height: 16,
      attrs: {'data-owner-kind': "edge", 'data-owner-id': edge.id,
        'data-annotation-kind': "external"}},
    {tag: "annotation", index: 6, width: 32, height: 16,
      attrs: {'data-owner-kind': "edge", 'data-owner-id': edge.id,
        'data-annotation-kind': "head"}},
    {tag: "annotation", index: 7, width: 32, height: 16,
      attrs: {'data-owner-kind': "edge", 'data-owner-id': edge.id,
        'data-annotation-kind': "tail"}}
  ], null)

{
  valid: normalized.valid,
  canonical: [annotations(graph, "node", node.id), annotations(graph, "edge", edge.id)],
  html: [for (entry in children(html, "annotation")) [entry["data-owner-kind"],
    entry["data-owner-id"], entry["data-annotation-kind"], entry["data-label-format"]]],
  placements: [for (index in 2 to 7) [index, placement(measured, index)]],
  size: [measured.width, measured.height]
}
