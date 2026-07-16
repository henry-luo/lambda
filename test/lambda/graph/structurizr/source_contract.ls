import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

let source^err = input("test/lambda/graph/structurizr/source_contract.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let invalid_root = structurizr.validate_source(<model>)

{
  source: [for (value in model.diagnostics(workspace))
    [value.code, value.path, value["source-line"], value["source-column"]]],
  root: [for (value in invalid_root) [value.code, value.path]]
}
