import img: lambda.image
import editor: lambda.editor.raster
import batch: lambda.editor.raster.batch
import files: lambda.editor.raster.files
import io
pn main() {
    io.mkdir("temp/photos") ^ { null }
    let st = editor.open_session(img.of_raster(img.blank({width:4,height:4,color:[1,0,0,1]})^))^
    files.write_document(st,"temp/photos/batch-source.photos","photos")^
    let action = {version:1,target:"active-layer",name:"Blue",steps:[{id:"pixels.fill",color:[0,0,1,1]}]}
    let report = batch.run(action,[{source:"temp/photos/batch-source.photos",target:"temp/photos/batch-a.photos",format:"photos"},
        {source:"temp/photos/no-batch-source.photos",target:"temp/photos/batch-b.png",format:"png"},
        {source:"temp/photos/batch-source.photos",target:"temp/photos/batch-c.png",format:"png"}])^
    let opened = files.open_document("temp/photos/batch-a.photos")^
    let png = img.read("temp/photos/batch-c.png")^
    let stopped = batch.run(action,[{source:"temp/photos/no-batch-source.photos",target:"temp/photos/batch-fail.png",format:"png"},
        {source:"temp/photos/batch-source.photos",target:"temp/photos/batch-never.png",format:"png"}],{continue_on_error:false})^
    let collision = batch.preflight(action,[{source:"temp/photos/batch-source.photos",target:"./temp/photos/batch-source.photos",format:"photos"}],"file:///base/") ^ { true }
    let duplicates = batch.preflight(action,[{source:"temp/photos/batch-source.photos",target:"temp/photos/batch-same.png",format:"png"},
        {source:"temp/photos/batch-source.photos",target:"./temp/photos/batch-same.png",format:"png"}],"file:///base/") ^ { true }
    return [report.completed == 3 and report.succeeded == 2 and report.failed == 1,report.results[1].error is string,
        img.sample(img.render_frame(opened.doc)^,0,0)^ == [0,0,255,255],img.render_frame(png)^ == img.render_frame(opened.doc)^,
        img.sample(img.render_frame(files.open_document("temp/photos/batch-source.photos")^.doc)^,0,0)^ == [255,0,0,255],
        stopped.completed == 1 and stopped.failed == 1,collision is bool,duplicates is bool]
}
