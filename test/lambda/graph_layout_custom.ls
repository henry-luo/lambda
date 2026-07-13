import graph: lambda.package.graph.graph

graph.layout_custom(null, [
  {id: "a", index: 0, width: 80, height: 40},
  {id: "b", index: 1, width: 100, height: 50}
], null, {
  edges: [{from: "a", to: "b"}],
  node_sep: 20,
  rank_sep: 50
})
