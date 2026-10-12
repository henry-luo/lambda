import t: lambda.chart.timeline
import tables: lambda.chart.export
let spec={id:"export-history",view:"cards",source:{revision:"edition-2"},events:[
    {id:"ancient",at:{year:-43,precision:"year"},title:"École, history",metadata:{exact:i64("9007199254740993")}},
    {id:"modern",at:"2020-01",title:"A \"quoted\" title\nSecond line"}]}
let initial=t.initial_state(spec)
let data=t.export_data(spec)
let csv=t.export_data(spec,"csv",{fields:["id","title"],headings:{id:"Identifiant",title:"Titre"}})
let selected=t.export_data(spec,"json",{scope:"selected"},{*:initial,selected_ids:["modern"]})
let windowed=t.export_data(spec,"json",{scope:"window"},{*:initial,window:{from:0,to:2000000000000}})
let xlsx=t.export_data(spec,"xlsx",{fields:["id","at","metadata"]})
let rich={*:spec,events:[{id:"rich",title:"Rich",content:<strong "Text">}]}
let html=t.export_data(spec,"html",{fields:["id","title"]});
let media_spec={events:[{id:"film",title:"Film",media:[{src:"film.mp4",poster:"poster.png"}]}]};
let poster=t.export_document(media_spec);
let unsupported_media={events:[{id:"film",media:[{src:"film.mp4"}]}]};
let remote={id:"remote-export",source:{url:"history.json",revision:"r2",format:"json"}};
let loaded={resources:{source:{status:"ready",revision:"r2",result:{events:[{id:"loaded",at:"2000"}]}}}};
let intro_media={view:"story",spacing:"sequence",story:{title_slide:{id:"intro",title:"Film introduction",
    media:[{src:"intro.mp4",poster:"intro-poster.png"}]}},events:[{id:"event",title:"History"}]};
let intro_poster=format(t.export_document(intro_media),'html');
let intro_missing={*:intro_media,story:{title_slide:{id:"intro",media:[{src:"intro.mp4"}]}}};
[
 ["stable source ID and revision",data.chart_id=="export-history" and data.source.revision=="edition-2"],
 ["original date precision",data.events[0].at=={year:-43,precision:"year"} and data.events[1].at=="2020-01"],
 ["exact metadata",data.events[0].metadata.exact==i64("9007199254740993")],
 ["no internal geometry",data.events[0].start==null and data.events[0].index==null and data.events[0].display_date_authored==null],
 ["localized ordered headings",starts_with(csv,"\"Identifiant\",\"Titre\"\r\n")],
 ["CSV escaping",contains(csv,"\"École, history\"") and contains(csv,"\"A \"\"quoted\"\" title\nSecond line\"")],
 ["selected dataset",len(selected.events)==1 and selected.events[0].id=="modern"],
 ["visible window dataset",len(windowed.events)==1 and windowed.events[0].id=="modern"],
 ["spreadsheet package",name(xlsx)=='fs' and len(content(xlsx))==3],
 ["spreadsheet column addressing",tables.column_name(25)=="Z" and tables.column_name(26)=="AA" and tables.column_name(16383)=="XFD"],
 ["accessible HTML table",name(html)=='table' and name(html[1])=='thead' and html[1][0][0].scope=="col"],
 ["rich export explicit",t.export_data(rich) is error],
 ["rich export conversion",t.export_data(rich,"json",{unsupported:"text"}).events[0].content=="Text"],
 ["rich export omission",t.export_data(rich,"json",{unsupported:"omit"}).events[0].content==null],
 ["invalid worksheet name",t.export_data(spec,"xlsx",{sheet_name:"bad/name"}) is error],
 ["duplicate headings",t.export_data(spec,"csv",{fields:["id","title"],headings:{id:"Same",title:"Same"}}) is error],
 ["unknown export format",t.export_data(spec,"unknown") is error],
 ["unknown scope",t.export_data(spec,"json",{scope:"unknown"}) is error],
 ["frozen document",name(t.export_document(spec,"html",{width:800,height:600}))=='html'],
 ["SVG full domain",name(t.export_document({*:spec,view:"axis"},"svg",{width:800,height:600},{scope:"full"}))=='svg'],
 ["static media uses poster",contains(format(poster,'html'),"poster.png") and not contains(format(poster,'html'),"<video")],
 ["introduction media uses poster",contains(intro_poster,"intro-poster.png") and not contains(intro_poster,"<video")],
 ["introduction media needs snapshot policy",t.export_document(intro_missing) is error],
 ["introduction media diagnostic",contains(format(t.export_document(intro_missing,"html",null,{unsupported:"text"}),'html'),"Unsupported media snapshot")],
 ["unsupported media explicit",t.export_document(unsupported_media) is error],
 ["unsupported media visible diagnostic",contains(format(t.export_document(unsupported_media,"html",null,{unsupported:"text"}),'html'),"Unsupported media snapshot")],
 ["external resources require policy",t.export_document({events:[{media:[{src:"https://example.test/photo.png"}]}]}) is error],
 ["loaded source export",t.export_data(remote,"json",{},loaded).events[0].id=="loaded"],
 ["partial snapshot retains defaults",t.export_data(spec,"json",{snapshot:{active_id:"modern"}}).snapshot.active_id=="modern"],
 ["snapshot source admission",t.export_data(remote,"json",{snapshot:loaded}).events[0].id=="loaded"],
 ["unknown options rejected",t.export_data(spec,"json",{typo:true}) is error],
 ["invalid density rejected",t.export_document(spec,"html",null,{scale:0}) is error],
 ["procedural formatter rejected",t.export_data(spec,"csv",{date_formatter:pn(value)=>{value}}) is error],
 ["rich SVG preserves text",contains(format(t.export_document(rich,"svg",{width:600,height:400}),'xml'),"Text")],
 ["full card SVG grows",float(t.export_document({events:[for (index in 0 to 14) {id:string(index),title:"Event " ++ string(index),content:<p "Details">}]},
    "svg",{width:600,height:400},{scope:"full"}).height)>400]
]
