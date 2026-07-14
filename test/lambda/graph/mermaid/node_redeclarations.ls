import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

let source^parse_err = input("test/lambda/graph/mermaid/node_redeclarations.mmd",
  {type: "graph", flavor: "mermaid"})
let result = normalize.normalize(source)
let canonical = result.graph
let repeated = normalize.normalize(canonical)
let html = transform.to_html(source)
let html_nodes = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "node") child]

{
  source_stage: source["ir-stage"],
  source_nodes: [for (node in model.nodes(source)) [
    node.id, node.label, node.shape, node["source-line"]
  ]],
  canonical_stage: canonical["ir-stage"],
  canonical_valid: result.valid,
  canonical_diagnostics: result.diagnostics,
  canonical_nodes: [for (node in model.nodes(canonical)) [
    node.id, model.label_source(node), node.shape, node["source-line"]
  ]],
  idempotent: repeated.graph == canonical,
  html_nodes: [for (node in html_nodes) [
    node["data-node-id"], node["data-shape"], node[0]
  ]]
}
