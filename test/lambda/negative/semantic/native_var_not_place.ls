// D7.6.3: only a binding or a member/index place of one has a home for a native `var` argument.
import paint: lambda.paint
import radiant
pn main() {
    radiant.paint_stroke(paint.layer(8, 8)^, [], null)
}
