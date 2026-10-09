import maps: lambda.map
let style = {version:8,center:[0,0],zoom:2,sources:{places:{type:"geojson",data:{type:"Point",coordinates:[0,0]}}},
    layers:[{id:"dots",type:"circle",source:"places",paint:{'circle-color':"#123456",'circle-radius':6}}]};
let model = maps.from_style(style,{width:640,height:360});
[
    name(model), model.zoom, model.width, len(content(model)), maps.validate(model),
    maps.from_style({*:style,version:7}) is error,
    maps.from_style({*:style,sprite:"sprites"}) is error,
    maps.from_style({*:style,sources:{places:{type:"vector",url:"tiles.json"}}}) is error,
    maps.from_style({*:style,layers:[{id:"bad",type:"symbol",source:"places"}]}) is error,
    maps.from_style({*:style,transition:{duration:300}}) is error,
    maps.from_style({*:style,sources:{places:{*:style.sources.places,cluster:true}}}) is error
]
