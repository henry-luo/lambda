import charts: lambda.editor.graphics.chart
import dimensions: lambda.editor.graphics.dimensions

let data = charts.checked(charts.defaults,dimensions.office,"Slides")^
let strict = {*:dimensions.office,max_width:100}
let rejected = charts.checked(charts.defaults,strict,"Slides") ^ { ^ }
let duplicate = charts.rows("A, 2\nA, 3","Slides") ^ { ^ };
[
    len(data)==4 and data[0].label=="Design" and data[0].value==12,
    match (rejected) { case error: contains(rejected.message,"Slides:") default: false },
    match (duplicate) { case error: contains(duplicate.message,"Slides:") default: false },
    name(charts.graphic(charts.defaults,dimensions.office,"Slides")^)=='svg'
]
