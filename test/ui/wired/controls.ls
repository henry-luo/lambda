import ui: lambda.ui.wired
import bold: lambda.ui.bold

view wired_controls_test: <wired_controls_test> state count:0, accepted:"", request:"", last_source:"", selected_type:"" {
    ui.render(<wired.flex direction:'vertical',align:"flex-start",gap:14,
        <wired.input id:"draft",label:"Draft",default_value:"">
        <wired.text_area id:"notes",label:"Notes",rows:2>
        <wired.input id:"fixed",label:"Fixed",value:"seed">
        <wired.input id:"accepted",label:"Accepted",value:accepted>
        <wired.checkbox id:"checked",checked:false,"Controlled choice">
        <wired.checkbox id:"choice","Native choice">
        <wired.switch id:"toggle",label:"Share">
        <wired.radio id:"radio-a",name:"native",value:'a',default_checked:true,"A">
        <wired.radio id:"radio-b",name:"native",value:'b',"B">
        <bold.radio id:"bold-peer",name:"controlled",value:'bold',checked:true,"Bold peer">
        <wired.radio id:"wired-peer",name:"controlled",value:'wired',checked:false,"Wired peer">
        <wired.select id:"stage",value:10,options:[{value:10,label:"Ten"},{value:20,label:"Twenty"}]>
        <wired.button id:"disabled",disabled:true,"Disabled">
        <wired.button id:"increment","Increment">
        <wired.form id:"form",
            <wired.input id:"project",name:"project",default_value:"Notebook",label:"Project">
            <wired.space style:"margin-top:12px;",
                <wired.button id:"submit",type:'submit',"Submit">
                <wired.button id:"reset",type:'reset',"Reset">>>
        <output id:"count",string(count)>
        <output id:"request",request>
        <output id:"last",last_source>
        <output id:"selected-type",selected_type>>)^
}
on ui_change(action) {
    last_source = string(action.id)
    request = string(action.value)
    if (action.id == "accepted") { accepted = action.value }
    if (action.id == "stage") { selected_type = string(type(action.value)) }
}
on ui_action(action) {
    last_source = string(action.id) ++ ":" ++ string(action.action)
    if (action.id == "increment") { count = count + 1 }
    if (action.action == 'submit') { request = action.value[0][0] ++ "=" ++ action.value[0][1] }
}
ui.page(<wired_controls_test>,{tokens:{font_family:"Arial"}})^
