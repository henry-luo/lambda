// Ribbon chrome is scoped to its own root and keeps caller override specificity.
import scope: lambda.ui.workspace.scope
pub let css = scope.isolate("/* Shared Slides/Sheet ribbon geometry and controls. */
.editor-ribbon__workspace_document__ { display:flex; align-items:stretch; gap:10px; width:100%; min-width:0; height:80px; }
.editor-ribbon .ribbon-group__workspace_document__ { padding-right:10px; margin:0; gap:0; height:78px; flex-shrink:0; }
.editor-ribbon .group-tools__workspace_document__ { display:flex; flex-direction:column; justify-content:center; gap:3px; flex:1; min-height:0; }
.editor-ribbon .ribbon-group__workspace_document__ { position:relative; }
.editor-ribbon .ribbon-group:last-child__workspace_document__ { padding-right:0; border-right:0; }
.editor-ribbon .ribbon-group-trigger__workspace_document__ { display:none; width:58px; height:70px; padding:4px 2px; flex-direction:column; align-items:center; justify-content:center; gap:3px; overflow:hidden; background:transparent; border-color:transparent; }
.editor-ribbon .ribbon-group-trigger .tool-label__workspace_document__ { max-width:54px; overflow:hidden; text-overflow:ellipsis; }
.editor-ribbon .ribbon-group-trigger .group-chevron__workspace_document__ { line-height:0; }
.editor-ribbon .ribbon-group[data-compact=true]>.ribbon-group-trigger__workspace_document__ { display:flex; }
.editor-ribbon .ribbon-group[data-compact=true]>.group-label__workspace_document__ { display:none; }
/* A closed group must lose popup geometry before live resize exposes its tools in flow. */
.editor-ribbon .ribbon-group[data-compact=true]>.group-tools__workspace_document__ { display:none; }
.editor-ribbon .ribbon-group[data-compact=true][data-open=true]>.group-tools__workspace_document__ { display:flex; position:absolute; top:100%; left:0; z-index:20; width:max-content; min-width:170px; max-width:calc(100vw - 32px); min-height:0; height:auto; padding:10px; border:1px solid #d4dae3; border-radius:7px; background:#fff; box-shadow:0 10px 26px #17274330; }
.editor-ribbon .ribbon-group[data-compact=true][data-open=true][data-align=right]>.group-tools__workspace_document__ { left:auto; right:0; }
.editor-ribbon .ribbon-group[data-compact=true][data-open=true]__workspace_document__ { z-index:21; }
.editor-ribbon .ribbon-group[data-compact=true] .home-row__workspace_document__ { flex-wrap:wrap; }
:where(.editor-ribbon) .home-row__workspace_document__ { display:flex; align-items:center; gap:2px; }
:where(.editor-ribbon) .home-stack__workspace_document__ { display:flex; flex-direction:column; gap:1px; align-items:stretch; }
:where(.editor-ribbon) .history-tools__workspace_document__ { display:flex; gap:2px; }
.editor-ribbon .group-undo .group-tools__workspace_document__ { align-items:flex-start; gap:4px; }
.editor-ribbon button.home-tool__workspace_document__ { display:flex; align-items:center; justify-content:flex-start; gap:5px; padding:3px 5px; border:1px solid transparent; border-radius:3px; background:transparent; height:25px; font-size:11px; color:#4b5563; }
.editor-ribbon button.home-tool:hover__workspace_document__ { background:#f5e9e2; border-color:#e5c2b7; }
.editor-ribbon button.home-tool[aria-pressed=true]__workspace_document__,.editor-ribbon button.home-tool[aria-expanded=true]__workspace_document__ { background:#f4dfd5; border-color:#d7a08b; color:#a54027; }
.editor-ribbon .tool-icon__workspace_document__ { display:flex; align-items:center; justify-content:center; flex-shrink:0; }
.editor-ribbon .home-tool.icon-only__workspace_document__ { justify-content:center; min-width:25px; padding:3px; }
.editor-ribbon .icon-only>.tool-label__workspace_document__ { display:none; }
/* Share an icon row and two-line label slot so menu arrows stay beside their icons. */
.editor-ribbon .home-tool.home-large__workspace_document__ { flex-direction:column; justify-content:flex-start; gap:3px; height:62px; min-width:50px; padding:3px 4px; }
:where(.editor-ribbon) .home-large .tool-icon__workspace_document__ { height:28px; gap:2px; }
:where(.editor-ribbon) .home-large .tool-label__workspace_document__ { min-height:24px; line-height:12px; text-align:center; }
:where(.editor-ribbon) .home-large .ribbon-label-line__workspace_document__ { display:block; }
.editor-ribbon .home-chevron__workspace_document__ { display:flex; align-items:center; justify-content:center; flex-shrink:0; width:10px; height:10px; color:#9199a3; }
.editor-ribbon .home-menu-trigger__workspace_document__ { position:relative; }
:where(.editor-ribbon) .paste-tool__workspace_document__ { display:flex; align-items:stretch; }:where(.editor-ribbon) .paste-tool>.home-tool.home-large__workspace_document__ { min-width:38px; }
:where(.editor-ribbon) .paste-tool>.home-tool.icon-only__workspace_document__ { min-width:13px; padding:0; align-self:stretch; height:auto; }:where(.editor-ribbon) .paste-tool .icon-only .tool-icon__workspace_document__ { display:none; }
.editor-ribbon select__workspace_document__,.editor-ribbon input__workspace_document__ { height:25px; padding:2px 5px; border-radius:2px; font-size:11px; }
")
