import ui: lambda.ui.bold
import ant: lambda.ui.dtna

let source = <bold.page title:"Bold document",direction:'rtl',tokens:{primary:"#fb7185"},
    <bold.card title:"Project",
        <bold.button id:"save","Save">
        <bold.input id:"name",default_value:"Ada">>
    <bold.config_provider tokens:{radius:0},
        <bold.button "Inherited accent">>>
let document = apply(source)
let root = content(content(document)[1])[0]
let card = content(root)[0]
let body = content(card)[1]
let scope = content(root)[1];
[
    ["source identity retained",name(source) == 'bold.page' and name(content(source)[0]) == 'bold.card'],
    ["document shell",name(document) == 'html' and document.dir == "rtl" and document.lang == "en-US"],
    ["root theme",root.class == "bold-root" and contains(root.style,"--bold-primary:#fb7185;") and not contains(root.style,"--dtna-")],
    ["flat page content",len(content(root)) == 2 and len(content(body)) == 2],
    ["nested native presentation",name(content(body)[0]) == 'button' and content(body)[0].id == "save" and content(body)[1].value == "Ada"],
    ["scope inheritance",name(scope) == 'fieldset' and scope.style == "--bold-radius:0px;"],
    ["default direct page",apply(<bold.page "Default">) == ui.page("Default")^],
    ["families coexist",ui.render(<bold.button "B">).class == "bold-button" and ant.render(ant.button({},"A")^).class == "dtna-button"],
    ["cross-family child",content(content(ui.render(ui.card({},ant.button({},"Ant child")^)^))[0])[0].class == "dtna-button"],
    ["constructor direct attributes",(ui.button({id:"direct"})^).id == "direct" and name(ui.button({})^) == 'bold.button'],
    ["legacy props maps",ui.render(<bold kind:'input',props:{value:"legacy"}>).value == "legacy"]
]
