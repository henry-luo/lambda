import ui: lambda.ui.clay
import c: lambda.ui.core.component

let source = <clay.page title:"Soft work",direction:'rtl',
    <clay.card title:"Project",<clay.button id:"save",variant:'primary',"Save">>
    <clay.config_provider tokens:{radius:24},<clay.button id:"round","Round">>>
let document = apply(source)
let root = content(content(document)[1])[0]
let card = content(root)[0]
let button = content(content(card)[1])[0]
let tabs = ui.render(<clay.tabs id:"tabs",items:[{key:'one',label:"One",children:<clay.button id:"nested","Nested">}]>)^;
[
    ["logical identity",name(source) == 'clay.page' and name(content(source)[0]) == 'clay.card'],
    ["constructor identity",ui.button({id:"save"},"Save")^ == <clay.button id:"save","Save">],
    ["normalized aliases",c.kind(ui.password()^) == 'input' and c.props(ui.search()^).type == "search"],
    ["page shell",name(document) == 'html' and document.dir == "rtl" and root.class == "dtna-root clay-root"],
    ["native button",name(button) == 'button' and button.id == "save"],
    ["nested slots",content(content(tabs)[1][0])[0].id == "nested"],
    ["layout detection",(ui.render(<clay.layout <clay.layout_sider "Side">>)^).class == "dtna-layout dtna-layout-has-sider"],
    ["provider keeps semantic fieldset",name(content(root)[1]) == 'fieldset' and contains(content(root)[1].style,"--dtna-radius:24px;")],
    ["native markup stays native",ui.render(<button "Native">)^ == <button "Native">],
    ["foreign names stay distinct",ui.render(<other.button "Other">)^ == <other.button "Other">],
    ["explicit null preserved",c.has(c.props(ui.input({value:null})^),"value")],
    ["explicit false preserved",c.props(ui.checkbox({checked:false})^).checked == false]
]
