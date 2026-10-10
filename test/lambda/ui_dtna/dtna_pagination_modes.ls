import ui: lambda.ui.dtna
let simple = ui.render(ui.pagination({id:"simple",total:100,default_current:3,simple:true})^)
let readonly = ui.render(ui.pagination({id:"readonly",total:100,current:8,simple:{read_only:true}})^)
let summary = ui.render(ui.pagination({total:52,current:6,show_total:(total,bounds)=>string(bounds[0]) ++ "–" ++ string(bounds[1]) ++ " of " ++ string(total)})^)
let empty = ui.render(ui.pagination({total:0,show_total:(total,bounds)=>string(bounds[0]) ++ "/" ++ string(bounds[1])})^);
[
    ["simple control",len(content(simple)) == 3 and name(content(content(simple)[1])[0]) == 'input' and content(content(simple)[1])[0].value == "3"],
    ["read only simple",name(content(content(readonly)[1])[0]) == 'span' and content(content(content(readonly)[1])[0])[0] == "8"],
    ["small mode",contains(ui.render(ui.pagination({size:'small'})^).class,"dtna-size-small")],
    ["last page range",content(content(summary)[0])[0] == "51–52 of 52"],
    ["zero range",content(content(empty)[0])[0] == "0/0"],
    ["localized total",content(content(ui.render(ui.pagination({total:52,show_total:true,locale:'zh-CN'})^))[0])[0] == "共 52 条"],
    ["single page hidden",ui.render(ui.pagination({total:10,hide_on_single_page:true})^) == null and ui.render(ui.pagination({total:0,hide_on_single_page:true})^) == null],
    ["multiple pages visible",name(ui.render(ui.pagination({total:11,hide_on_single_page:true})^)) == 'nav'],
    ["invalid simple",(ui.pagination({simple:1}) or null) == null and (ui.pagination({simple:{read_only:1}}) or null) == null and (ui.pagination({simple:{unknown:true}}) or null) == null],
    ["invalid total option",(ui.pagination({show_total:"Total"}) or null) == null and (ui.pagination({hide_on_single_page:1}) or null) == null]
]
