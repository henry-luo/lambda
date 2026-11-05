import math: lambda.editor.math.render

let p = {source:"x+1",size:22,color:"#24334b",display:true,justify:"center",linear:false,number:""}
let rendered = math.render(p,650,1000,"Slides")^
let limited = math.render(p,1,1000,"Slides") ^ { ^ }
let invalid = math.render({*:p,size:2},650,1000,"Slides") ^ { ^ };
[
    rendered.width>1 and rendered.height>1,
    name(rendered.element)=='svg',
    match (limited) { case error: contains(limited.message,"Slides: equation is too large") default: false },
    match (invalid) { case error: contains(invalid.message,"Slides:") default: false }
]
