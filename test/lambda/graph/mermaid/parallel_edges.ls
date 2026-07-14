import layout: lambda.package.graph.layout
import model: lambda.package.graph.model

let source^err = input("test/lambda/graph/mermaid/parallel_edges.mmd",
  {type: "graph", flavor: "mermaid"})

let routed = layout.compute({
  nodes: [
    {id: "A", width: 40, height: 30},
    {id: "B", width: 40, height: 30}
  ],
  edges: model.edges(source)
}, {direction: "TB", rank_sep: 80, edge_sep: 50})

let labeled = layout.from_velmts(
  {attrs: {'data-direction': "LR", 'data-rank-sep': "80", 'data-edge-sep': "20"}},
  [
    {tag: "node", index: 0, width: 60, height: 30,
      attrs: {'data-node-id': "a", 'data-shape': "box"}},
    {tag: "node", index: 1, width: 60, height: 30,
      attrs: {'data-node-id': "b", 'data-shape': "box"}},
    {tag: "edge-label", index: 2, width: 20, height: 10,
      attrs: {'data-edge-id': "e0"}},
    {tag: "edge-label", index: 3, width: 20, height: 10,
      attrs: {'data-edge-id': "e1"}},
    {tag: "edge", index: 4, width: 0, height: 0,
      attrs: {'data-edge-id': "e0", 'data-from': "a", 'data-to': "b"}},
    {tag: "edge", index: 5, width: 0, height: 0,
      attrs: {'data-edge-id': "e1", 'data-from': "a", 'data-to': "b"}}
  ], null)

{
  routed_size: [routed.width, routed.height],
  routed_nodes: [for (node in routed.nodes) [node.id, node.x, node.y]],
  routed_edges: [for (edge in routed.edges) [edge.id, edge.points]],
  labeled_edges: [for (edge in labeled.edges) [edge.id, edge.points]],
  label_placements: [for (place in labeled.placements
    where place.index == 2 or place.index == 3) place]
}
