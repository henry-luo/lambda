import ui: lambda.ui.dtna
import dom

let name_field = ui.input({id:"name",label:"Name",default_value:""})^
let notes_field = ui.text_area({id:"notes",label:"Notes"})^
let accepted = ui.checkbox({id:"accepted"}, "Accept")^
let enabled = ui.switch({id:"enabled",label:"Enabled"})^
let increment = ui.button({id:"increment",variant:'primary'}, "Increment")^
let disabled = ui.button({id:"disabled",disabled:true}, "Disabled")^
let loading = ui.button({id:"loading",loading:true}, "Loading")^
let pick = ui.select({id:"pick",label:"Priority",default_value:10,
    options:[{value:10,label:"Low"},{value:20,label:"High"}]})^

view dtna_controls_test: <controls_test> state count:0, last_action:"", display_value:"", display_type:"", changed:false {
    <div *[ui.render(ui.flex({direction:'vertical',align:"flex-start"}, [
        name_field, notes_field,
        ui.space({}, [accepted,enabled,pick])^,
        ui.space({}, [increment,disabled,loading])^,
        ui.config_provider({disabled:true}, ui.input({id:"inherited",default_value:"locked"})^)^
    ])^),
    <span id:"count",string(count)>, <span id:"last",last_action>, <span id:"value",display_value>, <span id:"value-type",display_type>,
    <button id:"reset",type:"button","Reset default">,
    ui.render(ui.input({id:"controlled",value:if (changed) "new" else "old"})^)]>
}
on ui_action(action) {
    if (action.id == "increment") { count = count + 1 }
    last_action = string(action.action)
}
on ui_change(action) {
    last_action = string(action.id)
    display_value = string(action.value)
    display_type = string(type(action.value))
}
on click(evt) {
    if (dom.get_attribute(evt.target, "id") == "reset") { changed = true }
    'pass'
}
ui.page(<controls_test>, {title:"dtna controls"})^
