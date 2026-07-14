import graph_transform: lambda.package.graph.transform

fn scene_children(value, wanted_tag) => [
  for (i in 0 to (len(value) - 1), let child = value[i]
    where child is element and string(name(child)) == wanted_tag) child
]

fn route_points(edge) => [
  for (route in scene_children(edge, "route"),
       point in scene_children(route, "point")) point
]

let installed = graph_transform.install()
let rendered = graph_transform.render_scene(input(
  "test/lambda/graph/mermaid/shapes_and_labels.mmd", {type: "graph", flavor: "mermaid"}),
  800, 600)
let expected^expected_error = input(
  "test/lambda/graph/mermaid/expected/scene/shapes_and_labels.mark", {type: "mark"})
let comparison = graph_transform.compare_scenes(rendered.scene, expected)
let nodes = scene_children(rendered.scene, "node")
let edges = scene_children(rendered.scene, "edge")

let upstream = graph_transform.render_scene(input(
  "test/lambda/graph/mermaid/cases/flowchart/nodes/upstream_single_diamond.mmd",
  {type: "graph", flavor: "mermaid"}), 800, 600)
let upstream_expected^upstream_expected_error = input(
  "test/lambda/graph/mermaid/expected/scene/upstream_single_diamond.mark", {type: "mark"})
let upstream_comparison = graph_transform.compare_scenes(upstream.scene, upstream_expected)

let geometry_actual = <'graph-scene' direction: "LR";
  <node id: "n", shape: "box", x: 10.0, y: 20.0, width: 40.0, height: 30.0>
  <edge id: "e", 'from': "n", 'to': "n", 'marker-start': "none", 'marker-end': "normal",
      'route-kind': "self-loop";
    <route; <point x: 50.0, y: 35.0> <point x: 70.0, y: 35.0>>
  >
>
let geometry_expected = <'graph-scene' direction: "LR";
  <node id: "n", shape: "box", x: 11.4, y: 19.0, width: 40.8, height: 29.0>
  <edge id: "e", 'from': "n", 'to': "n", 'marker-start': "none", 'marker-end': "normal",
      'route-kind': "self-loop";
    <route; <point x: 51.9, y: 34.0> <point x: 68.2, y: 36.0>>
  >
>
let geometry_comparison = graph_transform.compare_scenes(geometry_actual, geometry_expected)
let strict_geometry_comparison = graph_transform.compare_scenes(
  geometry_actual, geometry_expected,
  {'geometry-tolerance': 0.5, 'route-tolerance': 0.5})

{
  installed: installed,
  metadata: contains(rendered.svg, "data-graph-role=\"graph\"") and
    contains(rendered.svg, "data-node-id=\"A\"") and
    contains(rendered.svg, "data-route="),
  nodes: len(nodes),
  edges: len(edges),
  measured: all([for (node in nodes) node.width > 0.0 and node.height > 0.0]),
  routes: all([for (edge in edges) len(route_points(edge)) >= 2]),
  semantic: comparison["equal"],
  diagnostics: comparison.diagnostics,
  upstream_semantic: upstream_comparison["equal"],
  tolerant_geometry: geometry_comparison["equal"],
  strict_geometry: strict_geometry_comparison["equal"]
}
