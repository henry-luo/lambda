import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn run_case(test_case) {
  let path = "test/lambda/graph/graphviz/" ++ string(test_case.source);
  let source^source_error = input(path, {type: "graph", flavor: "dot"});
  let result = normalize.normalize(source);
  let html = transform.to_html(result.graph);
  {
    id: string(test_case.id),
    valid: result.valid,
    nodes: len(model.nodes(result.graph)),
    edges: len(model.edges(result.graph)),
    annotations: len(model.annotations(result.graph)),
    html: [len(children(html, "node")), len(children(html, "edge")),
      len(children(html, "annotation"))]
  }
}

let manifest^manifest_error = input(
  "test/lambda/graph/graphviz/manifest.mark", {type: "mark"})
let cases = [for (test_case in model.element_children(manifest)
  where model.tag(test_case) == "case") test_case]

{
  cases: len(cases),
  results: [for (test_case in cases) run_case(test_case)]
}
