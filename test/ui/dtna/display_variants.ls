import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main style:"width:360px;",*[for (node in [
    <dtna.space gap:24, *[
        <dtna.badge id:"zero-hidden",count:0, "Inbox">,
        <dtna.badge id:"zero-shown",count:0,show_zero:true>,
        <dtna.badge id:"overflow-count",count:120>,
        <dtna.badge id:"processing",status:'processing',text:"Working">]>,
    <dtna.badge_ribbon id:"ribbon",placement:'start',text:"New",style:"margin-top:24px;", <dtna.card title:"Ribbon card", "Content">>,
    <dtna.descriptions id:"details",column:{xs:1,md:3},bordered:true,style:"margin-top:24px;",items:[
        {label:"First",children:"One"},{label:"Fill",span:'filled',children:"Two"},
        {label:"Wide",span:2,children:"Three"},{label:"Last",children:"Four"}]>,
    <dtna.descriptions id:"vertical",column:2,layout:'vertical',colon:false,size:'small',items:[
        {label:"Name",children:"Ada"},{label:"Role",children:"Owner"}]>
]) apply(node)]>>)
