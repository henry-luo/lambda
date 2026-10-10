// selectors and custom properties are family-local; native controls keep UA behavior (D7.2.5).
pub let css = "
.bold-root,.bold-config-provider{font-family:var(--bold-font-family);font-size:var(--bold-font-size);line-height:var(--bold-line-height);color:var(--bold-text);background:var(--bold-background)}
.bold-root{box-sizing:border-box;min-height:100vh;padding:32px}.bold-root *{box-sizing:border-box}
.bold-config-provider{border:0;padding:0;margin:0;min-width:0}
.bold-flex,.bold-space{min-width:0}.bold-flex>*{min-width:0}
.bold-button,.bold-input,.bold-select,.bold-text-area{font:inherit;color:var(--bold-text);border:var(--bold-border-width) solid var(--bold-border);border-radius:var(--bold-radius);background:var(--bold-surface)}
.bold-button,.bold-input,.bold-select{min-height:var(--bold-control-height);padding:8px 16px}
.bold-button{display:inline-flex;align-items:center;justify-content:center;gap:8px;vertical-align:middle;text-decoration:none;font-weight:700;cursor:pointer;background:var(--bold-primary);box-shadow:var(--bold-shadow-offset) var(--bold-shadow-offset) 0 var(--bold-shadow)}
.bold-button:hover{transform:translate(-1px,-1px)}.bold-button:active{transform:translate(var(--bold-shadow-offset),var(--bold-shadow-offset));box-shadow:none}
.bold-button:focus-visible,.bold-input:focus,.bold-select:focus,.bold-text-area:focus,.bold-link:focus-visible{outline:3px solid var(--bold-text);outline-offset:3px}
.bold-variant-secondary{background:var(--bold-secondary)}.bold-variant-destructive{background:var(--bold-error)}.bold-variant-outline{background:var(--bold-surface)}
.bold-button.bold-variant-ghost,.bold-button.bold-variant-link{background:transparent;box-shadow:none;border-color:transparent}
.bold-button.bold-variant-ghost:hover{background:var(--bold-secondary)}.bold-button.bold-variant-link{text-decoration:underline}
.bold-button:disabled,.bold-button[aria-disabled=true],.bold-config-provider:disabled .bold-button{background:var(--bold-disabled-background);color:var(--bold-disabled-text);box-shadow:none;transform:none;cursor:default}
.bold-input:disabled,.bold-select:disabled,.bold-text-area:disabled{background:var(--bold-disabled-background);color:var(--bold-disabled-text)}
.bold-size-small{--bold-control-height:32px;font-size:14px;padding:4px 10px}.bold-size-large{--bold-control-height:52px;font-size:18px;padding:12px 24px}
.bold-block{display:flex;width:100%}.bold-input,.bold-select{min-width:120px;max-width:100%}
.bold-text-area{padding:10px 14px;max-width:100%;vertical-align:top}
.bold-status-success{--bold-state:var(--bold-success)}.bold-status-warning{--bold-state:var(--bold-warning)}.bold-status-error{--bold-state:var(--bold-error)}.bold-status-info{--bold-state:var(--bold-info)}
.bold-input,.bold-select,.bold-text-area{box-shadow:inset 4px 0 0 var(--bold-state,transparent)}
.bold-checkbox,.bold-radio,.bold-switch{display:inline-flex;align-items:center;gap:10px;cursor:pointer}
.bold-checkbox input,.bold-radio input{margin:0;width:18px;height:18px;accent-color:var(--bold-text)}
.bold-switch{position:relative}.bold-switch input{position:absolute;z-index:1;opacity:0;width:48px;height:28px;margin:0;cursor:pointer}
.bold-switch-track{position:relative;display:inline-block;width:48px;height:28px;border:var(--bold-border-width) solid var(--bold-border);border-radius:var(--bold-radius);background:var(--bold-disabled-background)}
.bold-switch-track:before{content:'';position:absolute;top:3px;left:3px;width:18px;height:18px;background:var(--bold-text);border-radius:2px}
.bold-switch input:checked + .bold-switch-track{background:var(--bold-primary)}.bold-switch input:checked + .bold-switch-track:before{left:23px}
.bold-switch input:focus-visible + .bold-switch-track{outline:3px solid var(--bold-text);outline-offset:3px}.bold-switch input:disabled + .bold-switch-track{opacity:0.5}
.bold-title{font-weight:800;line-height:1.1;letter-spacing:-0.025em;margin:0 0 0.5em}
h1.bold-title{font-size:48px}h2.bold-title{font-size:36px}h3.bold-title{font-size:28px}h4.bold-title{font-size:22px}h5.bold-title{font-size:18px}h6.bold-title{font-size:16px}
.bold-paragraph{margin:0 0 1em}.bold-link{color:var(--bold-text);font-weight:700;text-decoration:underline;text-decoration-thickness:2px;text-underline-offset:3px}
.bold-link:hover{background:var(--bold-accent)}
.bold-card,.bold-alert{border:var(--bold-border-width) solid var(--bold-border);border-radius:var(--bold-radius);background:var(--bold-surface);box-shadow:var(--bold-shadow-offset) var(--bold-shadow-offset) 0 var(--bold-shadow)}
.bold-card{width:100%;overflow:hidden}.bold-card-header{padding:20px 24px;border-bottom:var(--bold-border-width) solid var(--bold-border);background:var(--bold-secondary)}
.bold-card-title{font-size:22px;line-height:1.2;font-weight:800;margin:0}.bold-card-description{margin:8px 0 0;color:var(--bold-text-secondary)}
.bold-card-body{padding:24px}.bold-card-footer{padding:16px 24px;border-top:var(--bold-border-width) solid var(--bold-border);background:var(--bold-background)}
.bold-badge{display:inline-flex;align-items:center;padding:2px 10px;border:var(--bold-border-width) solid var(--bold-border);border-radius:var(--bold-radius);background:var(--bold-primary);font-size:12px;font-weight:800;text-transform:uppercase;letter-spacing:0.04em}
.bold-badge.bold-variant-secondary{background:var(--bold-secondary)}.bold-badge.bold-variant-outline{background:transparent}.bold-badge.bold-variant-destructive{background:var(--bold-error)}
.bold-alert{padding:16px 20px;background:var(--bold-info)}.bold-alert-title{display:block;margin-bottom:4px;font-weight:800}
.bold-alert.bold-status-success{background:var(--bold-success)}.bold-alert.bold-status-warning{background:var(--bold-warning)}.bold-alert.bold-status-error{background:var(--bold-error)}
.bold-divider{border:0;border-top:var(--bold-border-width) solid var(--bold-border);margin:24px 0}
.bold-form-item{margin-bottom:20px}.bold-form-label{display:block;font-weight:700;padding-bottom:8px}.bold-form-control>.bold-input,.bold-form-control>.bold-text-area,.bold-form-control>.bold-select{width:100%}
.bold-form-help{margin-top:6px;font-size:14px;color:var(--bold-text-secondary);border-left:4px solid var(--bold-state,transparent);padding-left:8px}.bold-required,.bold-form-item.bold-status-error .bold-form-help{color:#b91c1c}
.bold-progress{display:flex;align-items:center;gap:12px}.bold-progress-track{height:24px;flex:1;min-width:0;overflow:hidden;border:var(--bold-border-width) solid var(--bold-border);border-radius:var(--bold-radius);background:var(--bold-surface)}
.bold-progress-fill{height:100%;background:var(--bold-primary)}.bold-progress-label{font-weight:800;min-width:3em}
.bold-progress.bold-status-success .bold-progress-fill{background:var(--bold-success)}.bold-progress.bold-status-warning .bold-progress-fill{background:var(--bold-warning)}.bold-progress.bold-status-error .bold-progress-fill{background:var(--bold-error)}.bold-progress.bold-status-info .bold-progress-fill{background:var(--bold-info)}
@media(max-width:600px){.bold-root{padding:20px}.bold-card-header,.bold-card-body,.bold-card-footer{padding:16px}h1.bold-title{font-size:36px}h2.bold-title{font-size:28px}}
"
