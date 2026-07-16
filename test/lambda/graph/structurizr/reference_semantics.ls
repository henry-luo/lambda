import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr
import conformance: lambda.package.graph.conformance
import adapter: .reference.structurizr_json_adapter

let cases = conformance.manifest_cases("test/lambda/graph/structurizr/manifest.mark")
let references = [for (test_case in cases where test_case.reference != null) test_case]

fn run(test_case) {
  let source^source_error = input(
    "test/lambda/graph/structurizr/" ++ string(test_case.source),
    {type: "graph", flavor: "structurizr"});
  let reference^reference_error = input(
    "test/lambda/graph/structurizr/" ++ string(test_case.reference), {type: "json"});
  let workspace = structurizr.normalize(source);
  let actual = adapter.from_canonical(workspace);
  let expected = adapter.from_json(reference);
  {
    id: string(test_case.id), errors: [source_error, reference_error],
    equal: actual == expected,
    sections: [actual.workspace == expected.workspace,
      actual.elements == expected.elements,
      actual.relationships == expected.relationships,
      actual.views == expected.views,
      actual.styles == expected.styles],
    counts: [len(actual.elements), len(actual.relationships),
      len(actual.views), len(actual.styles)],
    diagnostics: [for (value in model.diagnostics(workspace)) value.code]
  }
}

{
  manifest: [len(cases), len(references)],
  results: [for (test_case in references) run(test_case)]
}
