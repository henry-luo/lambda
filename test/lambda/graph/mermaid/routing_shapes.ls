import layout: lambda.package.graph.layout

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

{
  size: [result.width, result.height],
  edge: result.edges[0].points,
  loop: result.edges[1].points
}
