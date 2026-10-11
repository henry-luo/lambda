import ui: lambda.ui.dtna
apply(<dtna.page <main style:"width:400px;",*[for (node in [
    <dtna.row id:"responsive-row",gutter:[{xs:16,md:24},12], *[
        <dtna.col id:"responsive-left",xs:24,md:12,xxxl:6, <div style:"height:20px;","Left">>,
        <dtna.col id:"responsive-right",xs:24,md:12,xxxl:6, <div style:"height:20px;","Right">>]>,
    <dtna.row id:"ordered",style:"margin-top:24px;", *[
        <dtna.col id:"offset",span:6,offset:6,order:2, <div style:"height:20px;","Second">>,
        <dtna.col id:"first",span:6,order:1, <div style:"height:20px;","First">>]>,
    <div dir:"rtl",apply(<dtna.row style:"margin-top:24px;", <dtna.col id:"rtl-offset",span:6,offset:6, <div style:"height:20px;","RTL">>>)>,
    <dtna.row style:"margin-top:24px;", <dtna.col id:"hidden-column",xs:0,md:8, <div style:"height:20px;","Responsive visibility">>>,
    // separate text elements retain Space item boundaries (S2.6.4).
    <dtna.space id:"split-space",separator:"/",style:"margin-top:24px;", *[
        <dtna.text "One">,null,<dtna.text "Two">,<dtna.text "Three">]>,
    <dtna.space id:"wrapped",gap:8,wrap:true,style:"width:120px;margin-top:24px;", *[
        <div style:"width:60px;height:20px;","One">,<div style:"width:60px;height:20px;","Two">]>,
    <dtna.space_compact id:"vertical-compact",direction:'vertical',style:"margin-top:24px;", *[
        <dtna.button id:"compact-first", "First">,<dtna.button id:"compact-last", "Last">]>,
    <dtna.divider id:"labelled",placement:'start',dashed:true,plain:true, "Section">,
    <dtna.divider id:"vertical-divider",direction:'vertical',dashed:true>
]) apply(node)]>>)
