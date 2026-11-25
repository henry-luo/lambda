import sh: lambda.shell

pn main() {
    sh.printf(null, "%s\n", "two words", "literal;value")
        |> sh.grep("two words") |> sh.tr("a-z", "A-Z")
}
