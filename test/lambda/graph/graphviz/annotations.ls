import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn annotations(value, owner_kind, owner_id) => [for (entry in model.annotations(value)
  where entry["owner-kind"] == owner_kind and entry["owner-id"] == owner_id) [
  entry.kind, entry.label, entry["label-format"], entry["font-name"],
  entry["font-size"], entry["font-color"]
]]

fn placement(result, index) {
  let matches = [for (entry in result.placements where entry.index == index) entry];
  if (len(matches) > 0) { [matches[0].x, matches[0].y] } else null
}

fn placement_item(result, index) {
  let matches = [for (entry in result.placements where entry.index == index) entry];
  if (len(matches) > 0) matches[0] else null
}

fn box(x, y, width, height) =>
  {left: x, top: y, right: x + width, bottom: y + height}

fn overlap(a, b) =>
  a.left < b.right and a.right > b.left and a.top < b.bottom and a.bottom > b.top

fn label_box(result, spec) {
  let value = placement_item(result, spec.index);
  box(value.x, value.y, spec.width, spec.height)
}

fn segment_box(a, b) => box(min([a.x, b.x]) - 1.9, min([a.y, b.y]) - 1.9,
  abs(b.x - a.x) + 3.8, abs(b.y - a.y) + 3.8)

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
let label_specs = [
  {index: 2, width: 42, height: 16, owner: edge.id},
  {index: 4, width: 50, height: 16, owner: null},
  {index: 5, width: 48, height: 16, owner: edge.id},
  {index: 6, width: 32, height: 16, owner: edge.id},
  {index: 7, width: 32, height: 16, owner: edge.id}
]
let label_boxes = [for (spec in label_specs) label_box(measured, spec)]
let node_boxes = [for (entry in measured.nodes)
  box(entry.x - entry.width / 2.0, entry.y - entry.height / 2.0,
    entry.width, entry.height)]
let route_boxes = [for (routed in measured.edges, i in 1 to (len(routed.points) - 1))
  {owner: routed.id, box: segment_box(routed.points[i - 1], routed.points[i])}]

{
  valid: normalized.valid,
  canonical: [annotations(graph, "node", node.id), annotations(graph, "edge", edge.id)],
  html: [for (entry in children(html, "annotation")) [entry["data-owner-kind"],
    entry["data-owner-id"], entry["data-annotation-kind"], entry["data-label-format"],
    entry["data-font-name"], entry["data-font-size"], entry["data-font-color"],
    contains(string(entry.style), "font-family:" ++ string(entry["data-font-name"])),
    contains(string(entry.style), "font-size:" ++ string(entry["data-font-size"]) ++ "px"),
    contains(string(entry.style), "color:" ++ string(entry["data-font-color"]))]],
  placements: [for (index in 2 to 7) [index, placement(measured, index)]],
  size: [measured.width, measured.height],
  clear: [
    all([for (i, left in label_boxes, j, right in label_boxes where i < j)
      not overlap(left, right)]),
    all([for (label in label_boxes, node_box in node_boxes) not overlap(label, node_box)]),
    all([for (i, label in label_boxes, route in route_boxes
      where label_specs[i].owner == null or label_specs[i].owner != route.owner)
      not overlap(label, route.box)])
  ]
}
