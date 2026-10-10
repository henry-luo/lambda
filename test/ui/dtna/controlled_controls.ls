import ui: lambda.ui.dtna

let fixed_input = ui.input({id:"fixed-input",value:"seed",label:"Fixed input"})^
let fixed_area = ui.text_area({id:"fixed-area",value:"notes",label:"Fixed notes",rows:2})^
let fixed_check = ui.checkbox({id:"fixed-check",checked:false},"Fixed unchecked")^
let fixed_true = ui.checkbox({id:"fixed-true",checked:true},"Fixed checked")^
let fixed_switch = ui.switch({id:"fixed-switch",checked:false,label:"Fixed switch"})^
let radio_a = ui.radio({id:"radio-a",name:"fixed-choice",value:'a',checked:true},"A")^
let radio_b = ui.radio({id:"radio-b",name:"fixed-choice",value:'b',checked:false},"B")^
let fixed_select = ui.select({id:"fixed-select",value:10,options:[{value:10,label:"Ten"},{value:20,label:"Twenty"}]})^

view controlled_contract: <controlled_contract> state accepted:"", checked:false, last_source:"", requested:"", requested_type:"" {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:8px;", *[
        *[for (control in [fixed_input,fixed_area,fixed_check,fixed_true,fixed_switch,radio_a,radio_b,fixed_select]) ui.render(control)],
        ui.render(ui.input({id:"accepted-input",value:accepted,label:"Accepted input"})^),
        ui.render(ui.checkbox({id:"accepted-check",checked:checked},"Accepted checkbox")^),
        <output id:"accepted-value",accepted>,<output id:"last",last_source>,
        <output id:"requested",requested>,<output id:"requested-type",requested_type>]>
}
on ui_change(action) {
    last_source = string(action.id)
    requested = string(action.value)
    requested_type = string(type(action.value))
    if (action.id == "accepted-input") { accepted = action.value }
    if (action.id == "accepted-check") { checked = action.value }
}
ui.page(<controlled_contract>,{tokens:{font_family:"Liberation Sans"}})^
