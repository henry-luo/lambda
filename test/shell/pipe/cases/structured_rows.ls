import sh: lambda.shell

pn main() {
    let rows = [
        {service: "Worker Pool", active: true},
        {service: "Database", active: true},
        {service: "Retired", active: false},
        {service: "Database", active: true}
    ]
    rows |: ~.active |> ~.service |> sh.sort |> sh.uniq
         |> sh.tr(" ", "_")
         |> {name: lower(~), index: ~key}
         |: ~.index > 0 |> ~.name
         |> sh.sed("s/^/service=/")
}
