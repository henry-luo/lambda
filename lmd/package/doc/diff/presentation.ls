import c: .common
import rows: .rows

pub let stylesheet = "
.diff-review{font:13px system-ui,sans-serif;color:#23303d;background:#fff;box-sizing:border-box}
.diff-review *{box-sizing:border-box}.diff-review button,.diff-review input{font:inherit;color:inherit}
.diff-review button{border:1px solid #d4dde5;border-radius:5px;background:#fff;padding:6px 9px;cursor:pointer}
.diff-review button:hover{background:#edf3f8}.diff-review button:focus-visible,.diff-review input:focus-visible{outline:2px solid #2167ad}
.diff-review button[aria-pressed=true]{background:#e4effa;border-color:#2167ad}.diff-review button:disabled{opacity:.45;cursor:default}
.diff-toolbar{display:flex;align-items:center;gap:8px;flex-wrap:wrap;padding:12px 16px;border-bottom:1px solid #d4dde5;background:#f7f9fb}
.diff-title{font-size:16px;font-weight:600;flex:1;min-width:150px}.diff-workspace{display:flex;flex:1;min-height:0}
.diff-sidebar{width:280px;flex-shrink:0;display:flex;flex-direction:column;border-right:1px solid #d4dde5;padding:12px;gap:8px}
.diff-review input{border:1px solid #c8d4df;border-radius:5px;padding:7px;min-width:0}.diff-filter{width:100%}
.diff-files{display:flex;flex-direction:column;gap:3px;flex:1;overflow:hidden}.diff-files button{text-align:left;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-height:32px;flex-shrink:0}
.diff-files button[aria-current=true]{background:#e4effa;border-color:#2167ad}.diff-kind{color:#586f83;font-size:11px;margin-right:6px}.diff-path{font-family:monospace}
.diff-group{color:#65778a;font-size:11px;margin:8px 0 2px;text-transform:uppercase}.diff-main{display:flex;flex-direction:column;min-width:0;flex:1;overflow:hidden}
.diff-file-header{padding:10px 16px;border-bottom:1px solid #d4dde5;white-space:pre-wrap;overflow-wrap:anywhere}.diff-file-header h2{font-size:14px;margin:0 0 4px}.diff-meta{font-size:12px;color:#65778a}
.diff-navigation{display:flex;align-items:center;gap:6px;padding:8px 12px;border-bottom:1px solid #e0e6ec;flex-wrap:wrap}.diff-query{flex:1;max-width:300px}
.diff-surface{flex:1;min-height:0;overflow-x:auto;overflow-y:hidden;font:13px/22px monospace;outline:none;position:relative}
.diff-row{display:flex;min-width:100%;width:max-content;min-height:22px;height:22px;white-space:pre;tab-size:4}
.diff-num{width:58px;flex-shrink:0;text-align:right;padding:0 8px;color:#6c7e8e;background:#f3f6f8;user-select:none;border-right:1px solid #e0e6ec}
.diff-sign{width:24px;flex-shrink:0;text-align:center;user-select:none}.diff-text{padding-right:24px;white-space:pre}.diff-add{background:#e7f6e9}.diff-remove{background:#fdebec}
.diff-add .diff-sign{color:#157531}.diff-remove .diff-sign{color:#a62c35}.diff-hunk{background:#eaf1fa;color:#42688c}.diff-summary{padding:16px;color:#65778a;height:auto;white-space:pre-wrap}
.diff-no-newline{color:#8b6637;font-size:11px;margin-left:20px;user-select:none}.diff-row[data-selected=true]{box-shadow:inset 3px 0 #2167ad}
.diff-footer{display:flex;gap:12px;align-items:center;padding:8px 16px;border-top:1px solid #d4dde5;font-size:12px;color:#586f83;min-height:34px}.diff-status{flex:1;white-space:pre-wrap}
.diff-interactive{height:100vh;display:flex;flex-direction:column;overflow:hidden}.diff-static .diff-file{margin:24px 0;break-inside:auto}.diff-static .diff-row{height:auto;min-height:22px}.diff-static .diff-surface{overflow:visible}
@media(max-width:700px){.diff-sidebar{width:190px}.diff-num{width:42px}.diff-toolbar{padding:8px}.diff-title{font-size:14px}}
@media print{.diff-review{font-size:11px}.diff-row{font-size:10px}.diff-toolbar{background:#fff}.diff-file-header{break-after:avoid}}
"

pub fn page(body,title) => <html lang:"en",<head <meta charset:"UTF-8"> <title title>
    <style ("html,body{margin:0;padding:0}" ++ stylesheet)>> <body body>>
pub fn button(command,text,disabled = false) => <button type:"button",["data-diff-command"]:command,
    *:(if (disabled) {disabled:true} else {}),text>
pub fn header(change) => <header class:"diff-file-header",<h2 rows.file_name(change)>
    <div class:"diff-meta",change.kind ++
        (if (change.old_name != null and change.new_name != null and change.old_name != change.new_name)
            " · " ++ change.old_name ++ " → " ++ change.new_name else "") ++
        (if (change.old_mode != null or change.new_mode != null) " · " ++ string(change.old_mode) ++ " → " ++ string(change.new_mode) else "") ++
        (if (change.added == null) "" else " · +" ++ string(change.added) ++ " −" ++ string(change.removed))>>
pub fn row(value,selected = null) element =>
    <div class:"diff-row diff-" ++ value.kind,["data-row"]:string(value.row),
        ["data-selected"]:string(selected != null and value.row >= min(selected.anchor.row,selected.head.row) and
            value.row <= max(selected.anchor.row,selected.head.row)),
        if (value.kind in ["hunk","summary"]) <span class:"diff-text",["data-row"]:string(value.row),value.text>
        else [<span class:"diff-num",["aria-hidden"]:"true",string(c.get(value,"old_line",""))>,
              <span class:"diff-num",["aria-hidden"]:"true",string(c.get(value,"new_line",""))>,
              <span class:"diff-sign",["aria-label"]:value.kind,{add:"+",remove:"−",context:" "}[value.kind]>,
              <span class:"diff-text",["data-row"]:string(value.row),value.text>,
              if (value.old_no_newline or value.new_no_newline) <span class:"diff-no-newline","No newline at end of file"> else null]>

pub fn to_html(document,options = null) element^ {
    let opts = c.options(options,["title"])^
    let valid = c.require(document.kind == "diff" and document.complete == true,"static output requires a complete diff document")^
    let title = c.get(opts,"title","Diff review");
    page(<main class:"diff-review diff-static",<header class:"diff-toolbar",<h1 class:"diff-title",title>
        <span string(len(document.files)) ++ " files · +" ++ string(document.added) ++ " −" ++ string(document.removed)>>
        if (len(document.files) == 0) <p class:"diff-summary","No changes.">
        else *[for (change in document.files) <section class:"diff-file",header(change);
            <div class:"diff-surface",*[for (value in rows.window(document,change,rows.index(change),0,rows.index(change).count)^) row(value)]>>]>,title)
}
