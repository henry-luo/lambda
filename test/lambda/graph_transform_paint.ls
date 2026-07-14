import graph_layout: lambda.package.graph.layout
import paint: lambda.package.graph.transform.paint

let result = graph_layout.compute({
  nodes: [
    {id: "a", width: 80, height: 40},
    {id: "b", width: 80, height: 40}
  ],
  edges: [{id: "ab", from: "a", to: "b", directed: true, arrow_start: true, z: -2}]
}, {rank_sep: 70})
let layers = paint.layers(result)
let layer = layers[0]
let svg = layer.content
let path = svg[len(svg) - 1]

{
  count: len(layers),
  z: layer.z,
  edge_id: layer.edge_id,
  svg: [name(svg), svg.width, svg.height, svg.viewBox],
  path: [name(path), path.d, path['marker-start'], path['marker-end']]
}
