import ui: lambda.ui.dtna
let editable = ui.pagination({id:"simple-pages",total:1000,default_current:3,simple:true,show_size_changer:true})^
let fixed = ui.pagination({id:"fixed-simple",total:100,current:4,simple:true})^
let readonly = ui.pagination({id:"readonly-pages",total:100,default_current:8,simple:{read_only:true}})^
let small = ui.pagination({id:"small-pages",total:52,size:'small',show_total:(total,bounds)=>string(bounds[0]) ++ "–" ++ string(bounds[1]) ++ " of " ++ string(total)})^
let empty = ui.pagination({id:"empty-pages",total:0,show_total:(total,bounds)=>string(bounds[0]) ++ "/" ++ string(bounds[1])})^
let single = ui.pagination({id:"single-page",total:10,hide_on_single_page:true})^
view pagination_mode_demo: <pagination_mode_demo> state last_request:"" {
    <main *[ui.render(editable),ui.render(fixed),ui.render(readonly),ui.render(small),ui.render(empty),ui.render(single),
        <output id:"last-request",last_request>,<button id:"blur-target",type:"button","Blur">]>
}
on ui_change(action) {
    last_request = string(action.id) ++ ":" ++ string(action.value.current) ++ ":" ++ string(action.value.page_size)
}
ui.page(<pagination_mode_demo>,{tokens:{font_family:"Liberation Sans"}})^
