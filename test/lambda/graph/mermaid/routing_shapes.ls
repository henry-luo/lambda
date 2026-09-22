import layout: lambda.graph.layout

fn by_id(items, id) {
  let matches = [for (item in items where item.id == id) item];
  if (len(matches) > 0) matches[0] else null
}

fn terminal_follows_rank_axis(edge, node, vertical_rank_axis) {
  let finish = edge.points[len(edge.points) - 1];
  let before = edge.points[len(edge.points) - 2];
  if (vertical_rank_axis)
    abs(finish.x - node.x) < 0.001 and
      abs(finish.y - (node.y - node.height / 2.0)) < 0.001 and
      abs(before.x - finish.x) < 0.001
  else
    abs(finish.x - (node.x - node.width / 2.0)) < 0.001 and
      abs(finish.y - node.y) < 0.001 and
      abs(before.y - finish.y) < 0.001
}

let result = layout.compute({
  nodes: [
    {id: "a", width: 80, height: 50, shape: "diamond"},
    {id: "b", width: 60, height: 60, shape: "circle"}
  ],
  edges: [
    {id: "ab", from: "a", to: "b"},
    {id: "loop", from: "b", to: "b"}
  ]
}, {direction: "LR", rank_sep: 90, edge_sep: 12})
let branches = {
  nodes: [
    {id: "b", width: 60, height: 40},
    {id: "c", width: 80, height: 40},
    {id: "d", width: 80, height: 40}
  ],
  edges: [{id: "bc", from: "b", to: "c"}, {id: "bd", from: "b", to: "d"}]
}
let top_down = layout.compute(branches, {direction: "TB"})
let left_right = layout.compute(branches, {direction: "LR"})
let top_down_c = by_id(top_down.nodes, "c")
let left_right_c = by_id(left_right.nodes, "c")
let top_down_edge = by_id(top_down.edges, "bc")
let left_right_edge = by_id(left_right.edges, "bc")

{
  size: [result.width, result.height],
  edge: result.edges[0].points,
  loop: result.edges[1].points,
  terminal_direction: [
    terminal_follows_rank_axis(top_down_edge, top_down_c, true),
    terminal_follows_rank_axis(left_right_edge, left_right_c, false)
  ]
}
