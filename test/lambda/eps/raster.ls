import eps: lambda.doc.eps.eps
import helpers: .mod_helpers

pn main() {
    let hex = eps.interpret(input("test/input/eps/raster-hex.eps", 'eps')^)^
    let planar = eps.interpret(input("test/input/eps/raster-planar.eps", 'eps')^)^
    let malformed = helpers.diagnose("2 1 8 [2 0 0 -1 0 1] {()} false 3 colorimage")^
    let limit = helpers.diagnose("2 1 8 [2 0 0 -1 0 1] (abcdef) false 3 colorimage", {limits:{max_image_pixels:1}})^
    let bad_filter = helpers.diagnose("(zzzz~>) /ASCII85Decode filter", {limits:{max_decoded_bytes:4}})^
    let files = eps.interpret(helpers.doc("(ff00>) /ASCIIHexDecode filter dup 2 string readstring pop <ff00> eq exch dup status exch closefile\n" ++
        "(00>) /ASCIIHexDecode filter dup flushfile dup read not exch status\n" ++
        "{currentfile dup save exch 1 string readstring pop pop restore 1 string readstring pop <62> eq} exec\nab")^)^
    let closed = helpers.diagnose("(ff>) /ASCIIHexDecode filter dup closefile token")^
    print({hex_draws:len(hex.draws), planar_draws:len(planar.draws), resumed:all(hex.stack),
        embedded:contains(hex.draws[0].node.href, "data:image/png;base64,"),
        planar_embedded:contains(planar.draws[0].node.href, "data:image/png;base64,"),
        empty:contains(malformed,"ioerror"), pixel_limit:contains(limit,"limitcheck"),
        filter_limit:contains(bad_filter,"ioerror"), files:all(files.stack), closed:contains(closed,"invalidaccess")})
}
