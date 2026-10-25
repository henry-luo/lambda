// D7.2.5: Insert dialogs retain typed settings and use existing Word catalogs.
import charts: ~~.word.mod_chart
import smartart: ~~.word.mod_smartart
import wordart: ~~.word.mod_wordart
import shapes: ~~.word.mod_shapes
import tables: ~~.word.mod_table_design
import equation_catalog: ~~.word.mod_equation_catalog
import symbol_sets: ~~.mod_symbols

fn field(id,label,kind="text",options=[]) => {id:id,label:label,kind:kind,options:options}
fn options(items) => [for (item in items) {value:item.id,label:item.label}]
fn choices(items) => [for (item in items) {value:item,label:item}]
pub let symbols = symbol_sets.symbols
pub let equations = equation_catalog.examples
pub let specs = [
    {id:"table",label:"Table",icon:"table",defaults:{rows:3,columns:3,header:true,style:"blue",data:"Team\tQ1\tQ2\nDesign\t12\t18\nBuild\t9\t15"},fields:[
        field("rows","Rows","number"),field("columns","Columns","number"),field("header","Header row","checkbox"),
        field("style","Table style","select",options(tables.styles)),field("data","Cell data (tab-separated rows)","textarea")]},
    {id:"picture",label:"Picture",icon:"album",defaults:{path:"",fit:"contain"},fields:[field("path","Image file path or URL"),field("fit","Fit","select",choices(["contain","cover","fill"]))]},
    {id:"screenshot",label:"Screenshot",icon:"album",defaults:{path:"",fit:"contain",mode:"file"},help:"Insert an existing screenshot, or capture the current slide as a picture.",fields:[field("mode","Source","select",[{value:"file",label:"Screenshot image file"},{value:"slide",label:"Current slide"}]),field("path","Screenshot file path (File source)"),field("fit","Fit","select",choices(["contain","cover","fill"]))]},
    {id:"shape",label:"Shape",icon:"shapes",defaults:shapes.defaults,fields:[field("kind","Shape","select",options(shapes.catalog)),field("text","Text"),field("fill","Fill color"),field("stroke","Outline color")]},
    {id:"smartart",label:"SmartArt",icon:"smartart",defaults:smartart.defaults,fields:[field("layout","Layout","select",options(smartart.layouts)),field("text","One label per line","textarea"),field("color","Color")]},
    {id:"chart",label:"Chart",icon:"chart",defaults:charts.defaults,fields:[field("type","Chart type","select",options(charts.types)),field("title","Title"),field("data","Label, Value — one per line","textarea"),field("color","Color")]},
    {id:"zoom",label:"Slide Zoom",icon:"object_position",defaults:{target:"",label:""},fields:[field("target","Destination slide","slide"),field("label","Caption (optional)")]},
    {id:"link",label:"Link",icon:"link",defaults:{label:"",href:"",target:""},fields:[field("label","Display text"),field("href","URL (leave empty to link to a slide)"),field("target","Destination slide","slide")]},
    {id:"action",label:"Action",icon:"star",defaults:{label:"Next slide",command:"next",target:""},fields:[field("label","Button label"),field("command","Action","select",[
        {value:"next",label:"Next build / slide"},{value:"previous",label:"Previous"},{value:"home",label:"First slide"},{value:"end",label:"Last slide"},{value:"go",label:"Go to slide"}]),field("target","Destination slide","slide")]},
    {id:"comment",label:"Comment",icon:"comment",defaults:{author:"Author",text:""},fields:[field("author","Author"),field("text","Comment","textarea")]},
    {id:"footer",label:"Header & Footer",icon:"footer",defaults:{header:"",footer:"",date_text:"",show_number:true,all_slides:true},fields:[field("header","Header"),field("footer","Footer"),field("date_text","Date or other right-aligned text"),field("show_number","Show slide number","checkbox"),field("all_slides","Apply to all slides","checkbox")]},
    {id:"wordart",label:"WordArt",icon:"wordart",defaults:wordart.defaults,fields:[field("text","Text"),field("style","Style","select",choices(wordart.styles)),field("transform","Text transform","select",choices(wordart.transforms)),field("font","Font family"),field("fill","Fill color"),field("outline","Outline color")]},
    {id:"date",label:"Date & Time",icon:"calendar",defaults:{text:""},fields:[field("text","Date / time text")]},
    {id:"number",label:"Slide Number",icon:"page_number",defaults:{all_slides:true},fields:[field("all_slides","Apply to all slides","checkbox")]},
    {id:"object",label:"Object",icon:"document",defaults:{format:"mark",source:"<div style: \"padding:24px;background:#eff6ff;border:1px solid #bfdbfe;\", <h2 \"Embedded content\"> <p \"Edit this content in the object dialog.\">>",path:""},fields:[field("format","Content format","select",choices(["mark","html","svg"])),field("path","File path (optional; replaces source below)"),field("source","HTML, SVG or Mark content","textarea")]},
    {id:"equation",label:"Equation",icon:"equation",defaults:{source:"a^2+b^2=c^2",size:32,color:"#24334b",display:true,justify:"center",linear:false,number:""},fields:[field("source","LaTeX","textarea"),field("size","Font size","number"),field("color","Color")]},
    {id:"symbol",label:"Symbol",icon:"symbol",defaults:{text:"λ"},fields:[field("text","Symbol / text")]},
    {id:"video",label:"Video",icon:"film",defaults:{path:"",poster:"",loop:false},fields:[field("path","Video file path or URL"),field("poster","Poster image (optional)"),field("loop","Loop playback","checkbox")]},
    {id:"audio",label:"Audio",icon:"sound",defaults:{path:"",loop:false},fields:[field("path","Audio file path or URL"),field("loop","Loop playback","checkbox")]}
]
pub fn spec(kind) => [for (entry in specs where entry.id == kind) entry][0]
