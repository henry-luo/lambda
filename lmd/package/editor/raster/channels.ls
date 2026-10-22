// S9.2.2: component views are snapshots; saved coverage is authored project metadata.
import img: lambda.image
import model: .model
import metadata: .metadata
pub fn container(image) => metadata.container(image,'photos_channels')
pub fn items(image) => model.saved_channels(image)
pub fn lookup(image,id) => [for (channel in items(image) where channel.id == id) channel][0]
fn replace(image,entries) any^ => metadata.replace(image,'photos_channels',entries)^
fn fresh(image,index=1) => if ("channel-" ++ string(index) in model.identifiers(image)) fresh(image,index+1)
    else "channel-" ++ string(index)
pub fn component(raster,channel) any^ => img.from_array(img.channel(raster,channel)^)^
pub fn resized(image,width,height,scale=false,origin=[0,0]) any^ {
    let entries = items(image);
    if (len(entries)==0) image else replace(image,[for (channel in entries) img.with_children(channel,
        [if (scale) img.resize(channel[0],width,height)^ else img.reframe(channel[0],[origin[0],origin[1],origin[0]+width,origin[1]+height])^])^])^
}
pub fn preview(st,raster) any^ {
    let current = model.value(st.channel_view,"composite");
    if (current == "composite") raster else {
        let coverage = if (current in ["r","g","b","a"]) component(raster,current)^ else lookup(st.doc,current)[0]
        if (coverage == null) raise error("Raster: missing preview channel")
        else img.render_frame(img.of_raster(coverage))^
    }
}
pub fn run_command(st,command) any^ {
    let id = command.id
    let channel = lookup(st.doc,command.channel)
    if (id == "channel.view") (
        if (not (command.channel in ["composite","r","g","b","a"]) and channel == null) raise error("Raster: missing preview channel")
        else {*:st,channel_view:command.channel})
    else if (id == "channel.save" or id == "channel.capture") {
        let label = model.value(command.name,"Saved selection")
        let coverage = if (id == "channel.save") st.selection else component(img.render_frame(st.doc)^,command.channel)^;
        if (coverage == null) raise error("Raster: create a selection before saving a channel")
        else if (not (label is string) or len(label)<1 or len(label)>256 or len(items(st.doc))>=64) raise error("Raster: invalid channel name or channel limit reached")
        else {
            let saved = <channel id:fresh(st.doc),name:label,coverage>;
            {*:st,doc:replace(st.doc,[*items(st.doc),saved])^}
        }
    }
    else if (channel == null) raise error("Raster: select a saved channel")
    else if (id == "channel.load") {*:st,selection:channel[0]}
    else if (id == "channel.delete") {*:st,doc:replace(st.doc,[for (entry in items(st.doc) where entry.id != channel.id) entry])^,
        channel_view:if (st.channel_view == channel.id) "composite" else st.channel_view}
    else if (id == "channel.rename") (
        if (not (command.name is string) or len(command.name)<1 or len(command.name)>256) raise error("Raster: invalid channel name")
        else {*:st,doc:replace(st.doc,[for (entry in items(st.doc)) if (entry.id == channel.id)
            img.with_properties(entry,{name:command.name})^ else entry])^})
    else if (id == "channel.replace") {
        let target = model.pixel_target(st)^
        let origin = target.position
        let coverage = img.reframe(channel[0],[origin[0],origin[1],origin[0]+target.raster.width,origin[1]+target.raster.height])^
        let raster = img.with_channel(target.raster,command.component,img.channel(coverage,"gray")^)^;
        model.replace_pixels(st,target,raster)^
    }
    else raise error("Raster: unknown channel command")
}
