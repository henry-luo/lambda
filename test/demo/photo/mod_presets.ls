pub let catalog = [
    {id:"original",label:"Original"}, {id:"mono",label:"Mono"},
    {id:"warm",label:"Warm"}, {id:"cool",label:"Cool"},
    {id:"vintage",label:"Vintage"}, {id:"vivid",label:"Vivid"}
]
pub fn channels(red, green, blue, id) {
    let luma = red*0.299+green*0.587+blue*0.114
    if (id == "mono") {r:luma,g:luma,b:luma}
    else if (id == "warm") {r:red*1.08+0.025,g:green*1.01,b:blue*0.9}
    else if (id == "cool") {r:red*0.92,g:green*1.015,b:blue*1.1+0.015}
    else if (id == "vintage") {r:red*0.82+green*0.16+blue*0.06+0.04,
        g:red*0.08+green*0.83+blue*0.05+0.025,b:red*0.05+green*0.12+blue*0.63+0.06}
    else if (id == "vivid") {r:(red-luma)*1.3+luma,g:(green-luma)*1.3+luma,b:(blue-luma)*1.3+luma}
    else {r:red,g:green,b:blue}
}
