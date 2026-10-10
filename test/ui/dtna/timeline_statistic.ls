import ui: lambda.ui.dtna
ui.page(<main style:"width:480px;",*ui.render([
    ui.timeline({id:"alternate",mode:'alternate',pending:"Waiting",items:[
        {title:"09:00",content:"Created",color:"green"},{title:"10:00",content:"Processing",icon:ui.icon({name:"clock-circle"})^},
        {title:"11:00",content:"Failed",color:"red"}]})^,
    ui.timeline({id:"horizontal",orientation:'horizontal',style:"margin:24px 0;",items:[{content:"One"},{content:"Two"},{content:"Three"}]})^,
    ui.statistic({id:"exact",title:"Balance",value:"-12345678901234567890.98765",precision:2,prefix:"$",suffix:"USD"})^,
    ui.statistic({id:"pending",loading:true,title:"Loading"})^
])>,{tokens:{font_family:"Liberation Sans"}})^
