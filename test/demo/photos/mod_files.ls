// S12.1.1v3: file acquisition/publication is explicit; decode/edit stays in lambda.image.
import img: lambda.image
import editor: lambda.editor.raster
import project: lambda.image.project
import actions: lambda.editor.raster.actions

pub pn open_document(path) any^ { return editor.open_session(if (ends_with(lower(path),".photos")) project.read(path)^ else img.read(path)^)^ }
pub pn place(st,path) any^ {
    let image = img.read(path)^
    if (len(img.frames(image)) != 0) raise error("Photos: choose a still image to place")
    let raster = img.render_frame(image)^;
    return editor.exec(st,{id:"layer.insert",raster:raster,name:split(path,"/")[last],position:[0,0]})^
}
pub pn write_document(st,path,format) any^ {
    if (not (path is string) or path == "" or
        not ends_with(lower(path),"." ++ format)) raise error("Photos: choose a ." ++ format ++ " destination")
    if (format == "photos") {
        project.write(st.doc,path)^
        return {snapshot:st.doc,report:{preserved:["pixels","layers","masks","effects","transforms","locks"],omitted:[]}}
    }
    if (format == "psd" and any([for (row in editor.layers(st.doc)) row.node.locked == true]))
        raise error("Photos: unlock layers before saving this PSD profile; lock persistence is not supported yet")
    let snapshot = st.doc
    let encoded = img.encode_report(snapshot,{format:symbol(format)})^
    // output's atomic publication protects an existing file when acquisition or writing fails.
    output(encoded.bytes,path,{atomic:true})^
    return {snapshot:snapshot,report:encoded.report}
}

pub pn read_action(path) any^ {
    let text = input(path,'text')^
    if (len(text)>8*1024*1024) raise error("Photos: action file exceeds 8 MiB")
    return actions.validate(parse(text,'mark')^ )^
}
pub pn write_action(action,path) bool^ {
    let checked = actions.validate(action)^
    let text = format(checked,'mark')
    if (not ends_with(lower(path),".laction") or len(text)>8*1024*1024) raise error("Photos: choose a .laction destination within 8 MiB")
    output(text,path,{atomic:true})^
    return true
}
