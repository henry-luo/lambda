// D7.2.5 / S12.1.3: Word and Slides share atomic PDF publication through Radiant.
import radiant

pub pn pdf(source,width,height,source_format,path,label="Document") any^ {
    let pending = path ++ "." ++ lower(label) ++ "-" ++ string(int(clock()*1000000)) ++ ".pdf"
    let result = radiant.export_document(source,pending,{format:"pdf",width:width,height:height,pagination:true,source_format:source_format})
    if (result != true) {
        io.delete(pending) ^ { null }
        raise error(label ++ ": " ++ string(result))
    }
    io.rename(pending,path) ^ { io.delete(pending) ^ { null }; raise ^ }
    return true
}
