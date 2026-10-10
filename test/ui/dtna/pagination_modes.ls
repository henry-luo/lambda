import ui: lambda.ui.dtna
let editable = <dtna.pagination id:"simple-pages",total:1000,default_current:3,simple:true,show_size_changer:true>
let fixed = <dtna.pagination id:"fixed-simple",total:100,current:4,simple:true>
let readonly = <dtna.pagination id:"readonly-pages",total:100,default_current:8,simple:{read_only:true}>
let small = <dtna.pagination id:"small-pages",total:52,size:'small',show_total:(total,bounds)=>string(bounds[0]) ++ "–" ++ string(bounds[1]) ++ " of " ++ string(total)>
let empty = <dtna.pagination id:"empty-pages",total:0,show_total:(total,bounds)=>string(bounds[0]) ++ "/" ++ string(bounds[1])>
let single = <dtna.pagination id:"single-page",total:10,hide_on_single_page:true>
view pagination_mode_demo: <pagination_mode_demo> state last_request:"" {
    <main *[apply(editable),apply(fixed),apply(readonly),apply(small),apply(empty),apply(single),
        <output id:"last-request",last_request>,<button id:"blur-target",type:"button","Blur">]>
}
on ui_change(action) {
    last_request = string(action.id) ++ ":" ++ string(action.value.current) ++ ":" ++ string(action.value.page_size)
}
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <pagination_mode_demo>>)
