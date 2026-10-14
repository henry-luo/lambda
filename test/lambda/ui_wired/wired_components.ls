import ui: lambda.ui.wired
import bold: lambda.ui.bold
import ant: lambda.ui.dtna

let source = <wired.page title:"Notebook",direction:'rtl',tokens:{ink:"#334455"},
    <wired.card title:"Idea",footer:["Footer",<b "slot">],
        <wired.button id:"save",variant:'primary',"Save">
        <wired.input id:"name",value:"",required:true>>
    <wired.config_provider tokens:{stroke_width:2},<wired.button "Heavier">>>
let document = apply(source)
let root = content(content(document)[1])[0]
let card = content(root)[0]
let body = content(card)[2]
let field = content(body)[1]
let button = ui.render(ui.button({href:"#next",target:"_blank"},"Next")^)
let disabled = ui.render(ui.button({href:"#next",disabled:true},"Next")^)
let selected = ui.render(ui.select({value:false,options:[{value:true,label:"Yes"},{value:false,label:"No"}]})^)
let check = ui.render(ui.checkbox({checked:false},"Choice")^)
let progress = ui.render(ui.progress({percent:0,label:"Build",show_info:false})^);
[
    ["source identity",name(source) == 'wired.page' and name(content(source)[0]) == 'wired.card'],
    ["document shell",name(document) == 'html' and document.dir == "rtl" and document.lang == "en-US"],
    ["scoped root",root.class == "wired-root" and contains(root.style,"--wired-ink:#334455;")],
    ["nested native button",name(content(body)[0]) == 'button' and content(body)[0].id == "save" and content(body)[0].type == "button"],
    ["native field identity",name(content(field)[0]) == 'input' and content(field)[0].id == "name" and content(field)[0].value == "" and ("required" at map(content(field)[0]))],
    ["noninteractive decoration",name(content(field)[1]) == 'svg' and content(field)[1]["aria-hidden"] == "true" and content(field)[1].focusable == "false"],
    ["footer slot",len(content(content(card)[3])) == 2],
    ["nested token inheritance",content(root)[1].style == "--wired-stroke-width:2px;"],
    ["anchor safety",name(button) == 'a' and button.href == "#next" and button.rel == "noopener noreferrer"],
    ["disabled anchor",not ("href" at map(disabled)) and disabled.tabindex == -1 and disabled["aria-disabled"] == "true"],
    ["typed select",content(content(selected)[0])[1].value == "false" and ("selected" at map(content(content(selected)[0])[1]))],
    ["controlled false",not ("checked" at map(content(check)[0]))],
    ["zero progress",progress["aria-valuenow"] == "0" and progress["aria-label"] == "Build" and len(content(progress)) == 1],
    ["status roles",ui.render(<wired.alert status:'error',"Error">).role == "alert" and ui.render(<wired.alert "Info">).role == "status"],
    ["separator role",ui.render(ui.divider()^).role == "separator"],
    ["heading levels",all([for (level in 1 to 6) name(ui.render(ui.title({level:level},"Title")^)) == symbol("h" ++ string(level))])],
    ["families coexist",bold.render(<bold.button "B">).class == "bold-button" and ant.render(ant.button({},"A")^).class == "dtna-button"],
    ["cross family slot",content(content(ui.render(ui.card({},bold.button({},"Bold")^)^))[1])[0].class == "bold-button"],
    ["direct page parity",apply(<wired.page "Default">) == ui.page("Default")^]
]
