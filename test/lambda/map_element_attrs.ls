let source = <node id: "a", label: "Alpha", custom: 42; "old content">
let attrs = map(source)
let rebuilt = <node *:attrs; "new content">

{
  attrs: attrs,
  rebuilt_attrs: [rebuilt.id, rebuilt.label, rebuilt.custom],
  rebuilt_content: [for (i in 0 to (len(rebuilt) - 1)) rebuilt[i]]
}
