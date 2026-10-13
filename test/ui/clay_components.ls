import ui: lambda.ui.clay

apply(<clay.page title:"Clay · Component collection",
    <clay.flex direction:'vertical',align:"stretch",gap:28,style:"max-width:1100px;margin:auto;",
        <clay.breadcrumb items:[{title:"Clay Studio"},{title:"Design system"}]>
        <clay.title level:1,style:"margin:0;","Made to take shape.">
        <clay.paragraph type:'secondary',"A complete working collection of soft surfaces, native controls, navigation, data display and feedback. Try the controls, tabs, table, and tree.">
        <clay.card title:"01 / Actions & typography",
            <clay.space wrap:true,gap:16,
                <clay.button variant:'primary',"Primary">
                <clay.button "Default">
                <clay.button variant:'dashed',"Dashed">
                <clay.button variant:'text',"Text">
                <clay.button variant:'link',"Link">
                <clay.button danger:true,"Danger">
                <clay.button disabled:true,"Disabled">
                <clay.button loading:true,"Loading">>
            <clay.space wrap:true,style:"margin-top:20px;",
                <clay.button size:'small',"Small">
                <clay.button size:'large',icon:<clay.icon name:"plus">,"Large action">
                <clay.button shape:'circle',label:"Search",icon:<clay.icon name:"search">>
                <clay.button_group <clay.button "Left"> <clay.button "Right">>>
            <clay.divider>
            <clay.space wrap:true,
                <clay.text strong:true,"Strong text"> <clay.text type:'secondary',"Secondary">
                <clay.text code:true,"lambda.ui.clay"> <clay.text mark:true,"Highlighted">
                <clay.text delete:true,"Removed"> <clay.link href:"#","A useful link">>>
        <clay.card title:"02 / Native inputs & forms",
            <clay.form id:"clay-example-form",
                <clay.row gutter:24,
                    <clay.col span:12,
                        <clay.form_item label:"Email address",for:"clay-email",required:true,
                            <clay.input id:"clay-email",name:"email",type:'email',placeholder:"you@example.com",required:true>>
                        <clay.form_item label:"Password",for:"clay-password",
                            <clay.input id:"clay-password",type:'password',placeholder:"Your password">>
                        <clay.form_item label:"Workspace",for:"clay-workspace",
                            <clay.select id:"clay-workspace",default_value:'design',options:[{value:'design',label:"Design studio"},{value:'product',label:"Product team"}]>>>
                    <clay.col span:12,
                        <clay.form_item label:"A little about your project",for:"clay-notes",
                            <clay.text_area id:"clay-notes",rows:3,placeholder:"What are you working on?">>
                        <clay.form_item label:"Needs attention",status:'error',help:"Please add a project name.",
                            <clay.input status:'error',placeholder:"Project name">>>>
                <clay.space wrap:true,gap:24,
                    <clay.checkbox id:"clay-remember","Remember me">
                    <clay.switch id:"clay-notifications",label:"Notifications",default_checked:true>
                    <clay.radio name:"clay-plan",value:'personal',default_checked:true,"Personal">
                    <clay.radio name:"clay-plan",value:'team',"Team">
                    <clay.rate id:"clay-rating",default_value:3,label:"Rate your experience">>
                <clay.space style:"margin-top:24px;",
                    <clay.button variant:'primary',type:'submit',"Create workspace →">
                    <clay.button type:'reset',"Reset">>>>
        <clay.card title:"03 / Navigation & disclosure",
            <clay.tabs id:"clay-demo-tabs",items:[
                {key:'overview',label:"Overview",children:<clay.paragraph "Your best work starts with a clear overview.">},
                {key:'details',label:"Details",children:<clay.alert message:"Every detail has a place.",status:'info'>},
                {key:'disabled',label:"Coming soon",disabled:true}]>
            <clay.space wrap:true,style:"margin:20px 0;",
                <clay.segmented id:"clay-demo-segments",items:[{key:'day',label:"Day"},{key:'week',label:"Week"},{key:'month',label:"Month"}]>
                <clay.pagination id:"clay-demo-pages",total:85,page_size:10>>
            <clay.steps current:1,items:[{title:"Imagine",description:"Find the idea"},{title:"Shape",description:"Make it yours"},{title:"Share",description:"Put it out there"}]>
            <clay.collapse id:"clay-demo-collapse",items:[
                {key:'native',label:"Does it support native interaction?",children:"Yes. Fields, focus, keyboard navigation and forms keep their existing native behavior."},
                {key:'tokens',label:"Can I make it my own?",children:"Override colors, dimensions and shadows with scoped theme tokens."}]>>
        <clay.card title:"04 / Collections & data display",
            <clay.row gutter:24,
                <clay.col span:12,
                    <clay.tree id:"clay-demo-tree",checkable:true,default_expanded_keys:['studio'],items:[
                        {key:'studio',label:"Studio",children:[{key:'design',label:"Design system"},{key:'product',label:"Product experience"}]}]>
                    <clay.timeline style:"margin-top:24px;",items:[{label:"09:30",children:"Project created"},{label:"10:15",children:"Team invited"},{label:"11:00",children:"First idea shared"}]>>
                <clay.col span:12,
                    <clay.space gap:16,
                        <clay.avatar "AJ"> <clay.avatar shape:'square',"MK">
                        <clay.badge count:8,<clay.button "Inbox">>
                        <clay.badge status:'success',text:"Online">>
                    <clay.space wrap:true,style:"margin:20px 0;",
                        <clay.tag color:"purple","Design"> <clay.tag color:"green","Development">
                        <clay.tag color:"orange",closable:true,"Marketing">
                        <clay.checkable_tag "Favorite">>
                    <clay.descriptions bordered:true,column:1,items:[{label:"Owner",children:"Alex Johnson"},{label:"Team",children:"Clay Studio"},{label:"Plan",children:"Professional"}]>>>
            <clay.divider>
            <clay.table id:"clay-demo-table",page_size:2,selection:'multiple',columns:[{key:'name',title:"Project",sortable:true},{key:'status',title:"Status"}],rows:[
                {key:1,name:"Design system",status:"In progress"},{key:2,name:"Studio website",status:"Ready"},{key:3,name:"Mobile app",status:"Planning"}]>>
        <clay.card title:"05 / Feedback & loading",
            <clay.flex direction:'vertical',align:"stretch",gap:18,
                <clay.alert status:'success',message:"All set!",description:"Your changes have been saved.",show_icon:true,closable:true>
                <clay.alert status:'warning',message:"One more thing",description:"Review your details before sharing.",show_icon:true>
                <clay.alert status:'error',message:"Something needs attention",description:"Check your connection and try again.",show_icon:true>
                <clay.progress percent:68>
                <clay.space gap:24,
                    <clay.progress type:'circle',percent:85,dimension:100>
                    <clay.progress type:'dashboard',percent:42,dimension:100>
                    <clay.spin tip:"Taking shape…">>
                <clay.skeleton avatar:true,paragraph:{rows:2},active:true>
                <clay.empty image:'simple',description:"Room for something wonderful.">
                <clay.result status:'success',title:"Ready to share",subtitle:"Your next great idea is taking shape.",extra:<clay.button variant:'primary',"Back to your workspace">>>>
        <clay.card title:"06 / Your palette. Your personality.",
            <clay.config_provider id:"clay-mint-scope",tokens:{primary:"#287b63",lavender:"#d7eee5",radius:12},
                <clay.space gap:20,wrap:true,
                    <clay.button id:"clay-mint",variant:'primary',"Fresh mint">
                    <clay.config_provider tokens:{radius:24},
                        <clay.button id:"clay-inherited",variant:'primary',"Inherited mint">>
                    <clay.config_provider disabled:true,<clay.button id:"clay-disabled-scope","Disabled together">>>>
            <clay.paragraph type:'secondary',style:"margin-top:24px;","59 components and subcomponents, shared keyboard behavior, and a theme that stays yours.">>>>)
