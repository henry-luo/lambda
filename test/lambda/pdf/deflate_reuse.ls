import radiant

pn main() {
    let text = [for (i in range(0, 256, 1)) "retained PDF resource " ++ string(i)] |> join(" ");
    let first = radiant.pdf_deflate(binary(text))^;
    let other = radiant.pdf_deflate(binary(text ++ " changed"))^;
    let repeated = radiant.pdf_deflate(binary(text))^;
    let invalid = radiant.pdf_deflate(text) ^ {true};
    return [first == repeated, first != other, len(first) < len(text), invalid == true]
}
