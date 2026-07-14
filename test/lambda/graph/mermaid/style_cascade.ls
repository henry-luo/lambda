import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import graph_style: lambda.package.graph.style
import paint: lambda.package.graph.transform.paint
import transform: lambda.package.graph.transform

let parsed = graph_style.parse(
  "fill:rgb(10, 20, 30),stroke:#abc,stroke-width:2px,opacity:.5," ++
  "stroke-dasharray:5 3,position:absolute,background:url(javascript:bad)")
let preserved = graph_style.parse(
  "stroke:#123456;stroke:url(javascript:bad);stroke-width:3px;stroke-width:calc(9px)")

let class_graph^class_err = input("test/lambda/graph/mermaid/class_metadata.mmd",
  {type: "graph", flavor: "mermaid"})
let class_html = transform.to_html(class_graph)
let class_nodes = [for (i in 0 to (len(class_html) - 1), let child = class_html[i]
  where string(name(child)) == "node") child]

let metadata_graph^metadata_err = input("test/lambda/graph/mermaid/metadata_styles.mmd",
  {type: "graph", flavor: "mermaid"})
let metadata_html = transform.to_html(metadata_graph)
let metadata_edges = [for (i in 0 to (len(metadata_html) - 1), let child = metadata_html[i]
  where string(name(child)) == "edge") child]
let edge = metadata_edges[0]
let result = layout.from_velmts(
  {attrs: {'data-direction': "LR"}},
  [
    {tag: "node", index: 0, width: 60, height: 30,
      attrs: {'data-node-id': "A", 'data-shape': "box"}},
    {tag: "node", index: 1, width: 60, height: 30,
      attrs: {'data-node-id': "B", 'data-shape': "box"}},
    {tag: "edge", index: 2, width: 0, height: 0, attrs: {
      'data-edge-id': edge["data-edge-id"], 'data-from': edge["data-from"],
      'data-to': edge["data-to"], 'data-directed': edge["data-directed"],
      'data-marker-start': edge["data-marker-start"],
      'data-marker-end': edge["data-marker-end"],
      'data-style-declarations': edge["data-style-declarations"]
    }}
  ], null)
let layer = paint.layers(result)[0]
let svg = layer.content
let path = svg[len(svg) - 1]

{
  parsed: [parsed.fill, parsed.stroke, parsed.stroke_width, parsed.opacity,
    parsed.dash_array, graph_style.node_css(parsed)],
  invalid_preserves_previous: [preserved.stroke, preserved.stroke_width],
  class_cascade: [
    model.node_style_declarations_for(class_graph, "A"),
    class_nodes[0]["data-style-declarations"],
    contains(class_nodes[0].style, "background:#fff4cc;"),
    contains(class_nodes[0].style, "border-color:#8a6500;")
  ],
  edge_cascade: [
    result.edges[0].stroke, result.edges[0].stroke_width,
    path.stroke, path["stroke-width"]
  ]
}
