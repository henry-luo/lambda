// Face-less text queries (system fonts only) must share one cached query font
// context; test_radiant_text_query_gtest counts the context builds behind them.
import radiant

let font = {font_family: "sans-serif", font_size: 16}
fn advance(text, faces) {
    let metrics = radiant.measure_text([{text: text, font: font}], faces)
    if (metrics == null) -1.0 else metrics[0].advance
}
// charts pass null; an empty face list names the same system-font context
let null_faces = [for (i in 1 to 12) advance("query " ++ string(i), null)]
let empty_faces = [for (i in 1 to 12) advance("query " ++ string(i), [])]
let measured = len([for (width in null_faces ++ empty_faces where width > 0) width])
let agree = null_faces == empty_faces;

<html <body <div id:"result", ["data-measured"]: string(measured), ["data-agree"]: string(agree)>>>
