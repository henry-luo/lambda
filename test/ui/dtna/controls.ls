import ui: lambda.ui.dtna
import dom

let name_field = <dtna.input id:"name",label:"Name",default_value:"">
let notes_field = <dtna.text_area id:"notes",label:"Notes">
let accepted = <dtna.checkbox id:"accepted", "Accept">
let enabled = <dtna.switch id:"enabled",label:"Enabled">
let increment = <dtna.button id:"increment",variant:'primary', "Increment">
let disabled = <dtna.button id:"disabled",disabled:true, "Disabled">
let loading = <dtna.button id:"loading",loading:true, "Loading">
let pick = <dtna.select id:"pick",label:"Priority",default_value:10,
    options:[{value:10,label:"Low"},{value:20,label:"High"}]>

view dtna_controls_test: <controls_test> state count:0, last_action:"", display_value:"", display_type:"", changed:false {
    <div *[apply(<dtna.flex direction:'vertical',align:"flex-start", *[
        name_field, notes_field,
        <dtna.space *[accepted,enabled,pick]>,
        <dtna.space *[increment,disabled,loading]>,
        <dtna.config_provider disabled:true, <dtna.input id:"inherited",default_value:"locked">>
    ]>),
    <span id:"count",string(count)>, <span id:"last",last_action>, <span id:"value",display_value>, <span id:"value-type",display_type>,
    <button id:"reset",type:"button","Reset default">,
    apply(<dtna.input id:"controlled",value:if (changed) "new" else "old">)]>
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
apply(<dtna.page title:"dtna controls", <controls_test>>)
