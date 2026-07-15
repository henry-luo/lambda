import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import conformance: lambda.package.graph.conformance
import adapter: .reference.graphviz_json_adapter

fn run_case(test_case) {
  let path = "test/lambda/graph/graphviz/" ++ string(test_case.source);
  let source^source_error = input(path, {type: "graph", flavor: "dot"});
  let result = normalize.normalize(source);
  let html = conformance.to_html(result.graph);
  {
    id: string(test_case.id),
    valid: result.valid,
    nodes: len(model.nodes(result.graph)),
    edges: len(model.edges(result.graph)),
    annotations: len(model.annotations(result.graph)),
    html: [len(conformance.children(html, "node")),
      len(conformance.children(html, "edge")),
      len(conformance.children(html, "annotation"))]
  }
}

fn run_reference(test_case) {
  let path = "test/lambda/graph/graphviz/" ++ string(test_case.source);
  let reference_path = "test/lambda/graph/graphviz/" ++ string(test_case.reference);
  let source^source_error = input(path, {type: "graph", flavor: "dot"});
  let reference^reference_error = input(reference_path, {type: "json"});
  let normalized = normalize.normalize(source);
  let expected = adapter.from_dot_json(reference, true,
    [for (edge in model.edges(normalized.graph)) edge.id]);
  let result = conformance.compare(source, expected, test_case, 640, 480);
  {id: string(test_case.id), equal: result.comparison["equal"],
    diagnostics: result.comparison.diagnostics}
}

let cases = conformance.manifest_cases("test/lambda/graph/graphviz/manifest.mark")
let installed = conformance.install()

{
  cases: len(cases),
  results: [for (test_case in cases) run_case(test_case)],
  references: [for (test_case in cases where test_case.reference != null)
    run_reference(test_case)]
}
