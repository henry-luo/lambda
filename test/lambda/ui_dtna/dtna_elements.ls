import ui: lambda.ui.dtna
import c: lambda.ui.core.component

let source = <dtna.page title:"Element document",locale:"en-US",direction:"rtl",tokens:{primary:"#722ed1"},
    <dtna.card title:"Project",
        <dtna.space gap:16,
            <dtna.button id:"save",variant:'primary',"Save">
            <dtna.checkbox id:"subscribe",default_checked:true,"Subscribe">>>
    <dtna.progress percent:64>>
let document = apply(source)
let head = content(document)[0]
let root = content(content(document)[1])[0]
let card = content(root)[0]
let spacer = content(content(card)[1])[0]
let button = content(content(spacer)[0])[0]
let checkbox = content(content(spacer)[1])[0];
[
    ["logical source retained",name(source) == 'dtna.page' and name(content(source)[0]) == 'dtna.card'],
    ["page template",name(document) == 'html' and document.dir == "rtl" and content(content(head)[1])[0] == "Element document"],
    ["root tokens and flat children",contains(root.style,"--dtna-primary:#722ed1;") and len(content(root)) == 2],
    ["nested button template",name(button) == 'button' and button.id == "save" and content(content(button)[0])[0] == "Save"],
    ["nested native checkbox",name(checkbox) == 'label' and content(checkbox)[0].id == "subscribe" and ("checked" at map(content(checkbox)[0]))],
    ["second root template",content(root)[1]["aria-valuenow"] == "64"],
    ["page defaults",apply(<dtna.page "Default">) == ui.page("Default")^],
    ["page validation",(apply(<dtna.page unknown:true>) or null) == null],
    ["flat constructor attributes",(ui.button({id:"save",variant:'primary'},"Save")^) == <dtna.button id:"save",variant:'primary',"Save">],
    ["tag metadata wins over name attribute",c.kind(<dtna.input name:"email">) == 'input'],
    ["native HTML stays native",apply(<button variant:'dashed',"Native">) == <button variant:'dashed',"Native">],
    ["other component namespace stays distinct",apply(<other.button variant:'dashed',"Other">) == <other.button variant:'dashed',"Other">],
    ["layout keeps component prefixes distinct",ui.render(<dtna.layout <other.layout_sider>>).class == "dtna-layout"],
    ["kind is the tag only",not ("kind" at map(ui.button({},"Plain")^))],
    ["empty props omitted",apply(<dtna.button "Plain">) == ui.render(ui.button({},"Plain")^)],
    ["attribute projection excludes children",c.props(<dtna.space gap:0,"Child">) == {gap:0}],
    ["attribute presence preserves null and false",c.has(c.props(<dtna.input value:null>),"value") and
        c.option(c.props(<dtna.input value:null>),"value","fallback") == null and
        c.option(c.props(<dtna.checkbox checked:false>),"checked",true) == false and
        c.props(<dtna.divider>) == {}]
]
