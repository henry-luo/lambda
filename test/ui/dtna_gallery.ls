// author logical dtna elements; imported view templates own their presentation (S12.1.3).
import ui: lambda.ui.dtna

let gallery = <dtna.page title:"Lambda UI dtna gallery",
    <dtna.flex direction:'vertical',align:"stretch",gap:20,style:"max-width:1000px;margin:auto;",
        <dtna.space gap:16,
            <dtna.avatar dimension:48, "λ">
            <dtna.flex direction:'vertical',align:"flex-start",gap:0,
                <dtna.title level:2, "Lambda UI · dtna">
                <dtna.text "Ant-inspired components rendered by Radiant">>>
        <dtna.card title:"Buttons and feedback",style:"width:100%;",
            <dtna.space wrap:true,
                <dtna.button id:"gallery-primary", variant:'primary', "Primary">
                <dtna.button variant:'default', "Default">
                <dtna.button variant:'dashed', "Dashed">
                <dtna.button variant:'text', "Text">
                <dtna.button variant:'link', "Link">
                <dtna.button disabled:true, "Disabled">
                <dtna.badge count:7,
                    <dtna.button "Inbox">>>
            <dtna.divider>
            <dtna.space
                <dtna.tag "Lambda">
                <dtna.tag closable:true, "Closable tag">
                <dtna.icon name:"check",label:"Success">
                <dtna.icon name:"search",label:"Search">>
            <dtna.alert id:"gallery-alert",status:'success',message:"Ready to build",
                description:"Native input, event routing and scoped tokens.",closable:true>>
        <dtna.card title:"Native data entry",style:"width:100%;",
            <dtna.form
                <dtna.form_item label:"Project name",for:"gallery-name",required:true,
                    <dtna.input id:"gallery-name",name:"project",placeholder:"Enter a project name",required:true>>
                <dtna.form_item label:"Notes",for:"gallery-notes",
                    <dtna.text_area id:"gallery-notes",rows:3,placeholder:"Write a note…",style:"width:100%;">>
                <dtna.space wrap:true,
                    <dtna.select id:"gallery-select",default_value:'active',options:[
                        {value:'active',label:"Active"},{value:'archived',label:"Archived"}]>
                    <dtna.checkbox id:"gallery-check", "Subscribe">
                    <dtna.switch id:"gallery-switch",label:"Enabled">
                    <dtna.radio id:"gallery-radio-a",name:"view",value:"list",default_checked:true, "List">
                    <dtna.radio id:"gallery-radio-b",name:"view",value:"grid", "Grid">>>>
        <dtna.card title:"Navigation",style:"width:100%;",
            <dtna.breadcrumb items:[{title:"Home",href:"#"},{title:"Components"}]>
            <dtna.tabs id:"gallery-tabs",items:[
                {key:'overview',label:"Overview",children:"Tab content lives in a semantic tabpanel."},
                {key:'details',label:"Details",children:"Use arrow keys, Home and End."}]>
            <dtna.space
                <dtna.segmented id:"gallery-segmented",items:[
                    {key:'daily',label:"Daily"},{key:'weekly',label:"Weekly"}]>
                <dtna.pagination id:"gallery-pages",total:100>>
            <dtna.menu id:"gallery-menu",items:[{key:"home",label:"Home"},{key:"reports",label:"Reports"}]>
            <dtna.steps current:1,items:[{title:"Define",description:"Models"},
                {title:"Build",description:"Components"},{title:"Verify",description:"Native tests"}]>>
        <dtna.card title:"Data display",style:"width:100%;",
            <dtna.space gap:40,align:"flex-start",
                <dtna.statistic title:"Tasks",value:32,suffix:"ready">
                <dtna.timeline items:[{label:"Foundation",children:"Templates and tokens"},
                    {label:"Interaction",children:"Native controls and keyboard"}]>
                <dtna.descriptions items:[{label:"Target",children:"Radiant"},
                    {label:"Theme",children:"Default light"}]>>
            <dtna.progress percent:64>
            <dtna.skeleton rows:2>>
        <dtna.card title:"Scoped configuration",style:"width:100%;",
            <dtna.config_provider id:"purple-scope",tokens:{primary:"#722ed1"},
                <dtna.space
                    <dtna.button id:"gallery-purple",variant:'primary', "Purple scope">
                    <dtna.config_provider tokens:{radius:0},
                        <dtna.button id:"gallery-square",variant:'primary', "Inherited purple">>>>
            <dtna.empty description:"No additional themes yet">>
        <dtna.card title:"Composition",style:"width:100%;",
            <dtna.layout
                <dtna.layout_header "Application header">
                <dtna.layout
                    <dtna.layout_sider "Navigation">
                    <dtna.layout_content
                        <dtna.row
                            <dtna.col span:12, "Left column">
                            <dtna.col span:12, "Right column">>>>
                <dtna.layout_footer
                    <dtna.link href:"https://ant.design/", "Ant Design reference">>>
            <dtna.paragraph "Lambda components compose as immutable data and present through view templates.">
            <dtna.spin "Preparing">
            <dtna.result status:'success',title:"Ready",description:"Try the interactive controls above.">>>>;

apply(gallery)
