import sh: lambda.shell

pn filter_lines(source, pattern: string, suffix: string) {
    source |: contains(~, pattern) |> upper(~) ++ suffix
}
pn main() {
    sh.stdin() |> sh.cat() |> filter_lines("a", "!") |> sh.sort
}
