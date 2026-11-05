// Native demo shells share desktop tokens without changing their DOM ownership.
import theme: .tokens
import scope: .scope

pub fn variables(overrides = {}) string^ => theme.variables(overrides)^

// Only explicitly marked chrome gets the common keyboard focus treatment.
pub let css = scope.isolate("
.workspace-shell { font-family:var(--lambda-app-font-family); font-size:var(--lambda-app-font-size); color:var(--lambda-app-text); background:var(--lambda-app-background); }
.workspace-shell .workspace-chrome button:focus-visible__workspace_document__,
.workspace-shell .workspace-chrome input:focus-visible__workspace_document__,
.workspace-shell .workspace-chrome select:focus-visible__workspace_document__,
.workspace-shell .workspace-chrome textarea:focus-visible__workspace_document__ { outline:2px solid var(--lambda-app-primary); outline-offset:2px; }
")
