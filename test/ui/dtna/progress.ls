import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main style:"width:360px;",*[for (node in [
    <dtna.progress id:"line",percent:40,success:{percent:15}>,
    <dtna.progress id:"steps",percent:60,steps:5,stroke_color:["red","orange","green"],style:"margin-top:24px;">,
    <dtna.progress id:"circle",type:'circle',percent:40,dimension:100,style:"margin-top:24px;">,
    <dtna.progress id:"dashboard",type:'dashboard',percent:70,dimension:100,gap_degree:90>,
    <dtna.progress id:"complete",percent:100>,
    <dtna.progress id:"failure",percent:45,status:'exception'>
]) apply(node)]>>)
