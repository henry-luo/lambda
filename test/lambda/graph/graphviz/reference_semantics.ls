import graph_transform: lambda.package.graph.transform
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import adapter: .reference.graphviz_json_adapter

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn label(value) => model.label_source(value)

fn summary(scene) => {
  direction: scene.direction,
  clusters: [for (value in children(scene, "cluster"))
    [value.id, value.parent, label(value)]],
  nodes: [for (value in children(scene, "node"))
    [value.id, value.shape, value.group, value.stroke, label(value)]],
  edges: [for (value in children(scene, "edge"))
    [value.id, value.from, value.to, value["marker-start"], value["marker-end"],
      value.stroke, label(value)]]
}

let source^source_error = input(
  "test/lambda/graph/graphviz/reference_semantics.dot", {type: "graph", flavor: "dot"})
let reference^reference_error = input(
  "test/lambda/graph/graphviz/reference/reference_semantics.dot.json", {type: "json"})
let canonical = normalize.normalize(source)
let actual = adapter.from_canonical(canonical.graph)
let expected = adapter.from_dot_json(reference)
let comparison = graph_transform.compare_scenes(actual, expected, {relations: false})
let installed = graph_transform.install()
let rendered = graph_transform.render_scene(source, 640, 480)
let expected_geometry = adapter.from_dot_json(reference, true,
  [for (edge in model.edges(canonical.graph)) edge.id])
let geometry = graph_transform.compare_scenes(rendered.scene, expected_geometry, {
  // Engines use different font metrics and separation constants; topology and
  // relative geometry remain strict while absolute boxes allow one rank gap.
  'geometry-tolerance': 75,
  'route-geometry': false,
  'rank-order': true
})

{
  valid: canonical.valid,
  equal: comparison["equal"],
  diagnostics: comparison.diagnostics,
  geometry: [geometry["equal"], geometry.diagnostics],
  actual: summary(actual),
  expected: summary(expected)
}
