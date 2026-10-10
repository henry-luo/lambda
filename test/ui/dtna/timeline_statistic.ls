import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main style:"width:480px;",*[for (node in [
    <dtna.timeline id:"alternate",mode:'alternate',pending:"Waiting",items:[
        {title:"09:00",content:"Created",color:"green"},{title:"10:00",content:"Processing",icon:<dtna.icon name:"clock-circle">},
        {title:"11:00",content:"Failed",color:"red"}]>,
    <dtna.timeline id:"horizontal",orientation:'horizontal',style:"margin:24px 0;",items:[{content:"One"},{content:"Two"},{content:"Three"}]>,
    <dtna.statistic id:"exact",title:"Balance",value:"-12345678901234567890.98765",precision:2,prefix:"$",suffix:"USD">,
    <dtna.statistic id:"pending",loading:true,title:"Loading">
]) apply(node)]>>)
