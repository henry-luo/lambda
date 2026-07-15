import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

pub fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

pub fn manifest_cases(path) {
  let manifest^manifest_error = input(path, {type: "mark"});
  [for (value in model.element_children(manifest) where model.tag(value) == "case") value]
}

fn option(test_case, key, fallback) {
  let value = test_case[key];
  if (value == null or value is error) fallback else value
}

pub fn comparison_policy(test_case) => {
  'geometry-tolerance': option(test_case, "geometry-tolerance", 1.5),
  'route-tolerance': option(test_case, "route-tolerance", 2.0),
  relations: option(test_case, "relations", true),
  'rank-order': option(test_case, "rank-order", true),
  'route-geometry': option(test_case, "route-geometry", true)
}

pub fn install() => transform.install()

pub fn to_html(graph) => transform.to_html(graph)

pub fn compare(source, expected, test_case, width, height) {
  let rendered = transform.render_scene(source, width, height);
  let comparison = transform.compare_scenes(
    rendered.scene, expected, comparison_policy(test_case));
  {rendered: rendered, comparison: comparison}
}

fn route_points(edge) => [
  for (route in children(edge, "route"), point in children(route, "point")) point
]

pub fn scene_metadata(rendered) {
  let nodes = children(rendered.scene, "node");
  let edges = children(rendered.scene, "edge");
  contains(rendered.svg, "data-graph-role=\"graph\"") and
    all([for (node in nodes) node.width > 0.0 and node.height > 0.0]) and
    all([for (edge in edges) len(route_points(edge)) >= 2])
}
