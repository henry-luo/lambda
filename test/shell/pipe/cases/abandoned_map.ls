import sh: lambda.shell

pn seen(line) { print("forced"); line }
pn main() {
    let pending = sh.yes(null, "test") |> seen(~);
    7
}
