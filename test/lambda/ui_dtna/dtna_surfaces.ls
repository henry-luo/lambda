import ui: lambda.ui.dtna
let card = ui.render(ui.card({title:0,extra:"More",cover:<img src:"cover.png",alt:"Cover">,actions:["Edit","Save"],size:'small',bordered:false,hoverable:true},"Body")^)
let meta = ui.render(ui.card_meta({title:"Title",description:"Description",avatar:ui.avatar({},"A")^})^)
let loading = ui.render(ui.card({loading:true},"Hidden")^)
let tabs = ui.render(ui.card({id:"tabs",tab_list:[{key:'a',label:"A",children:"Alpha"},{key:'b',label:"B",children:"Beta"}],default_active_tab:'b'})^)
let empty = ui.render(ui.empty({locale:'zh-CN',image:'simple'})^)
let custom = ui.render(ui.empty({description:false,image:<span "Custom">},"Action")^)
let no_image = ui.render(ui.empty({image:false,description:0})^)
let exception = ui.render(ui.result({status:'404',title:"Missing",subtitle:"Try again",extra:"Home"},"Details")^)
let success = ui.render(ui.result({status:'success',title:0})^);
[
    ["card zero title",content(content(content(card)[0])[0])[0] == "0"],
    ["card variants",contains(card.class,"dtna-card-borderless") and contains(card.class,"dtna-card-hoverable") and contains(card.class,"dtna-size-small")],
    ["card cover",name(content(content(card)[1])[0]) == 'img'],
    ["card actions",name(content(card)[3]) == 'ul' and len(content(content(card)[3])) == 2],
    ["card meta",len(content(meta)) == 2 and len(content(content(meta)[1])) == 2],
    ["card loading",loading["aria-busy"] == "true" and name(content(content(loading)[0])[0]) == 'div'],
    ["card tabs",content(content(content(content(tabs)[0])[0])[1][0])[0] == "Beta"],
    ["empty locale",content(content(empty)[1])[0] == "暂无数据"],
    ["simple illustration",content(content(empty)[0])[0].viewBox == "0 0 64 41"],
    ["custom illustration",name(content(content(custom)[0])[0]) == 'span' and content(content(custom)[1])[0] == "Action"],
    ["no illustration",len(content(no_image)) == 1 and content(content(no_image)[0])[0] == "0"],
    ["exception illustration",content(content(exception)[0])[0].viewBox == "0 0 252 294" and len(content(exception)) == 5],
    ["status icon",content(content(success)[0])[0].fill == "currentColor" and content(content(success)[1])[0] == "0"],
    ["invalid card",(ui.card({actions:1}) or null) == null and (ui.card({loading:1}) or null) == null and (ui.card({type:'bad'}) or null) == null],
    ["invalid tabs",(ui.card({tab_list:[]}) or null) == null and (ui.card({id:"x",tab_list:[{key:1,label:"One"}],active_tab:2}) or null) == null],
    ["invalid display",(ui.empty({image_height:-1}) or null) == null and (ui.empty({locale:'bad'}) or null) == null and (ui.result({status:'bad'}) or null) == null]
]
