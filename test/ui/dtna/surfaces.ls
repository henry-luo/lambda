import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main style:"width:760px;",*[for (node in [
    <dtna.card id:"card",title:"Profile",extra:<dtna.link href:"#", "More">,size:'small',actions:["Edit","Save"], <dtna.card_meta title:"Ada",description:"Owner",avatar:<dtna.avatar "A">>>,
    <dtna.card id:"tabs",style:"margin-top:16px;",tab_list:[
        {key:'a',label:"A",children:"Alpha"},{key:'b',label:"B",children:"Beta"},{key:'c',label:"C",children:"Gamma",disabled:true}]>,
    <dtna.card id:"grid",style:"margin-top:16px;", *[<dtna.card_grid "One">,<dtna.card_grid "Two">,<dtna.card_grid "Three">]>,
    <dtna.empty id:"empty",image:'simple',locale:'zh-CN',style:"margin-top:24px;", <dtna.button "Create">>,
    <dtna.result id:"result",status:'404',title:"404",subtitle:"Page missing",extra:<dtna.button variant:'primary', "Home">>
]) apply(node)]>>)
