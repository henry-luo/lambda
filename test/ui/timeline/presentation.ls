import t: lambda.chart.timeline
let model=t.model({id:"presentation",direction:"rtl",orientation:"horizontal",theme:"dark",
    interaction:{multiselect:true,contiguous:true},cards:{width:220,borderless:true,title_align:"center",date_align:"end",body_align:"start"},
    parts:{card_title:{render:(item,fallback)=><h4 class:"custom-heading",item.title>}},
    events:[{id:"a",title:"First",at:"1900",content:<p "First details">,size:160,appearance:{shape:"diamond",fill:"#ff0000"}},
        {id:"b",title:"Second",at:"1950",content:"Second details"},{id:"c",title:"Third",at:"2000",content:"Third details"}]},{width:800,height:500});
<html <head <style "body{margin:20px;font-family:Arial}h4{margin:4px 0}">> <body apply(model)>>
