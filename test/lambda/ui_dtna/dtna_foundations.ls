import ui: lambda.ui.dtna
import collection: lambda.ui.core.collection
let column = ui.render(ui.col({span:8,offset:4,order:-1,md:{span:12,offset:0},xxxl:6},"Cell")^)
let row = ui.render(ui.row({gutter:[{xs:8,md:24},16],wrap:false},ui.col()^)^)
let group = ui.render(ui.button_group({disabled:true,direction:'vertical'},ui.button({},"Save")^)^)
let spacer = ui.render(ui.space({separator:"/"},["One",null,"Two"])^)
let divider = ui.render(ui.divider({placement:'start',dashed:true,plain:true},"Title")^)
let pager = ui.render(ui.pagination({total:100000000,current:500000})^)
let steps = ui.render(ui.steps({id:"flow",clickable:true,status:'error',current:1,items:[{title:"One"},{title:"Two"}]})^);
[
    ["responsive column",contains(column.class,"dtna-col-md-span") and contains(column.class,"dtna-col-xxxl-span") and content(column)[0] == "Cell"],
    ["responsive two-axis gutters",contains(row.class,"dtna-row-md-x") and contains(row.style,"--dtna-row-base-y:16px;")],
    ["zero span",contains(ui.render(ui.col({span:0})^).style,"--dtna-col-base-display:none;")],
    ["compact group",name(group) == 'fieldset' and ("disabled" at map(group)) and contains(group.class,"dtna-compact-vertical")],
    ["space separators omit null slots",len(content(spacer)) == 3 and content(content(spacer)[1])[0] == "/"],
    ["labelled dashed divider",contains(divider.class,"dtna-divider-dashed") and len(content(divider)) == 3],
    ["bounded page window",collection.page_window(500000,1000000) == [1,499998,499999,500000,500001,500002,1000000] and collection.page_window(1,1) == [1] and collection.page_window(1,3) == [1,2,3]],
    ["bounded rendered pager",len([for (child in content(pager) where child is element and name(child) == 'button') child]) <= 11],
    ["step status",contains(content(steps)[1].class,"dtna-step-status-error")],
    ["column ranges",((ui.col({span:25}) ^ { null }) or null) == null and ((ui.col({offset:-1}) ^ { null }) or null) == null and ((ui.col({order:1.5}) ^ { null }) or null) == null],
    ["breakpoint validation",((ui.col({md:25}) ^ { null }) or null) == null and ((ui.col({xs:{other:2}}) ^ { null }) or null) == null and ((ui.col({xxxl:null}) ^ { null }) or null) == null],
    ["gutter shapes",((ui.row({gutter:[8]}) ^ { null }) or null) == null and ((ui.row({gutter:{tablet:8}}) ^ { null }) or null) == null and ((ui.row({gutter:[8,-1]}) ^ { null }) or null) == null],
    ["finite gutters",((ui.row({gutter:inf}) ^ { null }) or null) == null and ((ui.row({gutter:null}) ^ { null }) or null) == null],
    ["spacing validation",((ui.space({gap:[1,-1]}) ^ { null }) or null) == null and ((ui.flex({align:'missing'}) ^ { null }) or null) == null and ((ui.space_compact({block:"yes"}) ^ { null }) or null) == null],
    ["divider validation",((ui.divider({dashed:1}) ^ { null }) or null) == null and ((ui.divider({placement:'missing'}) ^ { null }) or null) == null and ((ui.divider({direction:'vertical'},"Label") ^ { null }) or null) == null],
    ["page ownership",((ui.pagination({current:1,default_current:2}) ^ { null }) or null) == null and ((ui.pagination({page_size:10,default_page_size:20}) ^ { null }) or null) == null],
    ["page size validation",((ui.pagination({page_size_options:[10,10]}) ^ { null }) or null) == null and ((ui.pagination({page_size_options:[0]}) ^ { null }) or null) == null and ((ui.pagination({show_quick_jumper:1}) ^ { null }) or null) == null],
    ["locale validation",((ui.pagination({locale:'missing'}) ^ { null }) or null) == null],
    ["step ownership",((ui.steps({clickable:true,items:[{title:"One"}]}) ^ { null }) or null) == null and ((ui.steps({current:0,default_current:0}) ^ { null }) or null) == null],
    ["step validation",((ui.steps({items:[{status:'missing'}]}) ^ { null }) or null) == null and ((ui.steps({items:[{disabled:1}]}) ^ { null }) or null) == null and ((ui.steps({items:[{title:"One"}],current:2}) ^ { null }) or null) == null and ((ui.steps(1) ^ { null }) or null) == null]
]
