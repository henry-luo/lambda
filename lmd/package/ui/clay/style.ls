import base: lambda.ui.dtna.style

// canonical behavior styles stay identical to dtna; every clay-specific rule is scoped.
pub let css = base.css ++ "
.clay-root,.clay-config-provider{--clay-raised:var(--clay-shadow-size) var(--clay-shadow-size) calc(var(--clay-shadow-size) * 2) var(--clay-shadow),calc(0px - var(--clay-shadow-size) / 2) calc(0px - var(--clay-shadow-size) / 2) var(--clay-shadow-size) var(--clay-highlight),inset 2px 2px 4px var(--clay-highlight),inset -2px -2px 5px var(--clay-border);--clay-pressed:inset 3px 3px 7px var(--clay-shadow),inset -3px -3px 7px var(--clay-highlight);--clay-soft:3px 5px 10px var(--clay-shadow),inset 1px 1px 3px var(--clay-highlight),inset -1px -2px 4px var(--clay-border)}
.clay-root{padding:32px;background:var(--clay-background);min-height:100vh}
.clay-config-provider{background:transparent}
.clay-root .dtna-title{color:var(--dtna-text);letter-spacing:-0.025em;font-weight:700}
.clay-root .dtna-card,.clay-root .dtna-collapse,.clay-root .dtna-table{border:1px solid var(--clay-highlight);border-radius:var(--clay-surface-radius);background:var(--dtna-surface);box-shadow:var(--clay-raised)}
.clay-root .dtna-card-header{border-bottom:1px solid var(--dtna-border);padding:20px 24px;min-height:64px}
.clay-root .dtna-card-title{font-size:17px;font-weight:700;color:var(--dtna-text)}
.clay-root .dtna-card-body{padding:24px}
.clay-root .dtna-card-actions{background:var(--dtna-background);border-radius:0 0 var(--clay-surface-radius) var(--clay-surface-radius)}
.clay-root .dtna-card-inner{border-radius:var(--dtna-radius);box-shadow:var(--clay-soft)}
.clay-root .dtna-card-hoverable:hover{box-shadow:var(--clay-raised);border-color:var(--dtna-primary-border)}
.clay-root .dtna-button{padding:0 18px;border-color:var(--clay-highlight);background:var(--dtna-surface);box-shadow:var(--clay-soft);font-weight:600}
.clay-root .dtna-button:hover{color:var(--dtna-primary);border-color:var(--dtna-primary-border);background:var(--dtna-primary-background)}
.clay-root .dtna-button:active{box-shadow:var(--clay-pressed)}
.clay-root .dtna-button.dtna-variant-primary{background:linear-gradient(145deg,var(--dtna-primary-hover),var(--dtna-primary));border-color:var(--dtna-primary);color:var(--clay-highlight);box-shadow:3px 5px 10px var(--clay-shadow),inset 2px 2px 4px #ffffff66,inset -2px -3px 5px #34227655}
.clay-root .dtna-button.dtna-danger{color:var(--dtna-error);border-color:var(--dtna-error-border)}
.clay-root .dtna-button.dtna-danger.dtna-variant-primary{color:var(--clay-highlight);background:var(--dtna-error);border-color:var(--dtna-error)}
.clay-root .dtna-button.dtna-variant-text,.clay-root .dtna-button.dtna-variant-link{box-shadow:none;background:transparent;border-color:transparent}
.clay-root .dtna-button.dtna-variant-dashed{border:1px dashed var(--dtna-primary-border)}
.clay-root .dtna-button.dtna-button-ghost{background:transparent;box-shadow:none;color:var(--dtna-primary);border-color:var(--dtna-primary)}
.clay-root .dtna-button:disabled,.clay-root .dtna-button-disabled{box-shadow:none;background:var(--dtna-disabled-background);color:var(--dtna-disabled-text);border-color:var(--dtna-border)}
.clay-root .dtna-input,.clay-root .dtna-text-area,.clay-root .dtna-select{background:var(--dtna-surface);border-color:var(--clay-highlight);box-shadow:var(--clay-pressed);padding:10px 14px}
.clay-root .dtna-input:focus,.clay-root .dtna-text-area:focus,.clay-root .dtna-select:focus{outline:2px solid var(--dtna-primary);outline-offset:2px}
.clay-root .dtna-input.dtna-status-error,.clay-root .dtna-text-area.dtna-status-error,.clay-root .dtna-select.dtna-status-error{border-color:var(--dtna-error)}
.clay-root .dtna-input.dtna-status-warning,.clay-root .dtna-text-area.dtna-status-warning,.clay-root .dtna-select.dtna-status-warning{border-color:var(--dtna-warning)}
.clay-root .dtna-size-small{--dtna-control-height:32px}.clay-root .dtna-size-large{--dtna-control-height:50px}
.clay-root .dtna-size-small.dtna-input,.clay-root .dtna-size-small.dtna-select{padding:4px 10px}
.clay-root .dtna-switch-track{width:48px;height:28px;border-radius:18px;background:var(--dtna-border);box-shadow:var(--clay-pressed)}
.clay-root .dtna-switch input{width:48px;height:28px}
.clay-root .dtna-switch-track:before{width:22px;height:22px;left:3px;top:3px;box-shadow:2px 2px 4px var(--clay-shadow),inset 1px 1px 3px var(--clay-highlight)}
.clay-root .dtna-switch input:checked + .dtna-switch-track:before{left:23px}
.clay-root .dtna-segmented{padding:5px;border-radius:var(--dtna-radius);background:var(--dtna-border);box-shadow:var(--clay-pressed)}
.clay-root .dtna-segmented .dtna-choice{border-radius:calc(var(--dtna-radius) - 4px);padding:0 16px}
.clay-root .dtna-segmented .dtna-choice-active{color:var(--dtna-primary);background:var(--dtna-surface);box-shadow:var(--clay-soft)}
.clay-root .dtna-tabs > .dtna-choice-list{border-bottom:1px solid var(--dtna-border);gap:8px;padding-bottom:10px}
.clay-root .dtna-tabs .dtna-choice{border:0;border-radius:var(--dtna-radius);padding:10px 16px}
.clay-root .dtna-tabs .dtna-choice-active{background:var(--dtna-primary-background);box-shadow:var(--clay-soft)}
.clay-root .dtna-menu .dtna-choice-list{gap:8px}.clay-root .dtna-menu .dtna-choice{border:0;border-radius:var(--dtna-radius);padding:12px 16px}
.clay-root .dtna-menu .dtna-choice-active{background:var(--dtna-primary-background);box-shadow:var(--clay-soft)}
.clay-root .dtna-pagination button{height:36px;min-width:36px;border-color:var(--clay-highlight);background:var(--dtna-surface);box-shadow:var(--clay-soft)}
.clay-root .dtna-pagination .dtna-page-active{background:var(--dtna-primary-background);color:var(--dtna-primary);border-color:var(--dtna-primary-border)}
.clay-root .dtna-avatar{box-shadow:var(--clay-soft);background:var(--clay-lavender);color:var(--dtna-primary)}
.clay-root .dtna-tag{border-radius:999px;padding:3px 12px;box-shadow:inset 1px 1px 2px var(--clay-highlight)}
.clay-root .dtna-badge-count{box-shadow:var(--clay-soft)}
.clay-root .dtna-alert{border-radius:var(--dtna-radius);box-shadow:var(--clay-soft);padding:14px 18px}
.clay-root .dtna-alert.dtna-status-success{background:var(--clay-mint)}
.clay-root .dtna-alert.dtna-status-warning{background:var(--clay-peach)}
.clay-root .dtna-alert.dtna-status-error{background:var(--clay-rose)}
.clay-root .dtna-alert.dtna-status-info{background:var(--clay-lavender)}
.clay-root .dtna-alert-banner{border-radius:0;box-shadow:none}
.clay-root .dtna-progress-track{box-shadow:var(--clay-pressed);background:var(--dtna-border)}
.clay-root .dtna-progress-fill{box-shadow:inset 1px 1px 3px #ffffff66,inset -1px -2px 3px #34227622}
.clay-root .dtna-statistic-content{font-weight:700;letter-spacing:-0.035em}
.clay-root .dtna-table table{font:inherit}
.clay-root .dtna-table th{background:var(--dtna-background);font-weight:600}
.clay-root .dtna-table td,.clay-root .dtna-table th{padding:14px 16px;border-color:var(--dtna-border)}
.clay-root .dtna-card .dtna-table{border:0;box-shadow:none}
.clay-root .dtna-collapse-header{background:var(--dtna-surface);padding:16px 20px}
.clay-root .dtna-collapse-panel{background:var(--dtna-background)}
.clay-root .dtna-tree-node[aria-selected=true]{border-radius:12px;background:var(--dtna-primary-background)}
.clay-root .dtna-layout,.clay-root .dtna-layout-header,.clay-root .dtna-layout-sider,.clay-root .dtna-layout-footer{background:transparent;color:var(--dtna-text)}
.clay-root .dtna-form-item{margin-bottom:20px}.clay-root .dtna-form-label{font-weight:600}
.clay-root .dtna-form-control > .dtna-input,.clay-root .dtna-form-control > .dtna-text-area,.clay-root .dtna-form-control > .dtna-select{width:100%}
.clay-root .dtna-skeleton-shape{background:var(--dtna-border);border-radius:var(--dtna-radius)}
.clay-root .dtna-compact > *{box-shadow:none}
@media(max-width:575px){.clay-root{padding:18px}}
"
