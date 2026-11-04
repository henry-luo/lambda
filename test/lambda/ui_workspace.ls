import workspace: lambda.ui.workspace
import c: lambda.ui.core.component
import interaction: lambda.ui.core.interaction
import menu: lambda.ui.core.menu

let button = apply(workspace.button({id:"save",variant:"primary"},"Save")^)
let field = apply(workspace.input({id:"query",value:"search",on_change:"find"})^)
let toolbar = apply(workspace.toolbar({id:"tools"},"Tools")^)
let modal = workspace.modal({id:"confirm",label:"Confirm"},"Continue")^
let panel = content(modal)^[0]
let page = workspace.page("Document",{title:"Desktop",tokens:{primary:"#315b8a"}})^
let palette = workspace.tokens({primary:"#315b8a",control_height:30})^;
[
    name(button) == 'button', button.id == "save", contains(button.class,"workspace-button"),
    name(field) == 'input', field.value == "search", not c.has(map(field),"on_change"),
    toolbar.role == "toolbar", contains(toolbar.class,"workspace-toolbar"),
    name(page) == 'html', palette.primary == "#315b8a", palette.control_height == 30,
    contains(workspace.stylesheet(),".workspace-root"),
    ((workspace.tokens({primary:"invalid"}) ^ { null }) or null) == null,
    ((workspace.input({value:"a",default_value:"b"}) ^ { null }) or null) == null,
    ((workspace.button({variant:"unknown"}) ^ { null }) or null) == null,
    not interaction.primary({}), interaction.primary({ctrlKey:true}),
    interaction.primary({metaKey:true}), not interaction.primary({altKey:true}),
    name(panel)=='section' and panel.role=="dialog" and panel['aria-modal']=="true",
    panel['aria-label']=="Confirm" and panel.id=="confirm",
    ((workspace.modal({id:"unnamed"},"Body") ^ { null }) or null)==null,
    ((workspace.modal({id:"both",label:"A",labelled_by:"b"},"Body") ^ { null }) or null)==null,
    ((workspace.modal({id:"blank",label:"  "},"Body") ^ { null }) or null)==null,
    not contains(workspace.stylesheet(),"__workspace_document__"),
    menu.key_action("ArrowDown")=='navigate', menu.key_action("Escape")=='dismiss',
    menu.key_action("Tab")=='dismiss', menu.key_action("a")=='pass'
]
