import eps: lambda.doc.eps.eps

pn main() {
    let doc = input("test/input/eps/vector.eps")^
    let display = eps.interpret(doc)^
    let picture = eps.display_to_svg(display, {id_prefix: "test"})^
    let xml = format(picture, 'xml')^
    print({format: doc.format, bbox: display.bbox, draws: len(display.draws), clips: len(display.definitions),
        viewport: picture.viewBox, width: picture.width, height: picture.height,
        flipped: contains(xml, "matrix(1 0 0 -1 10 65)"),
        red: contains(xml, "rgb(255,0,0)"), blue: contains(xml, "rgb(0,0,255)"),
        clipped: contains(xml, "url(#test-clip-0)")})
}
