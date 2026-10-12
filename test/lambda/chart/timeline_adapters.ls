import t: lambda.chart.timeline
let knight=t.convert({events:[{unique_id:"rome",start_date:{year:"-44",month:"3",day:"15"},text:{headline:"Rome",text:"A historical event"},group:"History"}]},"timelinejs")
let chrono=t.convert([{id:"one",title:"2019",cardTitle:"Idea",cardDetailedText:"Details",media:{type:"IMAGE",source:{url:"idea.png"}}}],"chrono")
let vis=t.convert({items:[{id:1,start:0,end:86400000,content:"Range",group:1}],groups:[{id:1,content:"Group"}]},"vis")
let json=t.convert("[{\"id\":\"a\",\"at\":\"2020\",\"title\":\"Hello\"}]","json");
let csv=t.convert("id,at,title\na,2020,Hello\n","csv")
let tsv=t.convert("id\tat\ttitle\na\t2020\tHello\n","tsv")
let sheet=t.convert("Year,Month,Day,Time,End Year,Headline,Text,Media,Type,Group\n-44,3,15,12:30,,Rome,Body,rome.jpg,,History\n1900,,,,2000,Century,,,,\n","timelinejs_csv")
let gviz=t.convert({table:{cols:[{label:"Year"},{label:"Headline"}],rows:[{c:[{v:2020},{v:"Idea"}]}]}},"sheets");
[
 ["TimelineJS BCE",t.validate(knight).events[0].start.year == -43],
 ["TimelineJS narrative",knight.view=="story" and knight.events[0].title=="Rome" and knight.groups[0].id=="History"],
 ["Chrono card",chrono.events[0].title=="Idea" and chrono.events[0].display_date=="2019"],
 ["Chrono media",chrono.events[0].media[0].kind=="image" and chrono.events[0].media[0].src=="idea.png"],
 ["vis epoch milliseconds",t.validate(vis).events[0].start.value==0 and t.validate(vis).events[0].end.value==86400000],
 ["vis normalized group IDs",vis.events[0].group=="1" and vis.groups[0].id=="1"],
 ["JSON records",t.validate(json).events[0].title=="Hello"],
 ["CSV records",csv.data==json.data],
 ["TSV records",tsv.data==json.data],
 ["spreadsheet dates",t.validate(sheet).events[0].start.year == -43 and t.validate(sheet).events[0].start.hour==12],
 ["spreadsheet ranges",t.validate(sheet).events[1].kind=="range"],
 ["Google table",gviz.events[0].title=="Idea"],
 ["malformed JSON",t.convert("[","json") is error],
 ["unknown adapter",t.convert([],"missing") is error],
 ["input unchanged",knight.events[0].metadata.timelinejs.start_date.year=="-44"]
]
