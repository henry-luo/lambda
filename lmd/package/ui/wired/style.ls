// sketch overlays never participate in layout, focus, or hit testing (D7.2.5).
pub let css = "
.wired-root,.wired-config-provider{font-family:var(--wired-font-family);font-size:var(--wired-font-size);line-height:var(--wired-line-height);color:var(--wired-text);background:var(--wired-background)}
.wired-root{box-sizing:border-box;min-height:100vh;padding:36px}.wired-root *{box-sizing:border-box}
.wired-config-provider{border:0;padding:0;margin:0;min-width:0;background:transparent}
.wired-flex,.wired-space{min-width:0}.wired-flex>*{min-width:0}
.wired-sketch{position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none;overflow:visible;color:var(--wired-ink)}
.wired-sketch path{stroke-width:var(--wired-stroke-width)}
.wired-button,.wired-card,.wired-badge,.wired-alert,.wired-divider,.wired-progress-track,.wired-field{position:relative}
.wired-button,.wired-input,.wired-select,.wired-text-area{font:inherit;color:var(--wired-text);border:2px solid transparent;border-radius:0;background:var(--wired-surface)}
.wired-button{display:inline-flex;align-items:center;justify-content:center;vertical-align:middle;gap:8px;min-height:var(--wired-control-height);padding:8px 20px;cursor:pointer;text-decoration:none;text-transform:uppercase;letter-spacing:0.08em;font-size:14px}
.wired-button:hover{background:var(--wired-accent)}.wired-button:active{transform:translate(1px,1px)}
.wired-button.wired-variant-primary{color:var(--wired-primary);background:var(--wired-surface)}.wired-button.wired-variant-primary .wired-sketch{color:var(--wired-primary)}
.wired-variant-filled{background:var(--wired-secondary)}.wired-button.wired-variant-text{background:transparent;text-decoration:underline}
.wired-button:focus-visible,.wired-input:focus,.wired-select:focus,.wired-text-area:focus,.wired-link:focus-visible{outline:2px dashed var(--wired-primary);outline-offset:4px}
.wired-button:disabled,.wired-button[aria-disabled=true],.wired-config-provider:disabled .wired-button{color:var(--wired-disabled-text);background:var(--wired-disabled-background);cursor:default;transform:none}
.wired-field-disabled{opacity:0.6}.wired-input:disabled,.wired-select:disabled,.wired-text-area:disabled{color:var(--wired-disabled-text);background:var(--wired-disabled-background)}
.wired-button.wired-size-small{min-height:32px;font-size:12px;padding:4px 12px}.wired-button.wired-size-large{min-height:52px;font-size:16px;padding:12px 28px}
.wired-input.wired-size-small,.wired-select.wired-size-small{min-height:32px;font-size:14px;padding:4px 10px}.wired-input.wired-size-large,.wired-select.wired-size-large{min-height:52px;font-size:18px;padding:12px 16px}
.wired-block{display:flex;width:100%}.wired-field{display:inline-block;vertical-align:middle;width:240px;max-width:100%}
.wired-input,.wired-select,.wired-text-area{display:block;width:100%;min-height:var(--wired-control-height);padding:8px 12px;max-width:100%}.wired-text-area{resize:vertical}
.wired-input.wired-status-error,.wired-select.wired-status-error,.wired-text-area.wired-status-error{background:var(--wired-error)}
.wired-input.wired-status-warning,.wired-select.wired-status-warning,.wired-text-area.wired-status-warning{background:var(--wired-warning)}
.wired-checkbox,.wired-radio,.wired-switch{display:inline-flex;align-items:center;gap:10px;cursor:pointer}
.wired-checkbox input,.wired-radio input{margin:0;width:20px;height:20px;accent-color:var(--wired-primary)}
.wired-switch{position:relative}.wired-switch input{position:absolute;z-index:1;opacity:0;width:50px;height:28px;margin:0;cursor:pointer}
.wired-switch-track{position:relative;display:inline-block;width:50px;height:28px;border:3px double var(--wired-ink);border-radius:45% 52% 42% 48%;background:var(--wired-disabled-background)}
.wired-switch-track:before{content:'';position:absolute;top:2px;left:3px;width:18px;height:18px;border:2px solid var(--wired-ink);border-radius:48% 45% 50% 42%;background:var(--wired-surface)}
.wired-switch input:checked + .wired-switch-track{background:var(--wired-secondary)}.wired-switch input:checked + .wired-switch-track:before{left:23px;background:var(--wired-primary)}
.wired-switch input:focus-visible + .wired-switch-track{outline:2px dashed var(--wired-primary);outline-offset:4px}.wired-switch input:disabled + .wired-switch-track{opacity:0.5}
.wired-title{font-weight:400;line-height:1.2;margin:0 0 0.4em}h1.wired-title{font-size:48px}h2.wired-title{font-size:34px}h3.wired-title{font-size:26px}h4.wired-title{font-size:22px}h5.wired-title{font-size:18px}h6.wired-title{font-size:16px}
.wired-paragraph{margin:0 0 1em}.wired-link{color:var(--wired-primary);text-decoration:underline;text-underline-offset:4px}.wired-link:hover{background:var(--wired-accent)}
.wired-card{width:100%;background:var(--wired-surface);padding:6px}.wired-card-header{padding:20px 22px 0}.wired-card-title{margin:0;font-size:24px;font-weight:400;line-height:1.3}
.wired-card-description{margin:8px 0 0;color:var(--wired-text-secondary);font-size:14px}.wired-card-body{padding:22px}.wired-card-footer{padding:0 22px 20px;color:var(--wired-text-secondary)}
.wired-badge{display:inline-flex;align-items:center;padding:3px 12px;font-size:13px;white-space:nowrap;background:var(--wired-surface)}.wired-badge.wired-variant-primary{color:var(--wired-primary)}.wired-badge.wired-variant-filled{background:var(--wired-accent)}
.wired-alert{padding:16px 20px;background:var(--wired-info)}.wired-alert-title{display:block;margin-bottom:4px;font-weight:700}
.wired-alert.wired-status-success{background:var(--wired-success)}.wired-alert.wired-status-warning{background:var(--wired-warning)}.wired-alert.wired-status-error{background:var(--wired-error)}
.wired-divider{height:8px;margin:20px 0}
.wired-form-item{margin-bottom:18px}.wired-form-label{display:block;padding-bottom:8px}.wired-form-control>.wired-field{width:100%}
.wired-form-help{margin-top:6px;font-size:13px;color:var(--wired-text-secondary)}.wired-required,.wired-form-item.wired-status-error .wired-form-help{color:#a52a3e}
.wired-progress{display:flex;align-items:center;gap:12px}.wired-progress-track{height:26px;flex:1;min-width:0;padding:4px;background:var(--wired-surface)}
.wired-progress-fill{height:100%;background:repeating-linear-gradient(-48deg,var(--wired-primary),var(--wired-primary) 1px,transparent 1px,transparent 5px)}.wired-progress-label{min-width:3em;color:var(--wired-text-secondary)}
.wired-progress.wired-status-success{--wired-primary:#427a54}.wired-progress.wired-status-warning{--wired-primary:#9b7622}.wired-progress.wired-status-error{--wired-primary:#a52a3e}
@media(max-width:600px){.wired-root{padding:20px}.wired-card-header,.wired-card-body,.wired-card-footer{padding:16px}h1.wired-title{font-size:36px}h2.wired-title{font-size:28px}}
"
