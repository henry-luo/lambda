import layout: lambda.package.graph.layout

fn by_id(items, id) {
  let matches = [for (item in items where item.id == id) item];
  if (len(matches) > 0) matches[0] else null
}

fn placement_by_index(placements, index) {
  let matches = [for (placement in placements where placement.index == index) placement];
  if (len(matches) > 0) matches[0] else null
}

fn separated(a, a_width, a_height, b, b_width, b_height) =>
  a.x + a_width <= b.x or b.x + b_width <= a.x or
    a.y + a_height <= b.y or b.y + b_height <= a.y

let grouped = layout.compute({
  nodes: [
    {id: "inside-a", width: 40, height: 30, group: "cluster"},
    {id: "outside", width: 40, height: 30},
    {id: "inside-b", width: 40, height: 30, group: "cluster"}
  ],
  edges: [],
  clusters: [{id: "cluster", padding: 16}],
  direction: "TB"
})

let routed = layout.compute({
  nodes: [
    {id: "a", width: 40, height: 30},
    {id: "blocker", width: 80, height: 50},
    {id: "c", width: 40, height: 30}
  ],
  edges: [
    {id: "ab", from: "a", to: "blocker"},
    {id: "ac", from: "a", to: "c", min_length: 2}
  ],
  direction: "TB"
})
let ac = by_id(routed.edges, "ac")

let labels = layout.from_velmts(
  {attrs: {'data-direction': "LR", 'data-edge-sep': "12"}},
  [
    {tag: "node", index: 0, width: 60, height: 40,
      attrs: {'data-node-id': "a"}},
    {tag: "node", index: 1, width: 60, height: 40,
      attrs: {'data-node-id': "b"}},
    {tag: "edge-label", index: 2, width: 70, height: 20,
      attrs: {'data-edge-id': "e1"}},
    {tag: "edge-label", index: 3, width: 70, height: 20,
      attrs: {'data-edge-id': "e2"}},
    {tag: "edge", index: 4, width: 0, height: 0,
      attrs: {'data-edge-id': "e1", 'data-from': "a", 'data-to': "b"}},
    {tag: "edge", index: 5, width: 0, height: 0,
      attrs: {'data-edge-id': "e2", 'data-from': "a", 'data-to': "b"}}
  ], null)
let label1 = placement_by_index(labels.placements, 2)
let label2 = placement_by_index(labels.placements, 3)

{
  grouped: [for (node in grouped.layers[0].nodes) node.id],
  cluster_separated: grouped.clusters[0].x + grouped.clusters[0].width <=
    by_id(grouped.nodes, "outside").x - by_id(grouped.nodes, "outside").width / 2.0,
  obstacle_detour: len(ac.points) >= 4,
  label_positions: [[label1.x, label1.y], [label2.x, label2.y]],
  labels_separated: separated(label1, 70, 20, label2, 70, 20),
  labels_contained: label1.x >= 0 and label1.y >= 0 and label2.x >= 0 and label2.y >= 0 and
    label1.x + 70 <= labels.width and label2.x + 70 <= labels.width and
    label1.y + 20 <= labels.height and label2.y + 20 <= labels.height
}
