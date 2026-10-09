import ui: lambda.ui.dtna
let sections = ui.tabs({id:"sections",items:[
    {key:'profile',label:"Profile",children:ui.input({id:"profile-name",default_value:"Ada"})^},
    {key:'disabled',label:"Disabled",disabled:true,children:"Unavailable"},
    {key:'settings',label:"Settings",children:"Preferences"}
]})^
let period = ui.segmented({id:"period",items:[{key:1,label:"Day"},{key:2,label:"Week"}]})^
let pages = ui.pagination({id:"pages",total:93,page_size:10})^
view dtna_nav_test: <nav_test> state last_choice:"", last_page:0 {
    <div *[ui.render(sections),ui.render(period),ui.render(pages),
        <span id:"last-choice",last_choice>,<span id:"last-page",string(last_page)>]>
}
on ui_change(action) {
    if (action.action == 'page') { last_page = action.value.current }
    else { last_choice = string(action.value) }
}
ui.page(<nav_test>)^
