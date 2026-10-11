import ui: lambda.ui.dtna
import dom
let icon = <dtna.icon name:"plus">
let rows = [
    [<dtna.button id:"button-primary",variant:'primary', "Primary">,<dtna.button id:"button-default", "Default">,
        <dtna.button id:"button-dashed",variant:'dashed', "Dashed">,<dtna.button id:"button-text",variant:'text', "Text">,
        <dtna.button id:"button-link",variant:'link',href:"#button-anchor", "Link">,<dtna.button id:"button-danger",danger:true, "Danger">],
    [<dtna.button id:"button-small",size:'small', "Small">,<dtna.button id:"button-middle", "Middle">,
        <dtna.button id:"button-large",size:'large', "Large">,<dtna.button id:"button-circle",shape:'circle',icon:icon,label:"Add">,
        <dtna.button id:"button-round",shape:'round', "Round">,<dtna.button id:"button-icon",icon:<dtna.icon name:"plus">,label:"Add only">,
        <dtna.button id:"button-end",icon:<dtna.icon name:"plus">,icon_placement:'end', "End">],
    [<dtna.button id:"button-disabled",disabled:true, "Disabled">,<dtna.button id:"button-loading",loading:true, "Loading">,
        <dtna.button id:"button-custom",loading:{icon:<b "L">}, "Custom">,
        <dtna.button id:"button-parts",icon:<i "I">,class_names:{root:"authored-button",icon:"authored-image",content:"authored-caption"},
            styles:{content:"font-weight:700;"}, "Parts">,
        <dtna.button id:"button-disabled-link",disabled:true,href:"#button-anchor", "Disabled link">],
    [<dtna.button id:"button-delay",loading:{delay:5000}, "Delayed">,
        <dtna.button id:"button-toggle", "Toggle">,<dtna.button id:"button-remove", "Remove">]]
let submit = <dtna.button id:"button-submit",type:'submit', "Submit">
let reset = <dtna.button id:"button-reset",type:'reset', "Reset">
view button_examples: <button_examples> state count:0,last_action:"",pending:false,removed:false,submits:0 {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;",
        *[*[for (row in rows) <div style:"display:flex;align-items:center;gap:12px;",*[for (node in row) apply(node)]>],
        apply(<dtna.button id:"button-controlled",loading:if (pending) {delay:300} else false, "Controlled">),
        if (not removed) apply(<dtna.button id:"button-removable",loading:{delay:60000}, "Removable">) else null,
        <form style:"display:flex;align-items:center;",*[<input id:"button-form-input",value:"seed",style:"font:inherit;box-sizing:border-box;width:180px;height:32px;border:1px solid #d9d9d9;border-radius:6px;padding:4px 11px;">,*[for (node in [submit,reset]) apply(node)]]>,
        <div style:"background:#555;padding:12px;",apply(<dtna.button id:"button-ghost",ghost:true,variant:'primary', "Ghost">)>,
        <div id:"button-anchor","Target">,<output id:"button-count",string(count)>,
        <output id:"button-request",last_action>,<output id:"button-submits",string(submits)>]>
}
on ui_action(action) {
    if (action.id == "button-toggle") { pending = not pending }
    else if (action.id == "button-remove") { removed = true }
    else { count = count + 1; last_action = string(action.id) }
}
on submit(evt) { submits = submits + 1; 'prevent-default' }
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <button_examples>>)
