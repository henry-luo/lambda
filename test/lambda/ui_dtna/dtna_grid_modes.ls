import ui: lambda.ui.dtna
import c: lambda.ui.core.component
import responsive: lambda.ui.core.responsive
let row = ui.render(ui.row({gutter:[{xs:"1rem",md:"24px"},"2em"],align:{xs:'top',md:'middle'},justify:{xs:'start',lg:'end'}},null)^)
let col = ui.render(ui.col({span:8,push:4,pull:0,md:{push:0,pull:2,flex:2}},"Cell")^)
let flexible = ui.render(ui.col({flex:"120px"},null)^)
let space = ui.render(ui.space({gap:["1rem","2em"]},["One","Two"])^)
let flex = ui.render(ui.flex({flex:2,gap:"8px"},"Child")^);
[
    ["string and responsive gutters",contains(row.style,"--dtna-row-xs-x:1rem;") and contains(row.style,"--dtna-row-base-y:2em;")],
    ["responsive alignment",contains(row.class,"dtna-row-align-md") and contains(row.style,"--dtna-row-align-md:center;") and contains(row.style,"--dtna-row-justify-lg:flex-end;")],
    ["responsive inheritance",responsive.at({xs:'top',lg:'bottom'},"md",null) == 'top' and responsive.at({md:'middle'},"xs",'top') == 'top'],
    ["logical push pull and reset",contains(col.style,"--dtna-col-base-push:16.6667%;") and contains(col.style,"--dtna-col-md-push:auto;") and contains(col.class,"dtna-col-md-pull")],
    ["responsive flex",contains(col.style,"--dtna-col-md-flex:2 2 auto;") and contains(col.class,"dtna-col-md-flex")],
    ["fixed flex basis",contains(flexible.style,"flex:0 0 120px;")],
    ["auto flex basis",contains(ui.render(ui.col({flex:"auto"})^).style,"flex:1 1 auto;")],
    ["explicit flex shorthand",contains(ui.render(ui.col({flex:"1 0 200px"})^).style,"flex:1 0 200px;")],
    ["spacing lengths",contains(space.style,"gap:2em 1rem;") and contains(flex.style,"flex:2 2 auto;")],
    ["nonnegative CSS lengths",all([for (value in [0,"0",".5rem","1e2px","12%","2VH","0.5in"]) c.css_length(value)])],
    ["reject invalid CSS lengths",all([for (value in [null,true,-1,"-1px","red","1garbagepx","1e999px","2px;color:red","calc(1rem + 2px)"]) not c.css_length(value)])],
    ["column shift bounds",(ui.col({push:25}) or null) == null and (ui.col({pull:-1}) or null) == null and (ui.col({md:{push:1.5}}) or null) == null],
    ["flex validation",all([for (value in [-1,inf,"1garbage","-1 0 auto","1 0 red","1 0 0 0",true]) (ui.col({flex:value}) or null) == null])],
    ["alignment validation",(ui.row({align:{md:'missing'}}) or null) == null and (ui.row({justify:{tablet:'center'}}) or null) == null and (ui.row({align:{}}) or null) == null],
    ["empty and invalid gutters",(ui.row({gutter:{}}) or null) == null and (ui.row({gutter:"red"}) or null) == null and (ui.row({gutter:{md:"-1rem"}}) or null) == null],
    ["skeleton dimension validation",(ui.skeleton_input({width:"red"}) or null) == null and (ui.skeleton({paragraph:{width:["100%","-1px"]}}) or null) == null],
    ["icon dimension validation",(ui.icon({width:"red"}) or null) == null and (ui.icon({height:true}) or null) == null],
    ["zero and fractional flex",contains(ui.render(ui.col({flex:0})^).style,"flex:0 0 auto;") and contains(ui.render(ui.col({flex:0.5})^).style,"flex:0.5 0.5 auto;")]
]
