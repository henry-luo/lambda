import ui: lambda.ui.dtna
let clamped = ui.render(ui.progress({percent:125})^)
let negative = ui.render(ui.progress({percent:-4,show_info:false})^)
let steps = ui.render(ui.progress({percent:60,steps:5,stroke_color:["red","orange","green"]})^)
let rounded = ui.render(ui.progress({percent:11,steps:5,rounding:(value)=>floor(value)})^)
let circle = ui.render(ui.progress({type:'circle',percent:40,dimension:80,success:{percent:15}})^)
let dashboard = ui.render(ui.progress({type:'dashboard',percent:70,gap_degree:90,gap_placement:'top'})^)
let custom = ui.render(ui.progress({percent:45,success:{percent:10},format:(value,success) => string(value) ++ "/" ++ string(success)})^)
let gradient = ui.render(ui.progress({id:"gradient",percent:50,stroke_color:{from:"red",to:"blue"}})^)
let gradient_circle = ui.render(ui.progress({id:"gradient-circle",type:'circle',percent:75,stroke_color:{["100%"]:"blue",["0%"]:"red",["50%"]:"green"}})^)
let arc = content(content(circle)[0])[1];
[
    ["percent clamp",clamped["aria-valuenow"] == "100" and negative["aria-valuenow"] == "0"],
    ["completion semantics",contains(clamped.class,"dtna-progress-phase-success") and content(content(clamped)[1])[0]["aria-hidden"] == "true"],
    ["hidden info",len(content(negative)) == 1],
    ["colored discrete steps",len(content(content(steps)[0])) == 5 and contains(content(content(steps)[0])[2].style,"green") and contains(content(content(steps)[0])[3].style,"disabled-background")],
    ["custom step rounding",contains(content(content(rounded)[0])[0].style,"disabled-background")],
    ["circle dimensions",name(content(circle)[0]) == 'svg' and content(circle)[0].width == "80" and arc.r == "47"],
    ["success segment",len(content(content(circle)[0])) == 3 and content(content(circle)[0])[2].class == "dtna-progress-success"],
    ["dashboard gap",contains(content(content(dashboard)[0])[0].transform,"rotate(-45 ")],
    ["custom label",content(content(custom)[1])[0] == "45/10"],
    ["invalid progress",(ui.progress({percent:nan}) or null) == null and (ui.progress({steps:-1}) or null) == null and (ui.progress({type:'pie'}) or null) == null],
    ["invalid stroke",(ui.progress({stroke_color:[]}) or null) == null and (ui.progress({stroke_color:"red;bad"}) or null) == null and (ui.progress({stroke_width:0}) or null) == null],
    ["invalid nested options",(ui.progress({success:{percent:inf}}) or null) == null and (ui.progress({steps:{count:5,gap:-1}}) or null) == null and (ui.progress({steps:{count:5,bad:true}}) or null) == null],
    ["invalid callbacks",(ui.progress({format:1}) or null) == null and (ui.progress({rounding:1}) or null) == null and (ui.progress({steps:5,rounding:(value)=>nan}) or null) == null],
    ["invalid gap",(ui.progress({gap_degree:360}) or null) == null and (ui.progress({gap_placement:'left'}) or null) == null],
    ["line gradient",contains(content(content(gradient)[0])[0].style,"linear-gradient(to right,red 0%,blue 100%)")],
    ["sorted gradient stops",contains(content(content(content(gradient_circle)[0])[2])[0].style,"red 0%,green 50%,blue 100%")],
    ["ring gradient reference",content(content(gradient_circle)[0])[2].mask == "url(#gradient-circle-gradient-conic)"],
    ["invalid gradients",(ui.progress({stroke_color:{from:"red",to:"blue"}}) or null) == null and (ui.progress({id:"x",stroke_color:{from:"red"}}) or null) == null and (ui.progress({id:"x",stroke_color:{["bad"]:"red",["100%"]:"blue"}}) or null) == null]
]
