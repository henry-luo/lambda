import sh: lambda.shell

pn main() {
    sh.stdin() |> sh.grep("-E", "^ERROR ")
               |> sh.awk("{print $2}")
               |> sh.sort |> sh.uniq(count: true)
               |> sh.cmd("sort", ["-nr"]) |> sh.head(lines: 2)
               |> sh.awk("{print $2, $1}")
}
