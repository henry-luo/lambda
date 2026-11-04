import diagram: lambda.editor.graphics.diagram
import wordart: lambda.editor.graphics.wordart
import dimensions: lambda.editor.graphics.dimensions

let d = diagram.parameters(diagram.defaults,dimensions.office,"Slides")^
let d_limited = diagram.parameters(d,{*:dimensions.office,max_width:100},"Slides") ^ { ^ }
let w = wordart.parameters(wordart.defaults,wordart.office_limits,"Slides")^
let w_limited = wordart.parameters(w,{*:wordart.office_limits,max_width:100},"Slides") ^ { ^ };
[
    len(diagram.labels(d))==3 and name(diagram.graphic(d))=='svg',
    name(wordart.graphic(w,"test-"))=='svg',
    match (d_limited) { case error: contains(d_limited.message,"Slides:") default: false },
    match (w_limited) { case error: contains(w_limited.message,"Slides:") default: false }
]
