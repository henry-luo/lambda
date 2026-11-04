// Scoped office chrome; document-level resets are an explicit standalone option.
import scope: lambda.ui.workspace.scope
pub let document_css = "html,body { margin:0; width:100%; height:100%; overflow:hidden; }\n"
pub let css = scope.isolate("/* Shared Word/Sheet shell; app accents override the theme variables. */
/* Light panels, blue accents, and system typography match photo-album. */
:where(.workspace-office) {
  --bg:var(--lambda-app-background,#f5f5f7); --panel:var(--lambda-app-surface,#ffffff); --sidebar:#eceef2; --ink:var(--lambda-app-text,#1d1d1f); --muted:var(--lambda-app-text-secondary,#6e6e73);
  --line:var(--lambda-app-border,#d9dbe1); --accent:var(--lambda-app-primary,#0a84ff); --accent-soft:#dcebff; --hover:#e9eaee;
  --tooltip-bg:#fafafa;
  --font:var(--lambda-app-font-family,-apple-system, \"Helvetica Neue\", Arial, sans-serif);
}
:where(.workspace-office) *__workspace_document__ { box-sizing:border-box; }
:where(.workspace-office) { font-family:var(--font); color:var(--ink); font-size:13px; background:var(--bg);
  display:flex; flex-direction:column; }
:where(.workspace-office) button__workspace_document__,:where(.workspace-office) input__workspace_document__,:where(.workspace-office) select__workspace_document__ { font-family:var(--font); font-size:12px; }
:where(.workspace-office) button__workspace_document__ { cursor:pointer; }
:where(.workspace-office) button:focus-visible__workspace_document__,:where(.workspace-office) input[type=checkbox]:focus-visible__workspace_document__,:where(.workspace-office) input[type=radio]:focus-visible__workspace_document__ { outline:2px solid var(--accent); outline-offset:2px; }
/* fields reuse their existing border for focus; borderless search inputs highlight the wrapper. */
:where(.workspace-office) input:not([type=checkbox]):not([type=radio]):focus__workspace_document__,:where(.workspace-office) textarea:focus__workspace_document__,:where(.workspace-office) select:focus__workspace_document__,:where(.workspace-office) .font-picker-button:focus__workspace_document__ { outline:none; border-color:var(--accent); }
:where(.workspace-office) .search-field:focus-within__workspace_document__,:where(.workspace-office) .picker-search:focus-within__workspace_document__ { border-color:var(--accent); }
/* compact two-line header: the icon spans the title line and the ribbon tab row (Google Docs style). */
:where(.workspace-office) .titlebar__workspace_document__ { flex-shrink:0; display:flex; align-items:center; padding:6px 16px 0 14px;
  gap:10px; background:var(--panel); color:var(--ink); border-bottom:1px solid var(--line); }
:where(.workspace-office) .brand__workspace_document__ { display:flex; align-items:center; gap:8px; flex-shrink:0; margin-bottom:4px; padding-right:12px; border-right:1px solid var(--line); font-size:18px; font-weight:700; }
:where(.workspace-office) .brand-mark__workspace_document__ { display:flex; align-items:center; justify-content:center; width:32px; height:40px; color:var(--accent); }
:where(.workspace-office) .title-stack__workspace_document__ { flex:1; min-width:0; display:flex; flex-direction:column; }
:where(.workspace-office) .title-line__workspace_document__ { height:24px; display:flex; align-items:center; gap:10px; min-width:0; padding-left:9px; }
:where(.workspace-office) .document-title__workspace_document__ { min-width:0; font-size:16px; font-weight:400; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
:where(.workspace-office) .save-indicator__workspace_document__ { flex-shrink:0; color:var(--muted); font-size:11px; }
:where(.workspace-office) .title-actions__workspace_document__ { display:flex; gap:8px; flex-shrink:0; }
:where(.workspace-office) .title-actions .tool__workspace_document__ { color:var(--ink); border-color:transparent; background:var(--hover); height:30px; padding:0 12px; }
:where(.workspace-office) .title-actions #export__workspace_document__ { background:var(--accent); color:#fff; border-color:var(--accent); }
:where(.workspace-office) .title-actions #export:hover__workspace_document__ { background:#0071e3; }
:where(.workspace-office) .ribbon-shell__workspace_document__ { position:relative; z-index:10; background:var(--panel); flex-shrink:0; border-bottom:1px solid var(--line); }
:where(.workspace-office) .tabs__workspace_document__ { height:30px; display:flex; align-items:flex-end; gap:2px; overflow-x:auto; }
:where(.workspace-office) .tabs>.tool__workspace_document__ { flex-shrink:0; height:28px; font-size:13px; }
:where(.workspace-office) .tool__workspace_document__ { border:1px solid transparent; border-radius:7px; background:transparent; color:var(--ink);
  height:28px; padding:3px 8px; display:inline-flex; gap:6px; align-items:center; justify-content:center; white-space:nowrap; font-size:11px; }
:where(.workspace-office) .tool:hover__workspace_document__ { background:var(--hover); }
:where(.workspace-office) .tool[aria-pressed=true]__workspace_document__ { background:var(--accent-soft); border-color:#b7d7ff; color:var(--accent); }
:where(.workspace-office) .tool:disabled__workspace_document__ { opacity:0.35; cursor:default; }
:where(.workspace-office) .tool-icon__workspace_document__ { display:inline-flex; flex-shrink:0; align-items:center; justify-content:center; line-height:0; }
:where(.workspace-office) .app-icon__workspace_document__ { display:block; flex-shrink:0; pointer-events:none; }
:where(.workspace-office) .icon-only .tool-label__workspace_document__ { display:none; }
:where(.workspace-office) .icon-only__workspace_document__ { width:28px; padding:3px; }
:where(.workspace-office) .tool-label__workspace_document__ { font-size:inherit; line-height:1.2; }
:where(.workspace-office) .tab__workspace_document__ { min-width:0; padding:3px 10px; border-radius:4px 4px 0 0; }
:where(.workspace-office) .tab[aria-pressed=true]__workspace_document__ { border-color:transparent; border-bottom:2px solid var(--accent); color:#2564ae; background:#f0f6ff; font-weight:600; }
:where(.workspace-office) .tab-divider__workspace_document__ { height:15px; border-left:1px solid var(--line); margin:0 6px 7px; }
:where(.workspace-office) .tab-spacer__workspace_document__ { flex:1; }
:where(.workspace-office) .tabs>#sample__workspace_document__ { color:var(--muted); font-size:11px; }
:where(.workspace-office) .ribbon-groups__workspace_document__ { min-height:80px; display:flex; padding:5px 18px 3px; gap:15px; }
:where(.workspace-office) .ribbon-group__workspace_document__ { position:relative; border-right:1px solid #e0e5ed; padding-right:15px; display:flex; flex-direction:column; min-width:0; flex-shrink:0; }
:where(.workspace-office) .ribbon-group:last-child__workspace_document__ { border-right:0; padding-right:0; }
:where(.workspace-office) .ribbon-group-trigger__workspace_document__ { display:none; width:70px; min-height:70px; padding:4px 2px; flex-direction:column; gap:3px; }
:where(.workspace-office) .ribbon-group-trigger .tool-label__workspace_document__ { max-width:66px; overflow:hidden; text-overflow:ellipsis; }
:where(.workspace-office) .ribbon-group-trigger .group-chevron__workspace_document__ { line-height:0; }
/* scope flex to contents; an outer ribbon group can itself be named Tools. */
:where(.workspace-office) .ribbon-group>.group-tools__workspace_document__ { flex:1; min-height:72px; }
:where(.workspace-office) .group-label__workspace_document__ { display:none; color:#768194; font-size:9px; line-height:12px; text-align:center; margin-top:5px; letter-spacing:0.65px; text-transform:uppercase; }
:where(.workspace-office)#word[data-ribbon-group-labels=true] .ribbon-groups__workspace_document__ { min-height:97px; }
:where(.workspace-office)#word[data-ribbon-group-labels=true] .group-label__workspace_document__ { display:block; }
:where(.workspace-office) .ribbon-shell .tool__workspace_document__ { border-radius:5px; }
:where(.workspace-office) .ribbon-groups .tool:hover__workspace_document__ { background:#edf4ff; border-color:#d4e4fa; }
:where(.workspace-office) .ribbon-groups .tool[aria-pressed=true]__workspace_document__ { background:#e4efff; border-color:#c0d8f8; color:#2469b8; }
:where(.workspace-office) .ribbon-groups .tool:disabled__workspace_document__ { opacity:0.46; background:transparent; border-color:transparent; }
:where(.workspace-office) .ribbon-groups .tool:disabled .color-icon-ink path__workspace_document__,:where(.workspace-office) .ribbon-groups .tool:disabled .color-icon-accent__workspace_document__ { stroke:#94a0b2; }
:where(.workspace-office) .ribbon-groups .tool:disabled .color-icon-fill__workspace_document__,:where(.workspace-office) .ribbon-groups .tool:disabled .color-icon-solid__workspace_document__ { fill:#e4e9f0; }
:where(.workspace-office) .ribbon-command-row__workspace_document__ { display:flex; align-items:center; gap:4px; }
:where(.workspace-office) .ribbon-command-stack__workspace_document__ { display:flex; flex-direction:column; justify-content:center; align-items:flex-start; gap:1px; min-height:70px; }
:where(.workspace-office) .ribbon-command-stack>.tool__workspace_document__,:where(.workspace-office) .ribbon-command-stack>.ribbon-command-row>.tool__workspace_document__ { height:23px; justify-content:flex-start; padding:2px 6px; }
:where(.workspace-office) .ribbon-groups .ribbon-large__workspace_document__ { position:relative; flex-shrink:0; height:66px; min-width:58px; width:auto; padding:3px; flex-direction:column; justify-content:flex-start; gap:4px; }
:where(.workspace-office) .ribbon-large .menu-chevron__workspace_document__ { position:absolute; right:2px; top:21px; }
:where(.workspace-office) .statusbar__workspace_document__ { display:flex; height:33px; flex-shrink:0; align-items:center; justify-content:space-between; padding:0 20px;
  background:var(--panel); border-top:1px solid var(--line); color:var(--muted); font-size:10px; }
:where(.workspace-office) .status-left__workspace_document__,:where(.workspace-office) .status-right__workspace_document__ { display:flex; align-items:center; gap:10px; white-space:nowrap; }
:where(.workspace-office) .status-left__workspace_document__ { min-width:0; }:where(.workspace-office) .status-left>span__workspace_document__ { flex-shrink:0; }:where(.workspace-office) #status__workspace_document__ { flex-shrink:1; min-width:0; overflow:hidden; text-overflow:ellipsis; }
:where(.workspace-office) .status-dot__workspace_document__ { width:5px; height:5px; background:var(--accent); border-radius:50%; }
:where(.workspace-office) .status-divider__workspace_document__ { height:11px; border-left:1px solid var(--line); }:where(.workspace-office) .statusbar .tool__workspace_document__ { height:23px; color:var(--muted); font-size:10px; }
:where(.workspace-office) .status-right__workspace_document__ { gap:7px; flex-shrink:0; }:where(.workspace-office) #document-mode__workspace_document__ { margin-left:8px; }
")
