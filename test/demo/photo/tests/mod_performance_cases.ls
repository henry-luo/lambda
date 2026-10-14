import model: ~~.mod_model
pub let pixels = as_ubyte([for (y in 0 to 4) [for (x in 0 to 7)
    [(x*37+y*11)%256,(x*19+y*43)%256,(x*53+y*7)%256,if ((x+y)%4==0) 0 else if ((x+y)%3==0) 127 else 255]]])
let base=model.recipe()
pub let recipes=[base,
    *[for (preset in ["mono","warm","cool","vintage","vivid"]) {*:base,preset:preset}],
    {*:base,exposure:0.4,contrast:1.12,saturation:1.2,gamma:1.15,preset:"warm"},
    {*:base,black:0.2,white:0.8,shadows:0.6,highlights:-0.4,temperature:0.4,brightness:-0.1},
    {*:base,vignette:0.8}, {*:base,softness:6.0}, {*:base,sharpen:1.2},
    {*:base,softness:2.0,sharpen:0.7,vignette:0.4,preset:"vintage",gamma:0.8},
    *[for (turns in 1 to 3) {*:base,turns:turns,flip_x:true,flip_y:true}],
    {*:base,angle:7.0,crop:{left:0.125,top:0.2,right:0.875,bottom:1}},
    {*:base,angle:-3.0,turns:1,flip_x:true,softness:1.0,preset:"cool"},
    {*:base,output_width:13,output_height:9}, {*:base,output_width:3,output_height:2},
    {*:base,output_width:7,output_height:1,angle:4.0,softness:3.0,gamma:1.2}]
