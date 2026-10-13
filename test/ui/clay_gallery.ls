import ui: lambda.ui.clay

let trend = <svg viewBox:"0 0 620 190",style:"width:100%;height:190px;",role:"img",["aria-label"]:"Revenue rises over the last six months",
    <defs <linearGradient id:"clay-chart-fill",x1:"0",y1:"0",x2:"0",y2:"1",
        <stop offset:"0%",["stop-color"]:"#b09cfa",["stop-opacity"]:"0.5">
        <stop offset:"100%",["stop-color"]:"#b09cfa",["stop-opacity"]:"0">>>
    <g *[for (y in [35,80,125,170]) <line x1:"0",y1:string(y),x2:"620",y2:string(y),stroke:"#e8e1f0",["stroke-width"]:"1">]>
    <path d:"M 0 160 C 25 160 25 110 55 120 S 100 160 130 110 S 175 40 210 85 S 260 130 290 65 S 345 115 375 65 S 425 10 455 45 S 505 80 535 30 S 585 55 620 12 L 620 190 L 0 190 Z",fill:"url(#clay-chart-fill)">
    <path d:"M 0 160 C 25 160 25 110 55 120 S 100 160 130 110 S 175 40 210 85 S 260 130 290 65 S 345 115 375 65 S 425 10 455 45 S 505 80 535 30 S 585 55 620 12",fill:"none",stroke:"#8065de",["stroke-width"]:"4",["stroke-linecap"]:"round">>;

apply(<clay.page title:"Clay Studio · Lambda UI",
    <clay.flex direction:'vertical',align:"stretch",gap:28,style:"max-width:1200px;margin:auto;",
        <clay.flex justify:"space-between",wrap:true,
            <clay.space gap:14,
                <clay.avatar shape:'square',size:48,"λ">
                <clay.flex direction:'vertical',align:"flex-start",gap:0,
                    <clay.title level:3,style:"margin:0;","Clay Studio">
                    <clay.text style:"color:var(--clay-text-secondary);","A softer space to do great work.">>>
            <clay.space gap:16,
                <clay.tag color:"purple","lambda.ui.clay">
                <clay.button id:"clay-new",variant:'primary',icon:<clay.icon name:"plus">,"New project">>>
        <clay.flex align:"stretch",gap:28,
            <clay.card style:"width:202px;flex-shrink:0;",body_style:"padding:18px 12px;",
                <clay.text style:"display:block;padding:8px 12px;color:var(--clay-text-secondary);font-size:11px;font-weight:700;letter-spacing:1.5px;","WORKSPACE">
                <clay.menu id:"clay-nav",orientation:'vertical',items:[
                    {key:'overview',label:<clay.space gap:10,<clay.icon name:"home"> "Overview">},
                    {key:'analytics',label:<clay.space gap:10,<clay.icon name:"bar-chart"> "Analytics">},
                    {key:'projects',label:<clay.space gap:10,<clay.icon name:"folder"> "Projects">},
                    {key:'team',label:<clay.space gap:10,<clay.icon name:"team"> "People">},
                    {key:'settings',label:<clay.space gap:10,<clay.icon name:"setting"> "Settings">}]>
                <clay.divider>
                <clay.flex direction:'vertical',align:"stretch",gap:12,style:"padding:8px;",
                    <clay.avatar size:40,"AJ">
                    <clay.text strong:true,"Alex Johnson">
                    <clay.text type:'secondary',"Your next big idea starts here.">
                    <clay.button block:true,"Invite a teammate">>>
            <clay.flex direction:'vertical',align:"stretch",gap:24,style:"flex:1;min-width:0;",
                <clay.flex justify:"space-between",align:"center",wrap:true,
                    <clay.flex direction:'vertical',align:"flex-start",gap:4,
                        <clay.breadcrumb items:[{title:"Workspace"},{title:"Overview"}]>
                        <clay.title level:2,style:"margin:0;","Good morning, Alex.">
                        <clay.text type:'secondary',"Here’s what’s taking shape today.">>
                    <clay.segmented id:"clay-period",items:[{key:'week',label:"Week"},{key:'month',label:"Month"},{key:'year',label:"Year"}],default_value:'month'>>
                <clay.flex align:"stretch",gap:20,wrap:true,
                    <clay.card style:"flex:1;min-width:180px;background:var(--clay-lavender);",
                        <clay.statistic title:"Total revenue",value:24780,prefix:"$",precision:0>
                        <clay.text style:"color:#287b63;font-size:12px;","↗ 12.5% this month">>
                    <clay.card style:"flex:1;min-width:180px;background:var(--clay-mint);",
                        <clay.statistic title:"New customers",value:1248>
                        <clay.text style:"color:#287b63;font-size:12px;","↗ 8.2% this month">>
                    <clay.card style:"flex:1;min-width:180px;background:var(--clay-peach);",
                        <clay.statistic title:"Conversion rate",value:3.86,suffix:"%",precision:2>
                        <clay.text style:"color:#287b63;font-size:12px;","↗ 4.1% this month">>>
                <clay.flex align:"stretch",gap:24,wrap:true,
                    <clay.card title:"Revenue overview",extra:<clay.tag "+12.5%">,style:"flex:2;min-width:340px;",
                        <div trend>
                        <clay.flex justify:"space-between",style:"font-size:11px;color:var(--clay-text-secondary);margin-top:12px;",
                            <clay.text "Jan"> <clay.text "Feb"> <clay.text "Mar"> <clay.text "Apr"> <clay.text "May"> <clay.text "Jun">>>
                    <clay.card title:"Monthly goal",style:"flex:1;min-width:220px;",
                        <clay.flex direction:'vertical',align:"center",gap:12,
                            <clay.progress id:"clay-goal",type:'circle',percent:78,dimension:132,stroke_width:10>
                            <clay.text strong:true,"Looking good.">
                            <clay.text type:'secondary',"You’re well on your way.">>>>
                <clay.card title:"Recent orders",extra:<clay.button variant:'text',"View all →">,
                    <clay.table id:"clay-orders",pagination:false,columns:[
                        {key:'name',title:"Customer",sortable:true},{key:'project',title:"Project"},
                        {key:'status',title:"Status",render:(value,row,index) => <clay.tag color:if (value == "Paid") "green" else "orange",value>},
                        {key:'amount',title:"Amount",align:'right'}],rows:[
                        {key:1,name:"Olivia Chen",project:"Brand refresh",status:"Paid",amount:"$1,280"},
                        {key:2,name:"Noah Williams",project:"Mobile experience",status:"Pending",amount:"$2,450"},
                        {key:3,name:"Emma Davis",project:"Studio website",status:"Paid",amount:"$890"}]>>
                <clay.alert id:"clay-saved",status:'success',show_icon:true,message:"Everything is up to date.",description:"Your workspace is synced and ready for what’s next.",closable:true>>>
        <clay.flex justify:"space-between",wrap:true,
            <clay.text type:'secondary',"Soft surfaces. Serious components.">
            <clay.link href:"https://dribbble.com/shots/27508244-Clay-Dashboard-Claymorphism-Dashboard-UI",target:"_blank","Inspired by Clay Dashboard ↗">>>>)
