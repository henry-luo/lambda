// Pure declarative Structurizr archetype inheritance and default merging.

import diagnostic: lambda.package.graph.diagnostics

fn first(values) => if (len(values) > 0) values[0] else null

pub fn find(values, name, target_kind = null) => first([
  for (value in values where value.valid and value.name == name and
    (target_kind == null or value.target_kind == target_kind)) value
])

fn merge_named(base, own) => [
  for (entry in base where len([for (value in own where value.name == entry.name) value]) == 0)
    entry,
  *own
]

fn chosen(base, own, name) => if (own[name] != null) own[name] else base[name]

pub fn merge(base, own) => {
  *:base, *:own,
  description: chosen(base, own, "description"),
  technology: chosen(base, own, "technology"),
  metadata: chosen(base, own, "metadata"),
  tags: unique([*base.tags, *own.tags]),
  properties: merge_named(base.properties, own.properties),
  perspectives: merge_named(base.perspectives, own.perspectives)
}

fn builtin(base, target_kind) {
  let element_kinds = ["person", "softwareSystem", "container", "component", "element",
    "deploymentNode", "infrastructureNode"];
  if (target_kind == "relationship" and base == "relationship") {
    {name: base, target_kind: target_kind, kind: "relationship", valid: true,
      description: null, technology: null, metadata: null,
      tags: [], properties: [], perspectives: [], source: null}
  } else if (target_kind == "element" and contains(element_kinds, base)) {
    {name: base, target_kind: target_kind, kind: base, valid: true,
      description: null, technology: null, metadata: null,
      tags: [], properties: [], perspectives: [], source: null}
  } else { null }
}

fn problem(code, message, definition) => diagnostic.for_value(code, "error", message,
  "archetype:" ++ definition.name, definition.source)

fn resolve_one(definitions, definition, stack) {
  if (contains(stack, definition.name)) {
    {value: {*:definition, kind: null, valid: false}, diagnostics: [
      problem("structurizr.archetype-cycle",
        "Archetype inheritance cycle includes '" ++ definition.name ++ "'", definition)
    ]}
  } else {
    let direct = builtin(definition.base, definition.target_kind);
    let inherited = if (direct != null) { {value: direct, diagnostics: []} } else {
      let matches = [for (value in definitions where value.name == definition.base) value];
      if (len(matches) == 0) {
        {value: null, diagnostics: [problem("structurizr.unknown-archetype",
          "Unknown archetype base '" ++ definition.base ++ "'", definition)]}
      } else if (matches[0].target_kind != definition.target_kind) {
        {value: null, diagnostics: [problem("structurizr.archetype-kind",
          "Archetype '" ++ definition.name ++ "' cannot inherit from a different kind",
          definition)]}
      } else {
        let resolved = resolve_one(definitions, matches[0], [*stack, definition.name]);
        {*:resolved, value: if (resolved.value == null) null else
          {*:resolved.value, tags: unique([*resolved.value.tags, matches[0].name])}}
      }
    };
    {value: if (inherited.value == null) {*:definition, kind: null, valid: false}
      else {*:merge(inherited.value, definition), kind: inherited.value.kind, valid: true},
      diagnostics: inherited.diagnostics}
  }
}

pub fn resolve(definitions) {
  let results = [for (definition in definitions) resolve_one(definitions, definition, [])];
  {values: [for (result in results) result.value],
    diagnostics: [for (result in results) for (value in result.diagnostics) value]}
}
