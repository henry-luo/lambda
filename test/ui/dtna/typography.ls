import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main style:"width:500px;",*[for (node in [
    *[for (level in 1 to 5) <dtna.title id:"heading-" ++ string(level),level:level, "Heading " ++ string(level)>],
    <dtna.paragraph id:"body", *[
        <dtna.text strong:true, "Strong">," ",<dtna.text italic:true, "Italic">," ",
        <dtna.text underline:true, "Underline">," ",<dtna.text delete:true, "Deleted">," ",
        <dtna.text code:true, "Code">," ",<dtna.text mark:true, "Mark">," ",<dtna.text keyboard:true, "Ctrl+C">]>,
    <dtna.text id:"danger",type:'danger', "Danger">,
    <dtna.link id:"disabled-link",disabled:true,href:"#target", "Disabled link">,
    <div id:"target",style:"margin-top:100px;","Target">
]) apply(node)]>>)
