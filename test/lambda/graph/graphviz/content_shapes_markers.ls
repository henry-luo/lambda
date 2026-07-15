import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import paint: lambda.package.graph.transform.paint
import scene: lambda.package.graph.scene
import transform: lambda.package.graph.transform

fn direct_children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn marker_components(marker) {
  let content = if (len(marker) > 0) marker[0] else null;
  [for (child in model.element_children(content)
    where child["data-marker-component"] != null) child["data-marker-component"]]
}

let source^source_error = input(
  "test/lambda/graph/graphviz/content_shapes_markers.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let nodes = model.nodes(graph)
let edges = model.edges(graph)
let html = transform.to_html(graph)
let html_nodes = direct_children(html, "node")
let html_edges = direct_children(html, "edge")
let result = layout.compute({
  nodes: [for (node in nodes) {
    id: node.id, width: 72.0, height: 42.0, shape: node.shape,
    polygon_sides: node["polygon-sides"],
    polygon_orientation: node["polygon-orientation"],
    polygon_skew: node["polygon-skew"],
    polygon_distortion: node["polygon-distortion"],
    regular: node.regular, peripheries: node.peripheries
  }],
  edges: edges,
  directed: true,
  direction: "LR"
})
let layers = paint.layers(result)
let polygon_layout = [for (node in result.nodes where node.id == "f") node][0]
let polygon_html = [for (node in html_nodes where node["data-node-id"] == "f") node][0]
let composed_html = [for (edge in html_edges
  where edge["data-from"] == "e" and edge["data-to"] == "f") edge][0]
let styled = [for (node in nodes where node.id == "i") node][0]
let styled_html = [for (node in html_nodes where node["data-node-id"] == "i") node][0]
let unsafe_html = transform.to_html(<graph directed: true;
  <node id: "unsafe", label: "Unsafe", 'graphviz-shape': "box",
    fill: "red:blue;background:black", stroke: "red;border-width:99px",
    'font-name': "Arial;color:red", 'font-color': "red;background:black",
    style: "filled;background:black">
>)
let unsafe_node = direct_children(unsafe_html, "node")[0]
let graph_scene = scene.from_svg(<svg;
  <g 'data-graph-role': "graph", 'data-x': 0, 'data-y': 0,
      'data-width': 160, 'data-height': 80, 'data-direction': "LR";
    <node 'data-graph-role': "node", 'data-node-id': "e", 'data-shape': "house",
      'data-x': 0, 'data-y': 10, 'data-width': 40, 'data-height': 40>
    <node 'data-graph-role': "node", 'data-node-id': "f",
      'data-shape': polygon_html["data-shape"],
      'data-shape-family': polygon_html["data-shape-family"],
      'data-polygon-sides': polygon_html["data-polygon-sides"],
      'data-polygon-orientation': polygon_html["data-polygon-orientation"],
      'data-polygon-skew': polygon_html["data-polygon-skew"],
      'data-polygon-distortion': polygon_html["data-polygon-distortion"],
      'data-regular': polygon_html["data-regular"],
      'data-peripheries': polygon_html["data-peripheries"],
      'data-x': 100, 'data-y': 0, 'data-width': 60, 'data-height': 60>
    <path 'data-graph-role': "edge", 'data-edge-id': "ef",
      'data-from': "e", 'data-to': "f", 'data-route': "40,30 100,30",
      'data-marker-start': composed_html["data-marker-start"],
      'data-marker-end': composed_html["data-marker-end"],
      'data-arrow-size': composed_html["data-arrow-size"]>
  >
>)
let scene_polygon = model.nodes(graph_scene)[1]
let scene_edge = model.edges(graph_scene)[0]

{
  valid: normalized.valid,
  diagnostics: [for (value in normalized.diagnostics)
    [value.code, value.severity, value.path]],
  graph_label: graph.label,
  nodes: [for (node in nodes) [node.id, node.label, node.shape,
    node["shape-family"], node["graphviz-shape"], node["polygon-sides"],
    node["polygon-orientation"], node["polygon-skew"], node["polygon-distortion"],
    node.regular, node.peripheries]],
  edges: [for (edge in edges) [edge.from, edge.to, edge.label,
    edge["arrow-tail"], edge["arrow-head"], edge["arrow-direction"], edge["arrow-size"]]],
  html_nodes: [for (node in html_nodes) [node["data-node-id"], node["data-label"],
    node["data-shape"], node["data-shape-family"], node["data-graphviz-shape"],
    node["data-polygon-sides"], node["data-peripheries"],
    contains(string(node.style), "clip-path:polygon"),
    contains(string(node.style), "box-shadow:")]],
  html_edges: [for (edge in html_edges) [edge["data-from"], edge["data-to"],
    edge["data-marker-start"], edge["data-marker-end"], edge["data-arrow-size"]]],
  styled: {
    canonical: [styled.width, styled.height, styled["fixed-size"],
      abs(styled["margin-x"] - 19.2) < 0.001,
      abs(styled["margin-y"] - 9.6) < 0.001, styled.fill, styled["gradient-angle"],
      styled["font-name"], styled["font-size"], styled["font-color"],
      styled.stroke, abs(styled["stroke-width"] - 2.6666667) < 0.001],
    html: [styled_html["data-width"], styled_html["data-height"],
      styled_html["data-fixed-size"],
      abs(styled_html["data-margin-x"] - 19.2) < 0.001,
      abs(styled_html["data-margin-y"] - 9.6) < 0.001, styled_html["data-fill"],
      styled_html["data-font-name"], styled_html["data-font-size"],
      styled_html["data-font-color"], styled_html["data-color"], styled_html["data-stroke"],
      styled_html["data-stroke-width"],
      contains(string(styled_html.style), "padding:9.6px 19.2px"),
      contains(string(styled_html.style), "aspect-ratio:1"),
      contains(string(styled_html.style), "width:96px;height:96px"),
      contains(string(styled_html.style), "radial-gradient(red,blue)"),
      contains(string(styled_html.style), "font-family:Times New Roman"),
      contains(string(styled_html.style), "font-size:24px")]
  },
  unsafe_fallback: [
    not contains(string(unsafe_node.style), "background:black"),
    not contains(string(unsafe_node.style), "border-width:99px"),
    not contains(string(unsafe_node.style), "font-family:Arial"),
    unsafe_node["data-fill"] == "#ffffff",
    unsafe_node["data-stroke"] == "#64748b"
  ],
  polygon_layout: [polygon_layout.width, polygon_layout.height],
  scene: [scene_polygon.shape, scene_polygon["shape-family"],
    scene_polygon["polygon-sides"], scene_polygon["polygon-orientation"],
    scene_polygon["polygon-skew"], scene_polygon["polygon-distortion"],
    scene_polygon.regular, scene_polygon.peripheries,
    scene_edge["marker-start"], scene_edge["marker-end"], scene_edge["arrow-size"]],
  paint: [for (layer in layers, let defs = layer.content[0]) [
    layer.edge_id, defs[0]["data-marker-type"], defs[1]["data-marker-type"],
    string(name(defs[0][0])), string(name(defs[1][0])),
    marker_components(defs[0]), marker_components(defs[1]),
    defs[1].markerWidth, defs[1].markerHeight
  ]]
}
