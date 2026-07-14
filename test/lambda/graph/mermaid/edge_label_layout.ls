import layout: lambda.package.graph.layout

let result = layout.from_velmts(
  {attrs: {'data-direction': "TB", 'data-rank-sep': "70"}},
  [
    {tag: "node", index: 0, width: 80, height: 40,
      attrs: {'data-node-id': "a", 'data-shape': "box"}},
    {tag: "node", index: 1, width: 80, height: 40,
      attrs: {'data-node-id': "b", 'data-shape': "box"}},
    {tag: "edge-label", index: 2, width: 50, height: 20,
      attrs: {'data-edge-id': "ab", 'data-z': "1"}},
    {tag: "edge", index: 3, width: 0, height: 0,
      attrs: {'data-edge-id': "ab", 'data-from': "a", 'data-to': "b",
        'data-directed': "true", 'data-arrow-end': "true"}}
  ], null)

{
  size: [result.width, result.height],
  placements: result.placements,
  edge_points: result.edges[0].points
}
