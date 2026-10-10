import ui: lambda.ui.dtna
let pager = ui.pagination({id:"advanced-pages",total:1000,default_current:10,show_size_changer:true,show_quick_jumper:true,page_size_options:[10,20,50]})^
let fixed = ui.pagination({id:"fixed-pages",total:1000,current:4,page_size:10,show_size_changer:true})^
let chinese = ui.pagination({id:"chinese-pages",total:100,locale:'zh-CN',show_quick_jumper:true})^
let disabled = ui.pagination({id:"disabled-pages",total:100,disabled:true,show_size_changer:true,show_quick_jumper:true})^
let flow = ui.steps({id:"step-flow",clickable:true,items:[{title:"Details",description:"Enter details"},
    {title:"Unavailable",disabled:true},{title:"Review",subtitle:"Last step"}]})^
let fixed_flow = ui.steps({id:"fixed-flow",clickable:true,current:0,status:'error',items:[{title:"First"},{title:"Second"}]})^
let group = ui.button_group({id:"locked-group",disabled:true},ui.button({id:"locked-button"},"Locked")^)^
let enabled = ui.button_group({id:"enabled-group"},ui.button({id:"enabled-button"},"Enabled")^)^
view dtna_foundation_navigation: <foundation_navigation> state last_request:"", activations:0 {
    <div *[ui.render(pager),ui.render(fixed),ui.render(chinese),ui.render(disabled),ui.render(flow),ui.render(fixed_flow),
        ui.render(group),ui.render(enabled),<span id:"last-request",last_request>,<span id:"activation-count",string(activations)>]>
}
on ui_change(action) {
    last_request = string(action.id) ++ ":" ++ (if (action.action == 'page') string(action.value.current) ++ ":" ++ string(action.value.page_size) else string(action.value))
}
on ui_action(action) { if (action.id == "enabled-button" or action.id == "locked-button") { activations = activations + 1 } }
ui.page(<foundation_navigation>)^
