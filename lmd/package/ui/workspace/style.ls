// scope desktop chrome so a document's own styles remain independent.
import scope: lambda.ui.workspace.scope
pub let css = scope.isolate("
.workspace-root { box-sizing:border-box; height:100vh; display:flex; flex-direction:column; color:var(--lambda-app-text); background:var(--lambda-app-background); font-family:var(--lambda-app-font-family); font-size:var(--lambda-app-font-size); line-height:var(--lambda-app-line-height); }
.workspace-root .workspace-toolbar__workspace_document__,.workspace-root .workspace-panes__workspace_document__,.workspace-root .workspace-sidebar__workspace_document__,.workspace-root .workspace-content__workspace_document__,.workspace-root .workspace-status__workspace_document__,.workspace-root .workspace-button__workspace_document__,.workspace-root .workspace-input__workspace_document__,.workspace-root .workspace-text-area__workspace_document__,.workspace-root .workspace-select__workspace_document__ { box-sizing:border-box; }
.workspace-root .workspace-toolbar__workspace_document__ { display:flex; align-items:center; gap:var(--lambda-app-spacing); padding:4px 8px; background:var(--lambda-app-surface); border-bottom:1px solid var(--lambda-app-border); }
.workspace-root .workspace-panes__workspace_document__ { display:flex; flex:1; min-height:0; min-width:0; }
.workspace-root .workspace-sidebar__workspace_document__ { overflow:auto; background:var(--lambda-app-background); border-right:1px solid var(--lambda-app-border); }
.workspace-root .workspace-content__workspace_document__ { flex:1; min-width:0; min-height:0; overflow:auto; background:var(--lambda-app-surface); }
.workspace-root .workspace-status__workspace_document__ { padding:3px 8px; background:var(--lambda-app-surface); border-top:1px solid var(--lambda-app-border); color:var(--lambda-app-text-secondary); }
.workspace-root .workspace-button__workspace_document__,.workspace-root .workspace-input__workspace_document__,.workspace-root .workspace-text-area__workspace_document__,.workspace-root .workspace-select__workspace_document__ { font:inherit; color:inherit; border:1px solid var(--lambda-app-border); border-radius:var(--lambda-app-radius); background:var(--lambda-app-surface); }
.workspace-root .workspace-button__workspace_document__,.workspace-root .workspace-input__workspace_document__,.workspace-root .workspace-select__workspace_document__ { min-height:var(--lambda-app-control-height); padding:3px 8px; }
.workspace-root .workspace-button__workspace_document__ { cursor:pointer; }
.workspace-root .workspace-button:hover__workspace_document__ { background:var(--lambda-app-background); }
.workspace-root .workspace-button.workspace-variant-primary__workspace_document__ { background:var(--lambda-app-primary); border-color:var(--lambda-app-primary); color:white; }
.workspace-root .workspace-button.workspace-variant-subtle__workspace_document__ { border-color:transparent; background:transparent; }
.workspace-root .workspace-button.workspace-variant-danger__workspace_document__ { color:#b42318; }
.workspace-root .workspace-button:focus-visible__workspace_document__,.workspace-root .workspace-input:focus-visible__workspace_document__,.workspace-root .workspace-text-area:focus-visible__workspace_document__,.workspace-root .workspace-select:focus-visible__workspace_document__ { outline:2px solid var(--lambda-app-primary); outline-offset:1px; }
.workspace-root .workspace-button:disabled__workspace_document__,.workspace-root .workspace-input:disabled__workspace_document__,.workspace-root .workspace-text-area:disabled__workspace_document__,.workspace-root .workspace-select:disabled__workspace_document__ { color:var(--lambda-app-disabled-text); background:var(--lambda-app-disabled-background); cursor:default; }
")
