import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr
import expression: lambda.package.graph.structurizr.expressions

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn pairs(value, wanted) => [
  for (child in children(value, wanted)) [child.name,
    if (wanted == "property") child.value else child.description,
    if (wanted == "perspective") child.value else null,
    if (wanted == "perspective") child.url else null]
]

let source^err = input("test/lambda/graph/structurizr/metadata.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let c4_model = children(workspace, "c4-model")[0]
let entries = children(c4_model, "c4-element")
let relationships = children(c4_model, "c4-relationship")
let user = [for (entry in entries where entry.id == "user") entry][0]
let group_entry = [for (entry in entries where entry.kind == "group") entry][0]
let relation = relationships[0]
let retail = structurizr.project(workspace, "Retail")
let sales = structurizr.project(workspace, "Sales")
let without_https = structurizr.project(workspace, "WithoutHttps")
let projected_user = model.nodes(retail)[0]

{
  model: pairs(c4_model, "property"),
  group: [group_entry.kind, group_entry.name, user.parent, user.group],
  element: [pairs(user, "property"), pairs(user, "perspective")],
  relationship: [relation.id, pairs(relation, "property"),
    pairs(relation, "perspective")],
  expressions: [
    expression.element_ids(entries, relationships,
      "element.properties[owner]==Retail"),
    expression.element_ids(entries, relationships,
      "element.properties[criticality]==low"),
    expression.element_ids(entries, relationships, "element.group==Sales"),
    expression.relationship_matches(entries, relation,
      "relationship.properties[protocol]==HTTPS")
  ],
  projected: [[for (node in model.nodes(retail)) node.id],
    [for (group in model.subgraphs(retail)) [group.id, group["c4-kind"], group.label]],
    len(model.edges(retail)), pairs(projected_user, "property"),
    pairs(projected_user, "perspective")],
  grouped: [for (node in model.nodes(sales)) [node.id, node.group]],
  excluded: len(model.edges(without_https))
}
