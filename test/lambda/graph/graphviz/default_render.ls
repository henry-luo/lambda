import layout: lambda.graph.layout
import model: lambda.graph.model
import normalize: lambda.graph.normalize
import transform: lambda.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn placement(result, index) {
  let matches = [for (entry in result.placements where entry.index == index) entry];
  if (len(matches) > 0) matches[0] else null
}

let source = (input("test/lambda/graph/graphviz/default_render.dot",
  {type: "graph", flavor: "dot"})) ^ { null }
let normalized = normalize.normalize(source)
let html = transform.to_html(normalized.graph)
let label = children(html, "edge-label")[0]
let measured = layout.from_velmts(
  {attrs: {'data-direction': "LR"}},
  [
    {tag: "node", index: 0, width: 60, height: 40,
      attrs: {'data-node-id': "a"}},
    {tag: "node", index: 1, width: 60, height: 40,
      attrs: {'data-node-id': "b"}},
    {tag: "edge-label", index: 2, width: 30, height: 20,
      attrs: {'data-edge-id': "ab", 'data-label-placement': label["data-label-placement"]}},
    {tag: "edge", index: 3, width: 0, height: 0,
      attrs: {'data-edge-id': "ab", 'data-from': "a", 'data-to': "b"}}
  ], null)
let label_placement = placement(measured, 2)

{
  valid: normalized.valid,
  route: [normalized.graph["route-mode"], html["data-route-mode"],
    html["data-use-splines"]],
  label: [label["data-label-placement"],
    label_placement.y + 10 < measured.edges[0].points[0].y]
}
