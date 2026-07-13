import graph: lambda.package.graph.graph

let result = graph.layout({
  nodes: [
    {id: "a", width: 80, height: 40},
    {id: "b", width: 100, height: 50},
    {id: "c", width: 60, height: 30},
    {id: "d", width: 90, height: 40}
  ],
  edges: [
    {from: "a", to: "b"},
    {from: "a", to: "c"},
    {from: "b", to: "d"},
    {from: "c", to: "d"}
  ]
}, {node_sep: 30, rank_sep: 70})

{
  size: [result.width, result.height],
  nodes: [for (n in result.nodes) {
    id: n.id,
    x: n.x,
    y: n.y,
    rank: n.rank,
    order: n.order
  }],
  edges: [for (e in result.edges) {
    from: e.from,
    to: e.to,
    points: e.points
  }],
  placements: result.placements
}
