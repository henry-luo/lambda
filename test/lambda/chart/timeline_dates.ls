import d: lambda.chart.timeline.date
let epoch = d.normalize("1970-01-01")
let zero = d.normalize({year:0})
let fold = {year:2024,month:11,day:3,hour:1,minute:30}
let gap = {year:2024,month:3,day:10,hour:2,minute:30}
let zone = {timezone:"America/New_York"}
let early = d.normalize(fold,{*:zone,ambiguous_time:"earlier"})
let late = d.normalize(fold,{*:zone,ambiguous_time:"later"});
[
 ["epoch",epoch.value == 0 and epoch.precision == "day"],
 ["native datetime",d.normalize(t'2026-10-11').value == t'2026-10-11T00:00:00z'.unix],
 ["partial precision",d.normalize("2026").precision == "year" and d.normalize("2026-10").precision == "month"],
 ["astronomical year zero",zero.label == "1 BCE" and zero.value < d.normalize({year:1}).value],
 ["human BCE adapter",d.human_year(-44) == -43 and d.human_year(2026) == 2026],
 ["BCE label",d.normalize({year:-43,month:3,day:15}).label == "44 BCE 03-15"],
 ["leap validity",not (d.normalize("2000-02-29") is error) and d.normalize("1900-02-29") is error],
 ["calendar roundtrip",all([for (year in [-10000,-400,-1,0,1,400,1900,1970,2000,10000])
    d.civil(d.days(year,3,1)) == {year:year,month:3,day:1}])],
 ["fixed offset",d.normalize("2026-10-11T12:13:14.25+08:00").value == t'2026-10-11T04:13:14.250z'.unix],
 ["invalid clock",d.normalize("2026-10-11T25:00:00Z") is error],
 ["unknown zone",d.normalize({year:2026,timezone:"Missing/Zone"}) is error],
 ["fold rejected",d.normalize(fold,zone) is error],
 ["fold policies",late.value - early.value == 3600000],
 ["gap rejected",d.normalize(gap,zone) is error],
 ["gap policies",d.normalize(gap,{*:zone,nonexistent_time:"forward"}).value -
    d.normalize(gap,{*:zone,nonexistent_time:"backward"}).value == 3600000],
 ["numeric unit required",d.normalize(10,{kind:"numeric"}) is error],
 ["concise exact numeric labels",d.normalize(40.0000m,{kind:"numeric",unit:"year"}).label=="40" and
    d.normalize(100000000000000000001.2500m,{kind:"cosmological",unit:"year"}).label=="100000000000000000001.25" and
    d.normalize(1.25e20,{kind:"numeric",unit:"year"}).label=="1.25e+20"],
 ["deep exact ordering",d.normalize(100000000000000000001m,{kind:"cosmological",unit:"year"}).value >
    d.normalize(100000000000000000000m,{kind:"cosmological",unit:"year"}).value],
 ["subtract before projection",d.fraction(100000000000000000001m,100000000000000000000m,100000000000000000002m) == 0.5],
 ["time only rejected",d.normalize(t'12:30:00') is error],
 ["invalid ISO rejected",d.normalize("October 11, 2026") is error],
 ["unknown date field",d.normalize({year:2026,unexpected:true}) is error]
]
