import t: lambda.chart.timeline
import d: lambda.chart.timeline.date
import c: lambda.chart.timeline.common
import l: lambda.chart.timeline.locale
import schema: lambda.chart.timeline.schema

let french=schema.validate({locale:"fr-FR",events:[{id:"a",at:"2026-10-11"}]});
let arabic=schema.validate({locale:"ar-EG",events:[{id:"a",at:"2026-10-11"}]});
let chinese=schema.validate({locale:"zh-CN",events:[{id:"a",at:{year:-43,month:3,day:15}}]});
let axis=t.layout({id:"locale-axis",view:"axis",width:700,height:300,locale:"fr-FR",
    window:{from:"2026-01-01",to:"2026-04-01"},axes:[{tick_unit:"month",format:"MMM YYYY",collision:"none"}],
    events:[{id:"a",at:"2026-02-01"}]},null);
let french_clock=d.normalize("2026-10-11T13:14:15.025+08:00",{locale:"fr-FR",format:"dddd D MMMM YYYY [à] HH:mm:ss.SSS Z"});
let formatter=(record,context)=>context.locale ++ ":" ++ context.precision ++ ":" ++ string(record.year);
let custom=d.normalize("2026-10",{locale:"fr-FR",formatter:formatter});
let native=d.from_coordinate(t'2026-10-11T05:14:15.025z'.unix,{locale:"fr-FR",timezone:480,
    format:"dddd D MMMM YYYY [à] HH:mm:ss.SSS Z"});
let custom_scale=schema.validate({locale:"zh-CN",scales:[{id:"time",locale:"fr-FR"}],events:[{at:"2026-10-11"}]});
fn invalid_formatter(record, context) error => error("format failed")
let explicit=d.normalize({year:2026,display_date:"Authored"},{formatter:invalid_formatter});
let numbers={kind:"cosmological",unit:"year",locale:"fr-FR"};
let all_keys=["first","previous","next","last","fit","play","pause","resume","stop","zoom_in","zoom_out","reset","quick_jump","layout","theme",
    "cards","axis","story","light","dark","high_contrast","navigation","groups","search","no_results","matches","previous_match","next_match",
    "filter_groups","filter_tags","filter_from","filter_to","clear_filters",
    "read_more","read_less","source_error","loading","empty","bookmark","window_from","window_to","overview","progress","overall_progress","move_event",
    "move_group","resize_start","resize_end","remove_event","pending_edit","edit_instructions","edit_preview","edit_accept","edit_cancel",
    "cluster_events","compressed_interval","inside_compressed","visible_events","week_number",
    "export_id","export_display_date","export_at","export_from","export_to","export_title","export_subtitle","export_content","export_group","export_subgroup","export_tags","export_metadata","export_sheet"];
[
 ["locale tag variants",l.language("FR-ca")=="fr" and l.language("zh_Hant_TW")=="zh"],
 ["unsupported language has deterministic fallback",l.language("xx-Unknown")=="en" and c.message({locale:"xx"},"first","First")=="First"],
 ["all translated control and status messages",all([for (locale in ["fr","zh","ar"]) for (key in all_keys) c.message({locale:locale},key,"MISSING")!="MISSING"])],
 ["localized interpolation",c.message({locale:"ar"},"matches","",{count:12})=="١٢ حدثًا مطابقًا"],
 ["French singular count",c.message({locale:"fr"},"matches","",{count:1})=="1 événement correspondant"],
 ["Arabic dual count",c.message({locale:"ar"},"matches","",{count:2})=="حدثان مطابقان"],
 ["authored plural messages",c.message({locale:"en",messages:{matches:{one:"One result",other:"{count} results"}}},"matches","",{count:1})=="One result"],
 ["authored message overrides translation",c.message({locale:"fr",messages:{first:"Commencer"}},"first","First")=="Commencer"],
 ["callback receives exact raw parameters",c.message({locale:"ar",messages:{matches:(parameters)=>string(parameters.count)}},"matches","",{count:12})=="12"],
 ["French exact grouping",l.format_number(100000000000000000001.25m,"fr",{group:true})=="100\u202f000\u202f000\u202f000\u202f000\u202f000\u202f001,25"],
 ["Arabic exact grouping",l.format_number(-12345.25m,"ar",{group:true})=="-١٢٬٣٤٥٫٢٥"],
 ["pattern grouping is applied once",l.format_number(1234.25,"fr",{group:true,pattern:",.2f"})=="1\u202f234,25"],
 ["numeric pattern uses existing chart formatter",l.format_number(0.25,"fr",{pattern:".1%"})=="25,0%"],
 ["scientific coordinates preserve exponent",l.format_number(1.25e20,"ar")=="١٫٢٥e+٢٠"],
 ["French date precision",french.events[0].display_date=="11/10/2026" and d.normalize("2026-10",{locale:"fr"}).label=="10/2026"],
 ["Arabic dates and automatic RTL",arabic.events[0].display_date=="١١/١٠/٢٠٢٦" and arabic.direction=="rtl"],
 ["explicit direction overrides locale",schema.validate({locale:"ar",direction:"ltr"}).direction=="ltr"],
 ["Chinese historical era",chinese.events[0].display_date=="公元前44年3月15日"],
 ["custom era labels",d.normalize({year:-43},{era_labels:{bce:"Before Common Era"}}).label=="44 Before Common Era"],
 ["explicit CE labels",d.normalize("2026",{era_labels:{ce:"CE"}}).label=="2026 CE"],
 ["astronomical display retains year zero",d.normalize({year:0},{numbering:"astronomical"}).label=="0"],
 ["Gregorian boundaries remain unchanged",d.normalize("2026-10-11",{locale:"ar"}).value==d.normalize("2026-10-11").value],
 ["localized month weekday and zoned pattern",french_clock.label=="dimanche 11 octobre 2026 à 13:14:15.025 +08:00"],
 ["coordinate formatting agrees with source",native.label==french_clock.label],
 ["literal pattern text is preserved",d.normalize("2026-10-11",{format:"[YYYY] YYYY"}).label=="YYYY 2026"],
 ["literal digits remain authored",d.normalize("2026-10-11",{locale:"ar",format:"[2026] YYYY"}).label=="2026 ٢٠٢٦"],
 ["deep calendar formatting does not use native datetime",d.normalize({year:-10000,month:1,day:1},{format:"YYYY-MM-DD"}).label=="-10000-01-01"],
 ["formatter gets locale and declared precision",custom.label=="fr-FR:month:2026"],
 ["explicit date label overrides formatter",explicit.label=="Authored"],
 ["formatter value errors propagate",d.normalize("2026",{formatter:invalid_formatter}) is error],
 ["axis formatter errors propagate",t.layout({view:"axis",axes:[{formatter:invalid_formatter}],events:[{at:"2026"}]},null) is error],
 ["numeric scale labels retain exact coordinates",d.normalize(100000000000000000001.25m,numbers).label=="100000000000000000001,25"],
 ["scale locale overrides inherited locale",custom_scale.events[0].display_date=="11/10/2026"],
 ["axis uses localized month names",axis.axes[0].labels[0].label=="janv. 2026"],
 ["source roundtrip retains locale",schema.validate(schema.source_record(french)).events[0].display_date=="11/10/2026"],
 ["data export headings use chart locale",starts_with(t.export_data({locale:"fr",events:[{id:"a",title:"Alpha"}]},"csv",{fields:["id","title"]}),"\"Identifiant\",\"Titre\"")],
 ["authored export headings override locale",starts_with(t.export_data({locale:"fr",events:[{id:"a"}]},"csv",{fields:["id"],headings:{id:"Key"}}),"\"Key\"")],
 ["invalid locale diagnosed",schema.validate({locale:12}) is error],
 ["invalid scale formatter diagnosed",schema.validate({scales:[{format:12}]}) is error],
 ["invalid era labels diagnosed",schema.validate({scales:[{era_labels:{bce:12}}]}) is error],
 ["unknown era label diagnosed",schema.validate({scales:[{era_labels:{other:"x"}}]}) is error],
 ["invalid axis pattern diagnosed",schema.validate({axes:[{format:12}]}) is error]
]
