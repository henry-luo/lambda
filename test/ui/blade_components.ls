import ui: lambda.ui.blade

apply(<blade.page title:"Blade · Component forge",
    <blade.flex direction:'vertical',align:"stretch",gap:30,style:"max-width:1180px;margin:auto;",
        <blade.flex justify:"space-between",align:"center",wrap:true,
            <blade.title level:1,style:"margin:0;","The Component Forge">
            <blade.tag "64 components · 18 RPG icons">>
        <blade.paragraph type:'secondary',"A complete adventurer's toolkit: native fields, shared collection behavior, engraved frames and scalable vector equipment.">
        <blade.panel title:"I · Armory",material:'iron',
            <blade.flex wrap:true,gap:16,style:"margin-bottom:22px;",
                <blade.item_slot icon:'sword',rarity:'legendary',selected:true,label:"Legendary sword">
                <blade.item_slot icon:'shield',rarity:'epic',label:"Epic shield">
                <blade.item_slot icon:'armor',rarity:'rare',label:"Rare armor">
                <blade.item_slot icon:'bow',rarity:'uncommon',label:"Uncommon bow">
                <blade.item_slot icon:'potion-red',quantity:8,label:"Eight health potions">
                <blade.item_slot label:"Empty slot">
                <blade.item_slot icon:'ring',disabled:true,label:"Unavailable ring">>
            <blade.flex wrap:true,gap:20,
                <blade.ability icon:'fire',hotkey:"1",label:"Fireball">
                <blade.ability id:"forge-cooldown",icon:'ice',hotkey:"2",label:"Frostbolt cooling down",cooldown:65>
                <blade.ability icon:'heal',hotkey:"Q",label:"Heal">
                <blade.ability icon:'dash',hotkey:"W",label:"Dash">
                <blade.ability icon:'skull',hotkey:"R",disabled:true,label:"Execute locked">>
            <blade.divider "Resource meters">
            <blade.flex wrap:true,gap:24,
                <blade.meter id:"forge-health",resource:'health',value:840,max:1000,style:"flex:1;">
                <blade.meter resource:'mana',value:360,max:500,style:"flex:1;">>
            <blade.flex wrap:true,gap:24,
                <blade.meter resource:'stamina',value:75,max:100,style:"flex:1;">
                <blade.meter resource:'experience',value:2200,max:4000,style:"flex:1;">>>
        <blade.panel title:"II · Actions & inscriptions",
            <blade.space gap:14,wrap:true,
                <blade.button variant:'primary',"Accept quest">
                <blade.button "Inspect">
                <blade.button variant:'dashed',"Recruit">
                <blade.button danger:true,"Abandon">
                <blade.button disabled:true,"Locked">
                <blade.button loading:true,"Forging…">
                <blade.button variant:'text',"Read more">
                <blade.button icon:<blade.icon name:"plus">,shape:'circle',label:"Add">>
            <blade.divider>
            <blade.title level:3,"A chronicle written in steel">
            <blade.paragraph "The order's archives record every adventure. " <blade.text strong:true,"Courage endures.">>
            <blade.space wrap:true,
                <blade.text type:'secondary',"Secondary inscription">
                <blade.text code:true,"/equip sword">
                <blade.text mark:true,"Quest item">
                <blade.link href:"https://github.com/RonenNess/RPGUI","RPGUI inspiration ↗">>>
        <blade.panel title:"III · Create your adventurer",material:'parchment',
            <blade.form id:"forge-form",
                <blade.flex wrap:true,align:"stretch",gap:24,
                    <blade.flex direction:'vertical',align:"stretch",gap:0,style:"flex:1;min-width:240px;",
                        <blade.form_item label:"Character name",for:"forge-name",required:true,
                            <blade.input id:"forge-name",name:"name",default_value:"Aelric",placeholder:"Your name",required:true>>
                        <blade.form_item label:"Class",for:"forge-class",<blade.select id:"forge-class",name:"class",options:[
                            {value:'warden',label:"Dawnwarden"},{value:'ranger',label:"Ranger"},{value:'mage',label:"Arcanist"}]>>
                        <blade.form_item label:"Biography",for:"forge-bio",<blade.text_area id:"forge-bio",name:"biography",rows:3,placeholder:"Tell your story…">>>
                    <blade.flex direction:'vertical',align:"flex-start",gap:18,style:"flex:1;min-width:220px;",
                        <blade.checkbox default_checked:true,"Show quest markers">
                        <blade.space <blade.radio name:"forge-mode",value:'story',default_checked:true,"Story"> <blade.radio name:"forge-mode",value:'hard',"Hardcore">>
                        <blade.space <blade.switch label:"Enable notifications",default_checked:true> <blade.text "Quest notifications">>
                        <blade.rate default_value:4,label:"Adventure difficulty">
                        <blade.space <blade.button type:'submit',variant:'primary',"Begin adventure"> <blade.button type:'reset',"Reset">>>>>>
        <blade.panel title:"IV · The quest journal",
            <blade.breadcrumb items:[{title:"Ashenvale"},{title:"Silver Flame"},{title:"Quest journal"}]>
            <blade.tabs id:"forge-tabs",keep_mounted:true,items:[
                {key:'quests',label:"Quests",children:<blade.collapse id:"forge-collapse",default_active_keys:['oath'],items:[
                    {key:'oath',label:"The Warden's Oath",children:<blade.alert status:'info',show_icon:true,message:"Meet the archivist",description:"Your journey begins at the gates of Ashenvale.">},
                    {key:'ember',label:"Embers of the past",children:"Find the three lost sigils."}]>},
                {key:'regions',label:"Regions",children:<blade.tree id:"forge-tree",default_expanded_keys:['vale'],checkable:true,items:[
                    {key:'vale',label:"Ashenvale",children:[{key:'citadel',label:"The Citadel"},{key:'forest',label:"Emberwood"}]}]>},
                {key:'locked',label:"Guild quests",disabled:true}]>
            <blade.divider>
            <blade.steps current:1,items:[{title:"Discover"},{title:"Prepare"},{title:"Explore"}]>
            <blade.space style:"margin-top:24px;",wrap:true,
                <blade.segmented id:"forge-filter",items:[{key:'all',label:"All quests"},{key:'active',label:"Active"},{key:'done',label:"Complete"}]>
                <blade.checkable_tag "Track quest">
                <blade.pagination total:50,page_size:10>>>
        <blade.panel title:"V · Trading post",
            <blade.table id:"forge-table",pagination:false,selection:'multiple',columns:[
                {key:'name',title:"Item",sortable:true},{key:'rarity',title:"Rarity"},{key:'price',title:"Gold",sortable:true,align:'right'}],rows:[
                {key:1,name:"Iron Oath",rarity:"Legendary",price:1200},
                {key:2,name:"Mana draught",rarity:"Common",price:45},
                {key:3,name:"Azure signet",rarity:"Epic",price:850}]>
            <blade.divider>
            <blade.flex wrap:true,gap:28,
                <blade.statistic title:"Gold in your purse",value:24580,suffix:"G">
                <blade.descriptions items:[{label:"Guild",children:"The Silver Flame"},{label:"Rank",children:"Sentinel"}]>
                <blade.timeline items:[{children:"Arrived at Ashenvale"},{children:"Joined the order"}]>>>
        <blade.panel title:"VI · Messages from the realm",
            <blade.alert id:"forge-notice",status:'success',show_icon:true,message:"Quest completed",description:"You earned 250 experience and 80 gold.",closable:true>
            <blade.space gap:16,wrap:true,style:"margin-top:20px;",
                <blade.tag status:'success',"Complete"> <blade.tag status:'warning',"In progress"> <blade.tag status:'error',"Failed">
                <blade.badge count:3,<blade.avatar "AF">>
                <blade.spin tip:"Consulting the oracle…">>
            <blade.skeleton paragraph:{rows:2},active:true>
            <blade.empty image:'simple',description:"No more quests in this region.">
            <blade.config_provider id:"forge-frost-scope",tokens:{primary:"#78b4d4",gold:"#78b4d4",slot_size:88},
                <blade.space gap:24,<blade.button id:"forge-frost",variant:'primary',"Frost covenant"> <blade.item_slot icon:'ice',rarity:'rare'>>>
            <blade.config_provider disabled:true,style:"margin-top:20px;",<blade.button "Disabled by provider">>>
        <blade.panel title:"VII · Original vector icon collection",material:'iron',
            <blade.flex gap:24,wrap:true,*[for (name in ui.rpg_icon_names()) <blade.rpg_icon name:name,size:72,label:name>]>>
        <blade.text type:'secondary',"Ordinary Lambda source. Native interaction. Resolution-independent ornament.">>>)
