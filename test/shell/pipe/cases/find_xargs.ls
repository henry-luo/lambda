import sh: lambda.shell

pn main() {
    sh.find(null, "find input", "-type", "f", "-name", "*.txt", "-print0")
        |> sh.xargs("-0", "printf", "%s\n")
        |> sh.sort
}
