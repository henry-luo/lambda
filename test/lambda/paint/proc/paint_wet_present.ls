// Wet presentation keeps both owners and agrees before and after dry transfer (PE8, PE9, S9.1.2).
import paint: lambda.paint
import wc: lambda.procart.watercolor
import radiant
let Sheet = wc.Sheet

pn main() {
    var reference: Sheet = wc.sheet(16, 16, 2, 2.5, 0.0)^
    wc.stamp(reference, {kind:"pigment", channel:1, x:8.0, y:8.0, radius:4.0, strength:0.7})^
    var sheet = paint.wet_from_reference(reference)^
    var layer = paint.layer(16, 16, {format:'pigment'})^
    let empty = paint.wet_sheet(16, 16, 2)^
    let blank = paint.wet_present(empty, layer, wc.CLASSIC)^
    let ordinary = paint.present(layer)^
    let old_sheet = sheet, old_layer = layer
    let image = paint.wet_present(sheet, layer, wc.CLASSIC)^
    let part = paint.wet_present(sheet, layer, wc.CLASSIC, [6,6,10,10])^
    let narrow = {*:sheet, cells:radiant.paint_wet_cells(sheet.cells)^}
    let narrow_image = paint.wet_present(narrow, layer, wc.CLASSIC)^
    let wrong = paint.wet_present(sheet, layer, [wc.CLASSIC[0]]) ^ { ^ }
    let outside = paint.wet_present(sheet, layer, wc.CLASSIC, [-1,0,2,2]) ^ { ^ }
    wc.dry(reference)^
    var dried = paint.wet_from_reference(reference)^
    let before_transfer = paint.wet_present(dried, layer, wc.CLASSIC)^
    paint.wet_deposit(layer, dried, wc.CLASSIC)^
    let after_transfer = paint.wet_present(dried, layer, wc.CLASSIC)^
    return {empty:blank == ordinary, painted:image[8,8] != blank[8,8],
        crop:shape(part) == [4,4,4] and part[2,2] == image[8,8], lanes:narrow_image == image,
        pure:sheet == old_sheet and old_layer == paint.layer(16,16,{format:'pigment'})^,
        transfer:before_transfer == after_transfer, invalid:wrong is error and outside is error}
}
