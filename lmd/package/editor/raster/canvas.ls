// S9.2.2: canvas/tool geometry derives plain data without publishing authored edits.
pub let crop_presets = [
    {id:"free",label:"Free"},{id:"original",label:"Original"},
    {id:"1:1",label:"1:1",ratio:1},{id:"4:3",label:"4:3",ratio:4/3},
    {id:"3:2",label:"3:2",ratio:3/2},{id:"16:9",label:"16:9",ratio:16/9},
    {id:"3:4",label:"3:4",ratio:3/4},{id:"2:3",label:"2:3",ratio:2/3},
    {id:"9:16",label:"9:16",ratio:9/16}]
pub let anchors = [
    {id:"top-left",label:"Top left",x:0,y:0},{id:"top",label:"Top",x:0.5,y:0},{id:"top-right",label:"Top right",x:1,y:0},
    {id:"left",label:"Left",x:0,y:0.5},{id:"center",label:"Center",x:0.5,y:0.5},{id:"right",label:"Right",x:1,y:0.5},
    {id:"bottom-left",label:"Bottom left",x:0,y:1},{id:"bottom",label:"Bottom",x:0.5,y:1},{id:"bottom-right",label:"Bottom right",x:1,y:1}]
pub fn crop_ratio(image,preset) any^ {
    let entry = [for (p in crop_presets where p.id == preset) p][0];
    if (entry == null) raise error("Raster: unknown crop aspect preset")
    else if (preset == "original") image.width/image.height else entry.ratio
}
pub fn offset(before,after,anchor="top-left") any^ {
    let entry = [for (a in anchors where a.id == anchor) a][0];
    if (entry == null) raise error("Raster: unknown canvas anchor")
    // floor keeps the extra center pixel on the right/bottom when extending the canvas.
    else [int(floor((after.width-before.width)*entry.x)),int(floor((after.height-before.height)*entry.y))]
}
