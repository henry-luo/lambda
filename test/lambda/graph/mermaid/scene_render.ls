import graph_transform: lambda.package.graph.transform
import model: lambda.package.graph.model

fn scene_children(value, wanted_tag) => [
  for (child in model.element_children(value) where model.tag(child) == wanted_tag) child
]

fn route_points(edge) => [
  for (route in scene_children(edge, "route"), point in scene_children(route, "point")) point
]

fn scene_case(test_case) => string(test_case.policy) == "scene-semantic"

fn comparison_policy(test_case) => {
  'geometry-tolerance': if (test_case["geometry-tolerance"] != null)
    test_case["geometry-tolerance"] else 1.5,
  'route-tolerance': if (test_case["route-tolerance"] != null)
    test_case["route-tolerance"] else 2.0,
  relations: if (test_case.relations != null) test_case.relations else true,
  'rank-order': if (test_case["rank-order"] != null) test_case["rank-order"] else true
}

fn run_case(test_case) {
  let source_path = "test/lambda/graph/mermaid/" ++ string(test_case.source);
  let expected_path = "test/lambda/graph/mermaid/" ++ string(test_case["expected-scene"]);
  let graph^graph_error = input(source_path, {type: "graph", flavor: "mermaid"});
  let expected^expected_error = input(expected_path, {type: "mark"});
  let rendered = graph_transform.render_scene(graph, 800, 600);
  let comparison = graph_transform.compare_scenes(
    rendered.scene, expected, comparison_policy(test_case));
  let nodes = scene_children(rendered.scene, "node");
  let edges = scene_children(rendered.scene, "edge");
  {
    id: string(test_case.id),
    equal: comparison["equal"],
    metadata: contains(rendered.svg, "data-graph-role=\"graph\"") and
      all([for (node in nodes) node.width > 0.0 and node.height > 0.0]) and
      all([for (edge in edges) len(route_points(edge)) >= 2]),
    diagnostics: comparison.diagnostics
  }
}

let installed = graph_transform.install()
let manifest^manifest_error = input(
  "test/lambda/graph/mermaid/manifest.mark", {type: "mark"})
let cases = [for (test_case in model.element_children(manifest)
  where model.tag(test_case) == "case" and scene_case(test_case)) test_case]
let results = [for (test_case in cases) run_case(test_case)]

{
  installed: installed,
  retained_runs: len(results),
  cases: [for (result in results) [result.id, result.equal, result.metadata]],
  diagnostics: [for (result in results where not result.equal)
    {id: result.id, issues: result.diagnostics}]
}
