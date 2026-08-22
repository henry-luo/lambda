// Lambda path v2 syntax and occurrence navigation regression coverage.
let data = [[1, 2], [3, 4]]
let indexed = [10, 20]
let record = {name: 30};
[
  /.a.b,
  .a.b,
  .~~.a,
  file./,
  file./.a,
  file.'other-host'.a,
  http.hostname.a,
  .a.1,
  .a.'1',
  indexed.1,
  record.'name',
  record./.name,
  /.a.b == /.a.b,
  /.a.b == file./.a.b,
  len(data |> ~~),
  len(data |> ~.~~),
  len(data |> ~./)
]
