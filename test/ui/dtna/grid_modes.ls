import ui: lambda.ui.dtna
fn cell() => <div style:"height:20px;width:0;">
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main id:"grid-main",style:"width:400px;",*[for (node in [
    <dtna.row id:"units-row",gutter:[{xs:"1rem",md:"24px"},"1em"], *[
        <dtna.col id:"units-left",span:12, <div id:"units-inner",style:"height:20px;">>,
        <dtna.col span:12, cell()>]>,
    <dtna.row id:"align-row",align:{xs:'top',md:'middle',lg:'bottom'},justify:{xs:'start',md:'center',lg:'end'},style:"height:80px;margin-top:16px;", <dtna.col id:"align-cell",span:6, cell()>>,
    <dtna.row id:"shift-row",style:"margin-top:16px;", *[
        <dtna.col id:"push-cell",span:8,push:8,md:{push:0}, cell()>,
        <dtna.col id:"pull-cell",span:8,pull:8,md:{pull:0}, cell()>]>,
    <div dir:"rtl",apply(<dtna.row id:"rtl-shift-row",style:"margin-top:16px;", *[
        <dtna.col id:"rtl-push",span:8,push:8,md:{push:0}, cell()>,
        <dtna.col id:"rtl-pull",span:8,pull:8,md:{pull:0}, cell()>]>)>,
    <dtna.row id:"flex-row",wrap:false,style:"margin-top:16px;", *[
        <dtna.col id:"fixed-flex",flex:"100px", cell()>,
        <dtna.col id:"one-flex",flex:1, cell()>,
        <dtna.col id:"two-flex",flex:2, cell()>]>,
    <dtna.row id:"responsive-flex-row",wrap:false,style:"margin-top:16px;", *[
        <dtna.col id:"responsive-flex-one",xs:{flex:"80px"},md:{flex:1}, cell()>,
        <dtna.col id:"responsive-flex-three",xs:{flex:1},md:{flex:3}, cell()>]>,
    <dtna.row id:"nested-row",gutter:16,style:"margin-top:16px;", <dtna.col id:"nested-col",span:12, <dtna.row id:"nested-inner-row",gutter:8, *[
            <dtna.col id:"nested-inner-col",span:12, cell()>,<dtna.col span:12, cell()>]>>>,
    <dtna.flex id:"flex-container",gap:"1rem",style:"width:400px;margin-top:16px;", *[
        <div id:"flex-fixed",style:"width:80px;height:20px;">,
        <dtna.flex id:"flex-growing",flex:1, cell()>]>
]) apply(node)]>>)
