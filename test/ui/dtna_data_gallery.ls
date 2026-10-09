// Native collection recipes use pure models and procedural actions (S12.1.3).
import ui: lambda.ui.dtna
let projects = ui.table({id:"projects",caption:"Project portfolio",selection:'multiple',default_selected_keys:[1],page_size:3,bordered:true,
    columns:[{key:'name',title:"Project",sortable:true,width:240},
        {key:'tasks',title:"Tasks",sortable:true,align:'right'},
        {key:'status',title:"Status",filters:[{value:'active',label:"Active"},{value:'planned',label:"Planned"}],
            render:(value,row,index) => ui.tag({status:if (value == 'active') 'success' else 'info'},string(value))^}],
    rows:[{key:1,name:"UI foundation",tasks:34,status:'active',detail:"Native controls, scoped tokens and typed actions."},
        {key:2,name:"Collections",tasks:12,status:'active',detail:"Sort, filter, select and expand through native interactions."},
        {key:3,name:"Overlay ownership",tasks:8,status:'planned'},{key:4,name:"Accessibility audit",tasks:6,status:'planned'}],
    summary:"Four projects · select a page or sort a column"})^
let outline = ui.tree({id:"project-tree",label:"Package structure",checkable:true,default_expanded_keys:['ui','dtna'],default_checked_keys:['controls'],
    items:[{key:'ui',label:"lambda.ui",children:[{key:'dtna',label:"dtna",children:[{key:'controls',label:"Native controls"},
        {key:'collections',label:"Collections"},{key:'overlays',label:"Overlays · pending",disabled:true}]}]},
        {key:'themes',label:"Other style families · future",disabled:true}]})^
let disclosure = ui.collapse({id:"project-details",default_active_keys:['retained'],items:[
    {key:'retained',label:"Retained editing",extra:ui.tag({},"Native state")^,
        children:ui.input({id:"project-note",default_value:"Hide and reopen this panel",style:"width:100%;"})^},
    {key:'keyboard',label:"Keyboard support",children:"Use arrows and Home/End between headings. Tree also supports Left/Right, Enter, Space and first-letter search."},
    {key:'future',label:"Async data · pending",disabled:true,children:"Lazy loading and virtualization remain outstanding."}]})^
ui.page(ui.flex({direction:'vertical',align:'stretch',gap:24,style:"max-width:1000px;margin:auto;"},[
    ui.title({level:2},"dtna · data and disclosure")^,projects,
    ui.row({gutter:24},[ui.col({span:12},ui.card({title:"Checked hierarchy"},outline)^)^,
        ui.col({span:12},ui.card({title:"Disclosure"},disclosure)^)^])^,
    ui.paragraph({},"Native Radiant preview. These components remain partial in the 73-entry Phase 1 inventory.")^
])^,{title:"Lambda UI dtna data gallery"})^
