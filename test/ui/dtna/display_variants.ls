import ui: lambda.ui.dtna
ui.page(<main style:"width:360px;",*ui.render([
    ui.space({gap:24},[
        ui.badge({id:"zero-hidden",count:0},"Inbox")^,
        ui.badge({id:"zero-shown",count:0,show_zero:true})^,
        ui.badge({id:"overflow-count",count:120})^,
        ui.badge({id:"processing",status:'processing',text:"Working"})^])^,
    ui.badge_ribbon({id:"ribbon",placement:'start',text:"New",style:"margin-top:24px;"},
        ui.card({title:"Ribbon card"},"Content")^)^,
    ui.descriptions({id:"details",column:{xs:1,md:3},bordered:true,style:"margin-top:24px;",items:[
        {label:"First",children:"One"},{label:"Fill",span:'filled',children:"Two"},
        {label:"Wide",span:2,children:"Three"},{label:"Last",children:"Four"}]})^,
    ui.descriptions({id:"vertical",column:2,layout:'vertical',colon:false,size:'small',items:[
        {label:"Name",children:"Ada"},{label:"Role",children:"Owner"}]})^
])>,{tokens:{font_family:"Liberation Sans"}})^
