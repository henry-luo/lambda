import model: lambda.graph.model
import structurizr: lambda.graph.structurizr.structurizr

let source = (input("test/lambda/graph/structurizr/invalid.dsl",
  {type: "graph", flavor: "structurizr"})) ^ { null }
let workspace = structurizr.normalize(source);

[
  for (value in model.diagnostics(workspace))
    [value.code, value.severity, value.path]
]
