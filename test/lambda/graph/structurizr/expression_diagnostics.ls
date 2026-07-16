import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

let source^err = input("test/lambda/graph/structurizr/expression_diagnostics.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)

[
  for (value in model.diagnostics(workspace))
    [value.code, value.severity, value.path,
      value["source-line"], value["source-column"],
      value["source-start"] < value["source-end"]]
]
