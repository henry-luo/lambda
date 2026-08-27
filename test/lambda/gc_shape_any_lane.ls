// A map field whose declared type is a `type` contract — a union, `T?`, an
// occurrence or a constrained type — is stored in the packed 9-byte `any`
// lane, not as a raw container pointer. When such a field is the only
// reference to its container, the collector must decode that lane to reach it.
// Reading the lane as an 8-byte pointer collected the live container, and the
// damage only surfaced later as a shortened or emptied collection.

fn node_at(nodes, id) {
  let matches = [for (entry in nodes where entry.id == id) entry];
  if (len(matches) > 0) matches[0] else null
}

fn replace_node(nodes, value) => [for (entry in nodes) if (entry.id == value.id) value else entry]

// The `nodes` field takes its type from this function's inferred result, which
// is a contract rather than a simple array type — that is what selects the
// `any` lane for the field.
fn ensure(st, id, properties) {
  let current = node_at(st.nodes, id);
  if (current == null) { {*: st, nodes: [*st.nodes, {id: id, properties: properties}]} }
  else { {*: st, nodes: replace_node(st.nodes,
    {*: current, properties: [*current.properties, *properties]})} }
}

fn walk(st, i, n) =>
  if (i >= n) st else walk(ensure(st, "n" ++ string(i % 3), [i]), i + 1, n)

// Unrelated churn so an ordinary run also allocates across the same fields.
fn churn(n) => [for (i in 1 to n) {index: i, label: string(i)}]

let built = walk({nodes: []}, 0, 60)
let noise = churn(200)
{
  nodes: len(built.nodes),
  first_id: built.nodes[0].id,
  first_properties: len(built.nodes[0].properties),
  last_property: built.nodes[2].properties[19],
  noise: len(noise)
}
