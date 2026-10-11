import ui: lambda.ui.bold
import ant: lambda.ui.dtna

let controls = [
    <bold.input id:"name",name:"name",label:"Name">,
    <bold.text_area id:"notes",label:"Notes",rows:2>,
    <bold.input id:"fixed",label:"Fixed",value:"seed">,
    <bold.checkbox id:"check","Subscribe">,
    <bold.checkbox id:"fixed-check",checked:false,"Controlled">,
    <bold.switch id:"switch",label:"Enabled">,
    <bold.select id:"select",default_value:10,options:[{value:10,label:"Ten"},{value:20,label:"Twenty"}]>,
    <bold.radio id:"radio-a",name:"choice",value:'a',default_checked:true,"A">,
    <bold.radio id:"radio-b",name:"choice",value:'b',"B">,
    ant.radio({id:"ant-radio",name:"mixed",value:'ant',checked:true},"Ant controlled")^,
    <bold.radio id:"bold-radio",name:"mixed",value:'bold',checked:false,"Bold controlled">,
    <bold.button id:"disabled",disabled:true,"Disabled">,
    <bold.button id:"increment","Increment">
]
let form = <bold.form id:"form",
    <bold.input id:"form-name",name:"project",default_value:"Lambda",label:"Project">
    <bold.button id:"submit",type:'submit',"Submit">
    <bold.button id:"reset",type:'reset',"Reset">>

view bold_controls: <bold_controls> state count:0, last_source:"", value_type:"", requested:"", accepted:"" {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;", *[
        *[for (source in controls) ui.render(source)],
        ui.render(ui.input({id:"accepted",label:"Accepted",value:accepted})^),
        ui.render(form),<output id:"count",string(count)>,<output id:"last",last_source>,
        <output id:"requested",requested>,<output id:"type",value_type>]>
}
on ui_action(action) {
    last_source = string(action.id) ++ ":" ++ string(action.action)
    if (action.id == "increment") { count = count + 1 }
    if (action.action == 'submit') { requested = action.value[0][0] ++ "=" ++ action.value[0][1] }
}
on ui_change(action) {
    last_source = string(action.id)
    requested = string(action.value)
    value_type = string(type(action.value))
    if (action.id == "accepted") { accepted = action.value }
}
ui.page(<bold_controls>,{tokens:{font_family:"Liberation Sans"}})^
