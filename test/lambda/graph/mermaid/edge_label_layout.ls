import layout: lambda.graph.layout
import model: lambda.graph.model
import transform: lambda.graph.transform

let children = [
    {tag: "node", index: 0, width: 80, height: 40,
      attrs: {'data-node-id': "a", 'data-shape': "box"}},
    {tag: "node", index: 1, width: 80, height: 40,
      attrs: {'data-node-id': "b", 'data-shape': "box"}},
    {tag: "edge-label", index: 2, width: 50, height: 20,
      attrs: {'data-edge-id': "ab", 'data-z': "1"}},
    {tag: "edge", index: 3, width: 0, height: 0,
      attrs: {'data-edge-id': "ab", 'data-from': "a", 'data-to': "b",
        'data-directed': "true", 'data-arrow-end': "true"}}
  ];
let result = layout.from_velmts(
  {attrs: {'data-direction': "TB", 'data-rank-sep': "70"}}, children, null)
let beside = layout.from_velmts(
  {attrs: {'data-direction': "TB", 'data-rank-sep': "70"}},
  [for (child in children) if (child.tag == "edge-label")
    {*:child,attrs:{*:child.attrs,'data-label-placement':"right"}} else child], null)
let beside_label = [for (placement in ((beside.placements) ^ { [] }) where placement.index == 2) placement][0]
let outgoing = input("test/lambda/graph/upstream/mermaid/nested-sg-outgoing-5.mmd",
  {type:"graph", flavor:"mermaid"})^
let outgoing_html = transform.to_html(outgoing)^;
let outgoing_children = [for (i, child in model.element_children(outgoing_html), let tag = string(model.tag(child)))
  {tag:tag, index:i, width:if (tag in ["node", "edge-label", "cluster-label"]) 100 else 0,
    height:if (tag == "node") 40 else if (tag in ["edge-label", "cluster-label"]) 20 else 0,
    attrs:map(child)}]
let outgoing_result = layout.from_velmts({attrs:map(outgoing_html)}, outgoing_children, null)
let edge_label_indexes = [for (child in outgoing_children where child.tag == "edge-label") child.index]
let label_boxes = [for (place in ((outgoing_result.placements) ^ { [] }) where place.index in edge_label_indexes)
  {left:place.x, right:place.x + 100, top:place.y, bottom:place.y + 20}]
let overlaps = [for (box in label_boxes, cluster in ((outgoing_result.clusters) ^ { [] }),
  let horizontal = box.left < cluster.x + cluster.width and box.right > cluster.x,
  let vertical = box.top < cluster.y + cluster.height and box.bottom > cluster.y
  where ((horizontal and ((box.top < cluster.y and box.bottom > cluster.y) or
      (box.top < cluster.y + cluster.height and box.bottom > cluster.y + cluster.height))) or
    (vertical and ((box.left < cluster.x and box.right > cluster.x) or
      (box.left < cluster.x + cluster.width and box.right > cluster.x + cluster.width))))) box]

{
  size: [result.width, result.height],
  placements: result.placements,
  edge_points: result.edges[0].points,
  right_label_clears_edge: beside_label.x > beside.edges[0].points[0].x,
  cluster_borders_clear: len(label_boxes) == 2 and len(overlaps) == 0
}
