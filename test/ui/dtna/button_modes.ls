import ui: lambda.ui.dtna
import dom
let icon = ui.icon({name:"plus"})^
let rows = [
    [ui.button({id:"button-primary",variant:'primary'},"Primary")^,ui.button({id:"button-default"},"Default")^,
        ui.button({id:"button-dashed",variant:'dashed'},"Dashed")^,ui.button({id:"button-text",variant:'text'},"Text")^,
        ui.button({id:"button-link",variant:'link',href:"#button-anchor"},"Link")^,ui.button({id:"button-danger",danger:true},"Danger")^],
    [ui.button({id:"button-small",size:'small'},"Small")^,ui.button({id:"button-middle"},"Middle")^,
        ui.button({id:"button-large",size:'large'},"Large")^,ui.button({id:"button-circle",shape:'circle',icon:icon,label:"Add"})^,
        ui.button({id:"button-round",shape:'round'},"Round")^,ui.button({id:"button-icon",icon:ui.icon({name:"plus"})^,label:"Add only"})^,
        ui.button({id:"button-end",icon:ui.icon({name:"plus"})^,icon_placement:'end'},"End")^],
    [ui.button({id:"button-disabled",disabled:true},"Disabled")^,ui.button({id:"button-loading",loading:true},"Loading")^,
        ui.button({id:"button-custom",loading:{icon:<b "L">}},"Custom")^,
        ui.button({id:"button-parts",icon:<i "I">,class_names:{root:"authored-button",icon:"authored-image",content:"authored-caption"},
            styles:{content:"font-weight:700;"}},"Parts")^,
        ui.button({id:"button-disabled-link",disabled:true,href:"#button-anchor"},"Disabled link")^],
    [ui.button({id:"button-delay",loading:{delay:5000}},"Delayed")^,
        ui.button({id:"button-toggle"},"Toggle")^,ui.button({id:"button-remove"},"Remove")^]]
let submit = ui.button({id:"button-submit",type:'submit'},"Submit")^
let reset = ui.button({id:"button-reset",type:'reset'},"Reset")^
view button_examples: <button_examples> state count:0,last_action:"",pending:false,removed:false,submits:0 {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;",
        *[*[for (row in rows) <div style:"display:flex;align-items:center;gap:12px;",*ui.render(row)>],
        ui.render(ui.button({id:"button-controlled",loading:if (pending) {delay:300} else false},"Controlled")^),
        if (not removed) ui.render(ui.button({id:"button-removable",loading:{delay:60000}},"Removable")^) else null,
        <form style:"display:flex;align-items:center;",*[<input id:"button-form-input",value:"seed",style:"font:inherit;box-sizing:border-box;width:180px;height:32px;border:1px solid #d9d9d9;border-radius:6px;padding:4px 11px;">,*ui.render([submit,reset])]>,
        <div style:"background:#555;padding:12px;",ui.render(ui.button({id:"button-ghost",ghost:true,variant:'primary'},"Ghost")^)>,
        <div id:"button-anchor","Target">,<output id:"button-count",string(count)>,
        <output id:"button-request",last_action>,<output id:"button-submits",string(submits)>]>
}
on ui_action(action) {
    if (action.id == "button-toggle") { pending = not pending }
    else if (action.id == "button-remove") { removed = true }
    else { count = count + 1; last_action = string(action.id) }
}
on submit(evt) { submits = submits + 1; 'prevent-default' }
ui.page(<button_examples>,{tokens:{font_family:"Liberation Sans"}})^
