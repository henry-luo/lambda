import sh: lambda.shell

pn main() {
    sh.cut(null, "-d,", "-f1,2", "input.csv")
        |: starts_with(~, "ERROR,")
        |> upper(split(~, ",")[1])
}
