import conformance: lambda.package.graph.conformance

fn scene_case(test_case) => string(test_case.policy) == "scene-semantic"

fn run_case(test_case) {
  let source_path = "test/lambda/graph/mermaid/" ++ string(test_case.source);
  let expected_path = "test/lambda/graph/mermaid/" ++ string(test_case["expected-scene"]);
  let graph^graph_error = input(source_path, {type: "graph", flavor: "mermaid"});
  let expected^expected_error = input(expected_path, {type: "mark"});
  let result = conformance.compare(graph, expected, test_case, 800, 600);
  {
    id: string(test_case.id),
    equal: result.comparison["equal"],
    metadata: conformance.scene_metadata(result.rendered),
    diagnostics: result.comparison.diagnostics
  }
}

let installed = conformance.install()
let cases = [for (test_case in conformance.manifest_cases(
  "test/lambda/graph/mermaid/manifest.mark") where scene_case(test_case)) test_case]
let results = [for (test_case in cases) run_case(test_case)]

{
  installed: installed,
  retained_runs: len(results),
  cases: [for (result in results) [result.id, result.equal, result.metadata]],
  diagnostics: [for (result in results where not result.equal)
    {id: result.id, issues: result.diagnostics}]
}
