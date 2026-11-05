import shapes: lambda.editor.graphics.shapes
import dimensions: lambda.editor.graphics.dimensions

let chosen = shapes.parameters(shapes.defaults,dimensions.office,"Slides")^
let strict = {*:dimensions.office,max_width:100}
let rejected = shapes.parameters(shapes.defaults,strict,"Slides") ^ { ^ };
[
    chosen.kind=="rect" and chosen.width==160,
    name(shapes.graphic(chosen,"test-"))=='svg',
    match (rejected) { case error: contains(rejected.message,"Slides:") default: false }
]
