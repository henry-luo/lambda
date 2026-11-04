// Shared chart source validation and SVG construction; callers supply insertion bounds.
import chart: lambda.chart.chart
import colors: lambda.ui.core.component
import geom: lambda.editor.draw.geometry
import dimensions: .dimensions

pub let types = [{id:"column",label:"Column",icon:"chart"},{id:"bar",label:"Bar",icon:"chart_bar"},
    {id:"line",label:"Line",icon:"chart_line"},{id:"pie",label:"Pie",icon:"chart_pie"},{id:"doughnut",label:"Doughnut",icon:"chart_doughnut"}]
pub let defaults = {type:"column",title:"Studio activity",data:"Design, 12\nPrint, 18\nBuild, 9\nShare, 15",color:"#2563eb",width:520,height:320,rotation:0}
pub fn rows(source,owner="Chart") array^ {
    if (not (source is string) or len(source)>4000) raise error(owner ++ ": enter chart data as Label, Value, one per line") else {
        let lines = [for (line in ((split(source,"\n")) ^ { [] }) where trim(line)!="") split(line,",")]
        let records = [for (line in lines) {label:trim(line[0]),value:float(trim(line[1])) ^ { null }}]
        if (len(lines)<1 or len(lines)>20 or any([for (line in lines) len(line)!=2]) or
            any([for (row in records) row.label=="" or len(split(row.label,""))>40 or not geom.finite_number(row.value)]) or
            len(unique([for (row in records) row.label]))!=len(records))
            raise error(owner ++ ": enter 1–20 unique labels with finite values, as Label, Value on each line")
        else records
    }
}
fn valid(p,limits) any^ => contains([for (entry in types) entry.id],p.type) and colors.hex_color(p.color) and
    p.title is string and len(split(p.title,""))<=100 and dimensions.valid(p,limits)
pub fn checked(p,limits,owner="Chart") array^ {
    if (not valid(p,limits)) raise error(owner ++ ": choose a chart type, title up to 100 characters, a #RRGGBB color, and valid dimensions/rotation") else {
        let records = rows(p.data,owner)^
        if (contains(["pie","doughnut"],p.type) and (any([for (r in records) r.value<0]) or sum([for (r in records) r.value])<=0))
            raise error(owner ++ ": pie and doughnut values must be nonnegative with a positive total") else records
    }
}
pub fn graphic(p,limits,owner="Chart") element^ {
    let data = checked(p,limits,owner)^
    let circular = contains(["pie","doughnut"],p.type)
    let category = {field:"label",dtype:"nominal",title:"",sort:null}
    let quantity = {field:"value",dtype:"quantitative",title:"Value",scale:{zero:true}}
    let rendered = chart.render({width:520,height:320,padding:30,title:p.title,data:data,
        mark:{kind:if (circular) "arc" else if (p.type=="line") "line" else "bar",color:p.color,
            *:if (circular) {inner_radius:if (p.type=="doughnut") 60 else 0} else {}},
        encoding:if (circular) {theta:quantity,
            color:{*:category,scale:{range:[p.color,"#60a5fa","#a78bfa","#f59e0b","#f472b6","#38bdf8"]}}}
            else {x:if (p.type=="bar") quantity else category,y:if (p.type=="bar") category else quantity},
        config:{font:"Arial",background:"#ffffff",axis_label_color:"#475569",axis_grid_color:"#e2e8f0"}})^;
    // A fixed plotting canvas preserves labels and axes while image handles scale the vector artwork.
    <svg xmlns:"http://www.w3.org/2000/svg",width:p.width,height:p.height,viewBox:"0 0 520 320",*rendered>
}
