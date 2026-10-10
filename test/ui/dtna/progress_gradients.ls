import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main style:"width:200px;",*[for (node in [
    <dtna.progress id:"line-gradient",percent:100,show_info:false,stroke_width:16,stroke_linecap:'butt',stroke_color:{from:"red",to:"blue"}>,
    <dtna.progress id:"ring-gradient",percent:75,type:'circle',dimension:100,style:"margin-top:24px;",stroke_color:{["0%"]:"red",["50%"]:"green",["100%"]:"blue"}>
]) apply(node)]>>)
