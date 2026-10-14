// one coefficient table feeds previews, thumbnails and full-resolution exports.
pub let catalog = [
    {id:"original",label:"Original",matrix:[1,0,0,0,0,1,0,0,0,0,1,0]},
    {id:"mono",label:"Mono",matrix:[0.299,0.587,0.114,0,0.299,0.587,0.114,0,0.299,0.587,0.114,0]},
    {id:"warm",label:"Warm",matrix:[1.08,0,0,0.025,0,1.01,0,0,0,0,0.9,0]},
    {id:"cool",label:"Cool",matrix:[0.92,0,0,0,0,1.015,0,0,0,0,1.1,0.015]},
    {id:"vintage",label:"Vintage",matrix:[0.82,0.16,0.06,0.04,0.08,0.83,0.05,0.025,0.05,0.12,0.63,0.06]},
    {id:"vivid",label:"Vivid",matrix:[1.2103,-0.1761,-0.0342,0,-0.0897,1.1239,-0.0342,0,-0.0897,-0.1761,1.2658,0]}
]
pub fn matrix(id) => [for (item in catalog where item.id==id) item.matrix][0]
