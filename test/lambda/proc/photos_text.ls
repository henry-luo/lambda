import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import project: lambda.image.project
pn main() {
    let st = editor.open_session(editor.new_document({width:160,height:80})^)^
    let bytes = input("lmd/package/doc/latex/fonts/Sans/cmunss.woff2",'binary')^
    let embedded = editor.exec(st,{id:"font.embed",font:"sans",bytes:bytes,family:"Computer Modern Sans"})^
    let text = editor.exec(embedded,{id:"text.insert",options:{width:160,height:80,font:"sans",size:20,text:"Hello\nPhotos",fill:[1,0,0,1]}})^
    let pixels = img.render_frame(text.doc)^
    let changed = editor.exec(text,{id:"text.update",options:{text:"Lambda",size:30,align:"center"}})^
    let restored = project.decode(project.encode(changed.doc)^)^
    let baked = editor.exec(changed,{id:"layer.rasterize"})^
    let cleared = editor.exec(changed,{id:"text.update",options:{text:""}})^
    let empty_saved = project.decode(project.encode(cleared.doc)^)^
    let spaced = editor.exec(changed,{id:"text.update",options:{text:" "}})^
    let words = editor.exec(changed,{id:"text.update",options:{text:"Hi Photos"}})^
    let missing = editor.exec(embedded,{id:"text.insert",options:{width:100,height:40,font:"absent",text:"Hello"}}) ^ { true }
    let glyph = editor.exec(embedded,{id:"text.insert",options:{width:100,height:40,font:"sans",text:"א"}}) ^ { true }
    return [sum(pixels.a)>0,sum(pixels.b) == 0,model.primary(model.active(changed)^.node)[0] == "Lambda",
        img.render_frame(changed.doc)^ != pixels,img.render_frame(restored)^ == img.render_frame(changed.doc)^,
        img.assets(restored)[0].data == bytes,img.render_frame(baked.doc)^ == img.render_frame(changed.doc)^,
        editor.exec(changed,{id:"history.undo"})^.doc == text.doc,missing is bool,glyph is bool,
        img.coverage_bounds(img.from_array(pixels.a)^)^[3]>24,
        len(content(model.primary(model.active(cleared)^.node))) == 0 and sum(img.render_frame(cleared.doc)^.a) == 0,
        img.render_frame(empty_saved)^ == img.render_frame(cleared.doc)^,
        editor.exec(cleared,{id:"history.undo"})^.doc == changed.doc,sum(img.render_frame(spaced.doc)^.a) == 0,
        sum(img.render_frame(words.doc)^.a)>0]
}
