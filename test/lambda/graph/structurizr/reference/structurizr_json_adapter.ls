// Structurizr JSON and Lambda C4 Workspace Mark to one semantic comparison value.

import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn children(value, wanted = null) => [
  for (child in model.element_children(value)
    where wanted == null or model.tag(child) == wanted) child
]

fn first(values) => if (len(values) > 0) values[0] else null

fn values(value, key) {
  let found = model.optional(value, key);
  if (found == null) { [] } else { found }
}

fn text(value, key, fallback = null) {
  let found = model.optional(value, key);
  if (found == null or string(found) == "") fallback else string(found)
}

fn either(value, fallback) => if (value == null) fallback else value

fn kind_label(kind) {
  if (kind == "software-system") "Software System"
  else if (kind == "deployment-environment") "Deployment Environment"
  else if (kind == "deployment-node") "Deployment Node"
  else if (kind == "infrastructure-node") "Infrastructure Node"
  else if (kind == "software-system-instance") "Software System Instance"
  else if (kind == "container-instance") "Container Instance"
  else upper(slice(kind, 0, 1)) ++ slice(kind, 1, len(kind))
}

fn tags(raw, kind, relationship = false) => sort([
  for (tag in split(if (raw == null) "" else string(raw), ","), let value = trim(tag)
    where value != "" and value != "Element" and
      value != if (relationship) "Relationship" else kind_label(kind)) value
])

fn segment(kind, name) => kind ++ ":" ++ string(name)

fn record(raw, id, kind, parent, name, description = null, technology = null,
    model_ref = null) => {
  raw: raw, raw_id: string(id), kind: kind, parent: parent,
  key: (if (parent == null) "" else parent ++ "/") ++ segment(kind, name),
  name: name, description: description, technology: technology,
  model_ref: model_ref, tags: tags(model.optional(raw, "tags"), kind)
}

fn components(container, parent) => [
  for (value in values(container, "components"))
    record(value, value.id, "component", parent, value.name,
      model.optional(value, "description"), model.optional(value, "technology"))
]

fn container_entries(value, parent) {
  let key = parent ++ "/" ++ segment("container", value.name);
  [
    record(value, value.id, "container", parent, value.name,
      model.optional(value, "description"), model.optional(value, "technology")),
    *components(value, key)
  ]
}

fn containers(system, parent) => [
  for (value in values(system, "containers"), entry in container_entries(value, parent)) entry
]

fn people(value) => [for (person in values(value, "people"))
  record(person, person.id, "person", null, person.name,
    model.optional(person, "description"))
]

fn system_entries(system) {
  let key = segment("software-system", system.name);
  [
    record(system, system.id, "software-system", null, system.name,
      model.optional(system, "description"), model.optional(system, "technology")),
    *containers(system, key)
  ]
}

fn systems(value) => [for (system in values(value, "softwareSystems"),
  entry in system_entries(system)) entry]

fn custom_elements(value) => [for (custom in values(value, "customElements"))
  record(custom, custom.id, "custom", null, custom.name,
    model.optional(custom, "description"), model.optional(custom, "technology"))
]

fn logical_elements(value) => [
  *people(value), *systems(value), *custom_elements(value)
]

fn by_raw_id(records, id) => first([
  for (entry in records where entry.raw_id == string(id)) entry
])

fn deployment_instances(node, parent, logical) => [
  *[for (value in values(node, "softwareSystemInstances"),
      let target = by_raw_id(logical, value.softwareSystemId) where target != null)
      record(value, value.id, "software-system-instance", parent, target.name,
        target.description, target.technology, target.key)],
  *[for (value in values(node, "containerInstances"),
      let target = by_raw_id(logical, value.containerId) where target != null)
      record(value, value.id, "container-instance", parent, target.name,
        target.description, target.technology, target.key)],
  *[for (value in values(node, "infrastructureNodes"))
      record(value, value.id, "infrastructure-node", parent, value.name,
        model.optional(value, "description"), model.optional(value, "technology"))]
]

fn deployment_node(value, parent, logical) {
  let key = parent ++ "/" ++ segment("deployment-node", value.name);
  [
    record(value, value.id, "deployment-node", parent, value.name,
      model.optional(value, "description"), model.optional(value, "technology")),
    *deployment_instances(value, key, logical),
    for (child in values(value, "children"), entry in deployment_node(child, key, logical)) entry
  ]
}

fn deployment_elements(value, logical) {
  let roots = values(value, "deploymentNodes");
  let environments = unique([for (node in roots) string(node.environment)]);
  [
    *[for (environment in environments)
      record({}, "environment:" ++ environment, "deployment-environment", null, environment)],
    *[for (node in roots, entry in deployment_node(node,
      segment("deployment-environment", node.environment), logical)) entry]
  ]
}

fn json_elements(workspace) {
  let logical = logical_elements(workspace.model);
  [*logical, *deployment_elements(workspace.model, logical)]
}

fn canonical_parent_key(entries, value) {
  let parent = first([for (entry in entries where string(entry.id) == string(value.parent)) entry]);
  if (parent == null) null else canonical_key(entries, parent)
}

fn canonical_key(entries, value) {
  let parent = canonical_parent_key(entries, value);
  (if (parent == null) "" else parent ++ "/") ++ segment(string(value.kind), value.name)
}

fn canonical_elements(workspace) {
  let blocks = children(workspace, "c4-model");
  let values = if (len(blocks) == 0) [] else children(blocks[0], "c4-element");
  [for (value in values) {
    raw: value, raw_id: string(value.id), kind: string(value.kind),
    parent: canonical_parent_key(values, value), key: canonical_key(values, value),
    name: value.name, description: model.optional(value, "description"),
    technology: model.optional(value, "technology"),
    model_ref: model.optional(value, "model-ref"),
    tags: sort([for (tag in children(value, "tag"), let name = string(tag.name)
      where name != "Element" and name != kind_label(string(value.kind))) name])
  }]
}

fn semantic_elements(records) => sort([for (entry in records
  where entry.kind != "deployment-group") [
  entry.key, entry.kind, entry.parent, entry.name, entry.description, entry.technology,
  if (entry.model_ref == null) null else {
    let target = by_raw_id(records, entry.model_ref);
    if (target == null) string(entry.model_ref) else target.key
  }, entry.tags
]])

fn logical_sources(value) => [
  *values(value, "people"), *values(value, "softwareSystems"), *[
    for (system in values(value, "softwareSystems"), container in values(system, "containers"))
      container
  ], *[
    for (system in values(value, "softwareSystems"), container in values(system, "containers"),
      component in values(container, "components")) component
  ], *values(value, "customElements")
]

fn logical_relationships(value) => [
  for (source in logical_sources(value), relationship in values(source, "relationships"))
    relationship
]

fn deployment_sources(value) => [
  value, *values(value, "infrastructureNodes"), *values(value, "softwareSystemInstances"),
  *values(value, "containerInstances"), *[
    for (child in values(value, "children"), source in deployment_sources(child)) source
  ]
]

fn deployment_relationships(value) => [
  for (root in values(value, "deploymentNodes"), source in deployment_sources(root),
    relationship in values(source, "relationships")) relationship
]

fn all_relationships(value) => [
  *logical_relationships(value), *deployment_relationships(value)
]

fn model_relationships(value) => [
  *logical_relationships(value), *[
    for (relationship in deployment_relationships(value)
      where model.optional(relationship, "linkedRelationshipId") == null) relationship
  ]
]

fn relation_signature(value, records) {
  let source = by_raw_id(records, value.sourceId);
  let destination = by_raw_id(records, value.destinationId);
  [if (source == null) string(value.sourceId) else source.key,
    if (destination == null) string(value.destinationId) else destination.key,
    model.optional(value, "description"), model.optional(value, "technology"),
    tags(model.optional(value, "tags"), "relationship", true)]
}

fn view_relation_signature(value, member, records, kind) {
  let signature = relation_signature(value, records);
  {source: signature[0], destination: signature[1],
    description: either(model.optional(member, "description"), signature[2]),
    technology: signature[3],
    order: if (kind == "dynamic") model.optional(member, "order") else null}
}

fn json_relationships(workspace, records) => sort([
  for (value in model_relationships(workspace.model)) relation_signature(value, records)
])

fn canonical_relationships(workspace, records) {
  let blocks = children(workspace, "c4-model");
  let relationships = if (len(blocks) == 0) [] else children(blocks[0], "c4-relationship");
  sort([for (value in relationships, let source = by_raw_id(records, value.source),
    let destination = by_raw_id(records, value.destination)) [
      if (source == null) string(value.source) else source.key,
      if (destination == null) string(value.destination) else destination.key,
      model.optional(value, "description"), model.optional(value, "technology"),
      sort([for (tag in children(value, "tag"), let name = string(tag.name)
        where name != "Relationship") name])
    ]])
}

fn direction(value) {
  let layout = model.optional(value, "automaticLayout");
  let raw = if (layout == null) null else text(layout, "rankDirection");
  if (raw == "LeftRight") "LR" else if (raw == "RightLeft") "RL"
  else if (raw == "BottomTop") "BT" else if (raw == "TopBottom") "TB" else null
}

fn view_values(value) => [
  for (kind_and_field in [
    ["systemLandscape", "systemLandscapeViews"], ["systemContext", "systemContextViews"],
    ["container", "containerViews"], ["component", "componentViews"],
    ["filtered", "filteredViews"], ["custom", "customViews"],
    ["dynamic", "dynamicViews"], ["deployment", "deploymentViews"]
  ], view in values(value, kind_and_field[1])) {kind: kind_and_field[0], value: view}
]

fn view_by_key(value, key) => first([
  for (entry in view_values(value) where string(entry.value.key) == string(key)) entry
])

fn raw_tags(value) => [
  for (tag in split(if (model.optional(value, "tags") == null) ""
    else string(value.tags), ","), let name = trim(tag) where name != "") name
]

fn has_any_tag(value, wanted) => len([
  for (tag in raw_tags(value) where contains(wanted, tag)) tag
]) > 0

fn filtered_members(entry, views, records) {
  let value = entry.value;
  let base = view_by_key(views, value.baseViewKey);
  let members = if (base == null) [] else values(base.value, "elements");
  let wanted = values(value, "tags");
  let include = lower(string(value.mode)) == "include";
  [for (member in members, let target = by_raw_id(records, member.id)
    where target != null and has_any_tag(target.raw, wanted) == include) member]
}

fn json_view_members(entry, views, records) {
  if (entry.kind == "filtered") filtered_members(entry, views, records)
  else values(entry.value, "elements")
}

fn json_view_relations(entry, views, records, relationships) {
  if (entry.kind != "filtered") { values(entry.value, "relationships") }
  else {
    let value = entry.value;
    let base = view_by_key(views, value.baseViewKey);
    let candidates = if (base == null) [] else values(base.value, "relationships");
    let members = json_view_members(entry, views, records);
    let member_ids = [for (member in members) string(member.id)];
    let wanted = values(value, "tags");
    let include = lower(string(value.mode)) == "include";
    [for (member in candidates, let relation = first([for (candidate in relationships
        where string(candidate.id) == string(member.id)) candidate])
      where relation != null and has_any_tag(relation, wanted) == include and
        contains(member_ids, string(relation.sourceId)) and
        contains(member_ids, string(relation.destinationId))) member]
  }
}

fn json_view_scope(entry, records) {
  let value = entry.value;
  let raw = if (entry.kind == "component") model.optional(value, "containerId")
    else if (contains(["systemContext", "container"], entry.kind))
      model.optional(value, "softwareSystemId")
    else model.optional(value, "elementId");
  let target = if (raw == null) null else by_raw_id(records, raw);
  if (target == null) raw else target.key
}

fn json_views(workspace, records, relationships) => sort([
  for (entry in view_values(workspace.views), let value = entry.value,
    let layout = model.optional(value, "automaticLayout")) [
    string(value.key), entry.kind, json_view_scope(entry, records),
    model.optional(value, "environment"), if (layout == null) "TB" else direction(value),
    if (layout == null) 300 else model.optional(layout, "rankSeparation"),
    if (layout == null) 300 else model.optional(layout, "nodeSeparation"),
    sort([for (member in json_view_members(entry, workspace.views, records),
      let target = by_raw_id(records, member.id))
      if (target == null) string(member.id) else target.key]),
    sort([for (member in json_view_relations(entry, workspace.views, records, relationships),
      let relation = first([for (candidate in relationships
        where string(candidate.id) == string(member.id)) candidate]) where relation != null)
      view_relation_signature(relation, member, records, entry.kind)])
  ]
])

fn canonical_view_values(workspace) {
  let blocks = children(workspace, "c4-views");
  if (len(blocks) == 0) { [] } else { children(blocks[0], "c4-view") }
}

fn canonical_views(workspace, records) => sort([
  for (view in canonical_view_values(workspace), let graph = structurizr.project(workspace, view.key),
    let scope = by_raw_id(records, model.optional(view, "scope")),
    let environment = by_raw_id(records, model.optional(view, "environment"))) [
    string(view.key), string(view.kind), if (scope == null) null else scope.key,
    if (environment == null) model.optional(view, "environment") else environment.name,
    model.optional(view, "direction"),
    model.optional(view, "rank-sep"), model.optional(view, "node-sep"),
    sort([*[
      for (node in model.nodes(graph), let target = by_raw_id(records, node.id))
        if (target == null) string(node.id) else target.key
    ], *[
      for (group in model.subgraphs(graph), let target = by_raw_id(records, group.id)
        where string(view.kind) == "deployment" and target != null) target.key
    ]]),
    sort([for (edge in model.edges(graph), let source = by_raw_id(records, edge.from),
      let destination = by_raw_id(records, edge.to)) {
      source: if (source == null) string(edge.from) else source.key,
      destination: if (destination == null) string(edge.to) else destination.key,
      description: either(model.optional(edge, "interaction-label"), model.optional(edge, "label")),
      technology: model.optional(edge, "technology"),
      order: if (string(view.kind) == "dynamic") model.optional(edge, "order") else null
    }])
  ]
])

fn style_properties(value, names) => sort([
  for (name in names, let found = model.optional(value, name) where found != null)
    [name, string(found)]
])

fn json_styles(workspace) {
  let configuration = model.optional(workspace.views, "configuration");
  let styles = if (configuration == null) null else model.optional(configuration, "styles");
  if (styles == null) { [] } else { sort([
    *[for (value in values(styles, "elements")) ["element", string(value.tag),
      style_properties(value, ["shape", "icon", "width", "height", "background", "color",
        "stroke", "strokeWidth", "border", "opacity", "fontSize", "metadata", "description"])]],
    *[for (value in values(styles, "relationships")) ["relationship", string(value.tag),
      style_properties(value, ["thickness", "color", "style", "routing", "fontSize",
        "width", "position", "opacity", "dashed"])] ]
  ]) }
}

fn canonical_styles(workspace) {
  let blocks = children(workspace, "c4-styles");
  if (len(blocks) == 0) { [] } else { sort([
    for (value in children(blocks[0], "c4-style")) [string(value["target-kind"]),
      string(value.tag), sort([for (property in children(value, "property"))
        [string(property.name), property.value]])]
  ]) }
}

pub fn from_json(workspace) {
  let records = json_elements(workspace);
  let relationships = all_relationships(workspace.model);
  {
    workspace: [workspace.name,
      if (string(model.optional(workspace, "description")) == "Description") null
      else model.optional(workspace, "description")],
    elements: semantic_elements(records),
    relationships: json_relationships(workspace, records),
    views: json_views(workspace, records, relationships),
    styles: json_styles(workspace)
  }
}

pub fn from_canonical(workspace) {
  let records = canonical_elements(workspace);
  {
    workspace: [workspace.name, model.optional(workspace, "description")],
    elements: semantic_elements(records),
    relationships: canonical_relationships(workspace, records),
    views: canonical_views(workspace, records),
    styles: canonical_styles(workspace)
  }
}
