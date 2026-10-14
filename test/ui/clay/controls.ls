import ui: lambda.ui.clay

view clay_control_example: <clay_control_example> state accepted:"", count:0, request:"", action_name:"" {
    ui.render(<clay.flex direction:'vertical',align:"flex-start",gap:16,
        <clay.input id:"draft",default_value:"",label:"Draft">
        <clay.text_area id:"notes",rows:2,label:"Notes">
        <clay.input id:"accepted",value:accepted,label:"Accepted edits">
        <clay.input id:"fixed",value:"Original",label:"Rejected edits">
        <clay.select id:"typed",value:10,options:[{value:10,label:"Ten"},{value:20,label:"Twenty"}]>
        <clay.space
            <clay.checkbox id:"choice","Uncontrolled">
            <clay.checkbox id:"controlled",checked:false,"Controlled">
            <clay.switch id:"toggle",label:"Notifications">>
        <clay.space
            <clay.radio id:"a",name:"clay-choice",value:'a',default_checked:true,"A">
            <clay.radio id:"b",name:"clay-choice",value:'b',"B">>
        <clay.space
            <clay.button id:"count-up",variant:'primary',"Count">
            <clay.button id:"disabled",disabled:true,"Disabled">
            <clay.button id:"loading",loading:true,"Loading">>
        <clay.form id:"form",
            <clay.input id:"project",name:"project",default_value:"Clay",label:"Project">
            <clay.space style:"margin-top:14px;",
                <clay.button id:"submit",type:'submit',"Save">
                <clay.button id:"reset",type:'reset',"Reset">>>
        <output id:"count",string(count)>
        <output id:"request",request>
        <output id:"action",action_name>>)^
}
on ui_change(action) {
    request = string(action.id) ++ ":" ++ string(type(action.value)) ++ ":" ++ string(action.value)
    if (action.id == "accepted") { accepted = action.value }
}
on ui_action(action) {
    action_name = string(action.id) ++ ":" ++ string(action.action)
    if (action.id == "count-up") { count = count + 1 }
    if (action.action == 'submit') { request = action.value[0][0] ++ "=" ++ action.value[0][1] }
}
apply(<clay.page <clay_control_example>>)
