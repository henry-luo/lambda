import ui: lambda.ui.bold

let button = ui.render(ui.button({id:"save",variant:'secondary',["aria-label"]:"Save item"},"Save")^)
let submit = ui.render(ui.button({type:'submit'},"Submit")^)
let anchor = ui.render(ui.button({href:"#next",target:"_blank"},"Next")^)
let disabled = ui.render(ui.button({href:"#next",disabled:true},"Next")^)
let input = ui.render(ui.input({id:"name",type:'email',value:"",required:true})^)
let check = ui.render(ui.checkbox({checked:false},"No")^)
let selected = ui.render(ui.select({value:false,options:[{value:true,label:"Yes"},{value:false,label:"No"}]})^)
let card = ui.render(ui.card({title:"Card",description:"Description",footer:["Footer",<b "slot">]},["Body",ui.badge({},"New")^])^)
let progress = ui.render(ui.progress({percent:0,label:"Build",show_info:false})^)
let area = ui.render(ui.text_area({value:"Notes",rows:4})^)
let toggle = ui.render(ui.switch({checked:true,label:"Enabled"})^);
[
    ["native button",name(button) == 'button' and button.type == "button" and button.id == "save"],
    ["family attributes",button.class == "bold-button bold-variant-secondary" and button["aria-label"] == "Save item"],
    ["native submit",submit.type == "submit"],
    ["anchor semantics",name(anchor) == 'a' and anchor.href == "#next" and anchor.rel == "noopener noreferrer"],
    ["disabled anchor",not ("href" at map(disabled)) and disabled.tabindex == -1 and disabled["aria-disabled"] == "true"],
    ["native input",name(input) == 'input' and input.type == "email" and input.value == "" and ("required" at map(input))],
    ["false checked preserved",not ("checked" at map(content(check)[0]))],
    ["typed false select",content(selected)[1].value == "false" and ("selected" at map(content(selected)[1]))],
    ["card slots flattened",len(content(card)) == 3 and len(content(content(card)[1])) == 2 and len(content(content(card)[2])) == 2],
    ["nested component rendered",content(content(card)[1])[1].class == "bold-badge"],
    ["zero progress",progress["aria-valuenow"] == "0" and progress["aria-label"] == "Build" and len(content(progress)) == 1],
    ["native textarea",name(area) == 'textarea' and area.rows == 4 and content(area)[0] == "Notes"],
    ["switch semantics",content(toggle)[0].role == "switch" and ("checked" at map(content(toggle)[0]))],
    ["six heading levels",all([for (level in 1 to 6) name(ui.render(ui.title({level:level},"Title")^)) == symbol("h" ++ string(level))])],
    ["status semantics",ui.render(ui.alert({status:'error'},"Failed")^).role == "alert" and ui.render(ui.alert({},"Ready")^).role == "status"],
    ["CSS length spacing",contains(ui.render(ui.flex({gap:"1.5rem",direction:'vertical',wrap:true},["A","B"])^).style,"gap:1.5rem;")]
]
