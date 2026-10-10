import ui: lambda.ui.dtna
ui.page(<main style:"width:500px;",*ui.render([
    *[for (level in 1 to 5) ui.title({id:"heading-" ++ string(level),level:level},"Heading " ++ string(level))^],
    ui.paragraph({id:"body"},[
        ui.text({strong:true},"Strong")^," ",ui.text({italic:true},"Italic")^," ",
        ui.text({underline:true},"Underline")^," ",ui.text({delete:true},"Deleted")^," ",
        ui.text({code:true},"Code")^," ",ui.text({mark:true},"Mark")^," ",ui.text({keyboard:true},"Ctrl+C")^])^,
    ui.text({id:"danger",type:'danger'},"Danger")^,
    ui.link({id:"disabled-link",disabled:true,href:"#target"},"Disabled link")^,
    <div id:"target",style:"margin-top:100px;","Target">
])>,{tokens:{font_family:"Liberation Sans"}})^
