// The importer owns Office interpretation; viewing reuses the retained Slides player.
import pptx: lambda.doc.pptx
import slide: lambda.slide
import live: lambda.slide.live

pub fn warning_groups(diagnostics) => [for (key in unique([for (issue in diagnostics) [issue.code,issue.message]]),
    let occurrences = [for (issue in diagnostics where [issue.code,issue.message]==key) issue])
    {*:occurrences[0],count:len(occurrences)}]
pub fn warnings(diagnostics) element {
    let groups = warning_groups(diagnostics);
    // Shared masters can repeat an issue on every slide; retain occurrences without repeating rows.
    <details class:"pptx-import-warnings",<summary "Presentation import: " ++ string(len(diagnostics)) ++
        " warning" ++ (if (len(diagnostics)==1) "" else "s") ++ " (" ++ string(len(groups)) ++
        " type" ++ (if (len(groups)==1) "" else "s") ++ ")">
        <ul *[for (issue in groups) <li issue.message ++ (if (issue.count>1) " (" ++ string(issue.count) ++ " occurrences)" else "")>]>>
}
// The expanded list overlays the canvas, keeping the summary and player controls in place.
pub let css = ".pptx-import-warnings{box-sizing:border-box;position:relative;z-index:10;background:#fff4ce;color:#624a00;padding:8px 20px;font:14px/1.4 Arial,sans-serif;}.pptx-import-warnings summary{cursor:pointer;font-weight:600;}.pptx-import-warnings ul{box-sizing:border-box;position:absolute;top:100%;left:0;right:0;max-height:25vh;overflow:auto;margin:0;padding:8px 20px 8px 40px;background:#fff4ce;box-shadow:0 3px 6px #0002;}"
pub fn preview(result) element {
    <div class:"pptx-preview-content",
        if (result.error is string) <p class:"preview-error","Unable to open PowerPoint presentation: " ++ result.error>
        else {
            let width = min(720.0,result.document.width);
            if (len(result.diagnostics)>0) warnings(result.diagnostics) else null;
            for (page in slide.slides(result.document,{width:width,height:width*result.document.height/result.document.width})^)
                <article style:"margin-bottom:20px;",page>
        }>
}
pub fn render_document_file(source,options=null) element^ {
    let result = pptx.convert(source,options)^
    let plan = slide.compile(result.document)^;
    live.document_tree(plan,{instance:"pptx"},if (len(result.diagnostics)>0) warnings(result.diagnostics) else null,css)
}
