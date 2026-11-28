import layout: lambda.graph.layout
import model: lambda.graph.model

let source = (input("test/lambda/graph/mermaid/parallel_edges.mmd",
  {type: "graph", flavor: "mermaid"})) ^ { null }

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

// Parallel markers must approach the rank-facing boundary at distinct points.
fn parallel_terminal_check(direction, shape) any^ {
  let result = layout.compute({nodes:[{id:"a",width:100,height:100,shape:shape},
    {id:"b",width:100,height:100,shape:shape}],edges:[
    {id:"a0",from:"a",to:"b",stroke_width:2,arrow_size:2},
    {id:"a1",from:"a",to:"b",stroke_width:2,arrow_size:2}],direction:direction});
  let first = result.edges[0].points;
  let second = result.edges[1].points;
  let target = [for (node in ((result.nodes) ^ { [] }) where node.id == "b") node][0];
  let a = first[len(first)-1];
  let b = second[len(second)-1];
  let vertical = direction in ["TB", "BT"];
  let before_a = first[len(first)-2];
  let before_b = second[len(second)-2];
  let a_dx = (a.x - target.x) / (target.width / 2.0);
  let a_dy = (a.y - target.y) / (target.height / 2.0);
  let boundary = if (shape == "ellipse") abs(a_dx * a_dx + a_dy * a_dy - 1.0) < 0.001
    else if (shape == "diamond") abs(abs(a_dx) + abs(a_dy) - 1.0) < 0.001
    else if (vertical) abs(abs(a_dy) - 1.0) < 0.001 else abs(abs(a_dx) - 1.0) < 0.001;
  boundary and len(first) == 2 and len(second) == 2 and (if (vertical)
    abs(a.x - b.x) >= 36.0 and abs(a.x - before_a.x) < 0.001 and abs(b.x - before_b.x) < 0.001
    else abs(a.y - b.y) >= 36.0 and abs(a.y - before_a.y) < 0.001 and abs(b.y - before_b.y) < 0.001)
}

{
  routed_size: [routed.width, routed.height],
  routed_nodes: [for (node in ((routed.nodes) ^ { [] })) [node.id, node.x, node.y]],
  routed_edges: [for (edge in ((routed.edges) ^ { [] })) [edge.id, edge.points]],
  labeled_edges: [for (edge in ((labeled.edges) ^ { [] })) [edge.id, edge.points]],
  label_placements: [for (place in ((labeled.placements) ^ { [] })
    where place.index == 2 or place.index == 3) place],
  terminal_shapes: [for (direction in ["TB", "BT", "LR", "RL"],
    shape in ["box", "ellipse", "diamond"]) parallel_terminal_check(direction, shape)]
}
