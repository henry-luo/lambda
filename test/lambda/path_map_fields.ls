// IL2-I20: inferred any fields must decode stored paths (D2.6.1v3).
fn record(value: any) => {label: string(value), target: value}
fn attribute(value: any) => <entry target: value>
fn lookup(value, key) => value[key]

let path = \.test.input.dir.'test.txt'
let direct = {target: path}
let dynamic = record(path)
let node = attribute(path)
let records = [for (entry in \.test.input.dir.**) {label: string(entry), target: entry}]
{
  direct: [type(direct.target), direct.target == path],
  dynamic: [type(dynamic.target), dynamic.target == path, exists(dynamic.target)],
  lookup: [type(lookup(dynamic, "target")), lookup(dynamic, "target") == path],
  attribute: [type(node.target), node.target == path],
  iterated: [
    len(records) > 0,
    all([for (entry in records) type(entry.target) == type(path)]),
    all([for (entry in records) entry.label == string(entry.target)]),
    all([for (entry in records) exists(entry.target)])
  ]
}
