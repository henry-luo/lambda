import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

let source^err = input("test/lambda/graph/structurizr/invalid.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)

[
  for (value in model.diagnostics(workspace))
    [value.code, value.severity, value.path]
]
