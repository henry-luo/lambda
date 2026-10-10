import ui: lambda.ui.dtna
ui.page(<main style:"width:760px;",*ui.render([
    ui.card({id:"card",title:"Profile",extra:ui.link({href:"#"},"More")^,size:'small',actions:["Edit","Save"]},
        ui.card_meta({title:"Ada",description:"Owner",avatar:ui.avatar({},"A")^})^)^,
    ui.card({id:"tabs",style:"margin-top:16px;",tab_list:[
        {key:'a',label:"A",children:"Alpha"},{key:'b',label:"B",children:"Beta"},{key:'c',label:"C",children:"Gamma",disabled:true}]})^,
    ui.card({id:"grid",style:"margin-top:16px;"},[ui.card_grid({},"One")^,ui.card_grid({},"Two")^,ui.card_grid({},"Three")^])^,
    ui.empty({id:"empty",image:'simple',locale:'zh-CN',style:"margin-top:24px;"},ui.button({},"Create")^)^,
    ui.result({id:"result",status:'404',title:"404",subtitle:"Page missing",extra:ui.button({variant:'primary'},"Home")^})^
])>,{tokens:{font_family:"Liberation Sans"}})^
