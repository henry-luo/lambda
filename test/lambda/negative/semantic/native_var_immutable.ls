// D7.6.3 / S9.1.3v4: a native procedure's `var` argument must root at a mutable binding.
import paint: lambda.paint
import radiant
pn main() {
    let layer = paint.layer(8, 8)^
    radiant.paint_stroke(layer, [], null)
}
