import ui: lambda.ui.dtna
ui.page(<main style:"width:360px;",*ui.render([
    ui.progress({id:"line",percent:40,success:{percent:15}})^,
    ui.progress({id:"steps",percent:60,steps:5,stroke_color:["red","orange","green"],style:"margin-top:24px;"})^,
    ui.progress({id:"circle",type:'circle',percent:40,dimension:100,style:"margin-top:24px;"})^,
    ui.progress({id:"dashboard",type:'dashboard',percent:70,dimension:100,gap_degree:90})^,
    ui.progress({id:"complete",percent:100})^,
    ui.progress({id:"failure",percent:45,status:'exception'})^
])>,{tokens:{font_family:"Liberation Sans"}})^
