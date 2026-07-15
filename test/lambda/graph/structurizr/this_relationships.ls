import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^err = input("test/lambda/graph/structurizr/this_relationships.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let c4_model = children(workspace, "c4-model")[0]

[for (relation in children(c4_model, "c4-relationship"))
  [relation.id, relation.source, relation.destination, relation.description]]
