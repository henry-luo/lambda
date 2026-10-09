// Native component explorer. Browser interaction is outside this package phase.
import ui: lambda.ui.dtna

fn panel(title, children) element^ => ui.card({title:title,style:"width:100%;"},children)^
let overview = ui.space({gap:16},[
    ui.avatar({dimension:48},"λ")^,
    ui.flex({direction:'vertical',align:"flex-start",gap:0},[
        ui.title({level:2},"Lambda UI · dtna")^,
        ui.text({},"Ant-inspired components rendered by Radiant")^
    ])^
])^
let buttons = panel("Buttons and feedback",[
    ui.space({wrap:true},[
        ui.button({id:"gallery-primary",variant:'primary'},"Primary")^,
        ui.button({variant:'default'},"Default")^,ui.button({variant:'dashed'},"Dashed")^,
        ui.button({variant:'text'},"Text")^,ui.button({variant:'link'},"Link")^,
        ui.button({disabled:true},"Disabled")^,
        ui.badge({count:7},ui.button({},"Inbox")^)^
    ])^,
    ui.divider()^,
    ui.space({},[ui.tag({},"Lambda")^,ui.tag({closable:true},"Closable tag")^,
        ui.icon({name:"check",label:"Success"})^,ui.icon({name:"search",label:"Search"})^])^,
    ui.alert({id:"gallery-alert",status:'success',message:"Ready to build",description:"Native input, event routing and scoped tokens.",closable:true})^
])^
let form_panel = panel("Native data entry", ui.form({},[
    ui.form_item({label:"Project name",for:"gallery-name",required:true},
        ui.input({id:"gallery-name",name:"project",placeholder:"Enter a project name",required:true})^)^,
    ui.form_item({label:"Notes",for:"gallery-notes"},ui.text_area({id:"gallery-notes",rows:3,placeholder:"Write a note…",style:"width:100%;"})^)^,
    ui.space({wrap:true},[
        ui.select({id:"gallery-select",default_value:'active',options:[{value:'active',label:"Active"},{value:'archived',label:"Archived"}]})^,
        ui.checkbox({id:"gallery-check"},"Subscribe")^,ui.switch({id:"gallery-switch",label:"Enabled"})^,
        ui.radio({id:"gallery-radio-a",name:"view",value:"list",default_checked:true},"List")^,
        ui.radio({id:"gallery-radio-b",name:"view",value:"grid"},"Grid")^
    ])^
])^)^
let navigation = panel("Navigation",[
    ui.breadcrumb({items:[{title:"Home",href:"#"},{title:"Components"}]})^,
    ui.tabs({id:"gallery-tabs",items:[{key:'overview',label:"Overview",children:"Tab content lives in a semantic tabpanel."},
        {key:'details',label:"Details",children:"Use arrow keys, Home and End."}]})^,
    ui.space({},[ui.segmented({id:"gallery-segmented",items:[{key:'daily',label:"Daily"},{key:'weekly',label:"Weekly"}]})^,
        ui.pagination({id:"gallery-pages",total:100})^])^,
    ui.menu({id:"gallery-menu",items:[{key:"home",label:"Home"},{key:"reports",label:"Reports"}]})^,
    ui.steps({current:1,items:[{title:"Define",description:"Models"},{title:"Build",description:"Components"},{title:"Verify",description:"Native tests"}]})^
])^
let display = panel("Data display",[
    ui.space({gap:40,align:"flex-start"},[
        ui.statistic({title:"Tasks",value:32,suffix:"ready"})^,
        ui.timeline({items:[{label:"Foundation",children:"Templates and tokens"},{label:"Interaction",children:"Native controls and keyboard"}]})^,
        ui.descriptions({items:[{label:"Target",children:"Radiant"},{label:"Theme",children:"Default light"}]})^
    ])^,
    ui.progress({percent:64})^, ui.skeleton({rows:2})^
])^
let config = panel("Scoped configuration",[
    ui.config_provider({id:"purple-scope",tokens:{primary:"#722ed1"}},
        ui.space({},[ui.button({id:"gallery-purple",variant:'primary'},"Purple scope")^,
            ui.config_provider({tokens:{radius:0}},ui.button({id:"gallery-square",variant:'primary'},"Inherited purple")^)^])^)^,
    ui.empty({description:"No additional themes yet"})^
])^
let composition = panel("Composition",[
    ui.layout({},[ui.layout_header({},"Application header")^,
        ui.layout({},[ui.layout_sider({},"Navigation")^,
            ui.layout_content({},ui.row({},[ui.col({span:12},"Left column")^,ui.col({span:12},"Right column")^])^)^])^,
        ui.layout_footer({},ui.link({href:"https://ant.design/"},"Ant Design reference")^)^])^,
    ui.paragraph({},"Lambda components compose as immutable data and present through view templates.")^,
    ui.spin({},"Preparing")^,ui.result({status:'success',title:"Ready",description:"Try the interactive controls above."})^
])^
ui.page(ui.flex({direction:'vertical',align:"stretch",gap:20,style:"max-width:1000px;margin:auto;"},
    [overview,buttons,form_panel,navigation,display,config,composition])^,{title:"Lambda UI dtna gallery"})^
