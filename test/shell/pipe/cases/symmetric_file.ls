import sh: lambda.shell

pn select_items(source) {
    source |> sh.cut("-d,", "-f2")
           |: contains(~, "pl") |> upper(~)
           |> sh.sort |> sh.uniq
}
pn main() {
    \.'catalog.csv' |> select_items |> sh.head(lines: 2) |> \.'selected items.txt'
}
