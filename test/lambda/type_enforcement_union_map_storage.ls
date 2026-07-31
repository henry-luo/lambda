fn normalized_id(value) =>
  if (value != null) string(value) else ""

let entries = [
  {id: normalized_id("b")},
  {id: normalized_id("a")}
]

let sorted = sort(entries)
{
  ids: [for (entry in sorted) entry.id],
  equality: [for (entry in sorted) entry.id == "a"]
}
