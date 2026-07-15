import model: lambda.package.graph.model

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn args(value) => [for (child in children(value, "argument")) child.value]

let source^err = input("test/lambda/graph/structurizr/basic.dsl",
  {type: "graph", flavor: "structurizr"});
if (err) raise(err)
else {
  let workspace_children = model.element_children(source);
  let source_model = children(source, "model")[0];
  let source_views = children(source, "views")[0];
  let shop = children(source_model, "declaration")[1];
  let context = children(source_views, "view")[0];
  let styles = children(source_views, "styles")[0];
  {
    workspace: [model.tag(source), args(source), source.flavor, source["ir-stage"]],
    order: [for (child in workspace_children) model.tag(child)],
    model_order: [for (child in model.element_children(source_model)) model.tag(child)],
    shop: [shop.identifier, shop.keyword, args(shop),
      [for (child in children(shop, "declaration")) [child.identifier, child.keyword, args(child)]]],
    relationships: [for (relation in children(source_model, "relationship"))
      [relation.from, relation.to, args(relation)]],
    view: [context.kind, args(context),
      args(children(context, "include")[0]), args(children(context, "auto-layout")[0])],
    colors: [for (rule in children(styles, "style-rule"))
      for (property in model.element_children(rule)
        where model.tag(property) == "statement") [property.keyword, args(property)]],
    spans_valid: [for (child in workspace_children)
      child["source-start"] < child["source-end"]]
  }
}
