// Pure Structurizr parent relationship implication with stable provenance.

fn element_by_id(elements, identifier) {
  let found = [for (value in elements where value.id == identifier) value];
  if (len(found) > 0) found[0] else null
}

fn chain_of(elements, item_id, stack) {
  if (item_id == null or contains(stack, item_id)) slice(stack, 0, 0)
  else {
    let value = element_by_id(elements, item_id);
    if (value == null) slice(stack, 0, 0) else [item_id,
      *chain_of(elements, value.parent, [*stack, item_id])]
  }
}

// A shared ancestor is a boundary, not another endpoint combination.
fn branch(chain, other) {
  let shared = [for (i, id in chain where contains(other, id)) i];
  slice(chain, 0, if (len(shared) == 0) len(chain) else shared[0])
}

fn logical_kind(kind) => contains([
  "person", "software-system", "container", "component", "custom"
], string(kind))

fn candidates(elements, relationship) {
  let source_value = element_by_id(elements, relationship.from);
  let destination_value = element_by_id(elements, relationship.to);
  // Structurizr implies relationships only through logical-model ancestry;
  // deployment nodes and instances use explicit or lifted relationships.
  if (source_value == null or destination_value == null or
      not logical_kind(source_value.kind) or not logical_kind(destination_value.kind)) { [] }
  else {
    let sources = chain_of(elements, relationship.from, []);
    let destinations = chain_of(elements, relationship.to, []);
    let source_branch = branch(sources, destinations);
    let destination_branch = branch(destinations, sources);
    [for (source in source_branch)
      for (destination in destination_branch
      where source != destination and
        not (source == relationship.from and destination == relationship.to))
      {source: source, destination: destination}]
  }
}

fn exists(values, candidate) => len([
  for (value in values
    where value.from == candidate.source and value.to == candidate.destination) value
]) > 0

fn append_candidates(values, candidates, index, origin) {
  if (index >= len(candidates)) values
  else {
    let candidate = candidates[index];
    let next = if (exists(values, candidate)) values else [*values, {
      *:origin,
      id: "implied:" ++ origin.id ++ ":" ++ candidate.source ++ ":" ++ candidate.destination,
      from: candidate.source, to: candidate.destination,
      implied: true, implied_from: origin.id
    }];
    append_candidates(next, candidates, index + 1, origin)
  }
}

fn expand_at(elements, explicit, index, values) {
  if (index >= len(explicit)) values
  else expand_at(elements, explicit, index + 1,
    append_candidates(values, candidates(elements, explicit[index]), 0, explicit[index]))
}

pub fn expand(elements, relationships, enabled = true) =>
  if (enabled) expand_at(elements, relationships, 0, relationships) else relationships
