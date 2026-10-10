import layout_parts: .layout
import avatar_parts: .avatar
import display_parts: .display
import icon_parts: .icons
import progress_parts: .progress
import timeline_parts: .timeline
import statistic_parts: .statistic
import skeleton_parts: .skeleton
import surfaces: .surfaces
import typography: .typography
import tag_parts: .tag
import rate_parts: .rate
import alert_parts: .alert
import spin_parts: .spin
import button_parts: .button

// Shared scoped rules consume inherited tokens; instances never install a global reset.
pub let css = "
.dtna-root,.dtna-config-provider{font-family:var(--dtna-font-family);font-size:var(--dtna-font-size);line-height:var(--dtna-line-height);color:var(--dtna-text);background:var(--dtna-background)}
.dtna-root *{box-sizing:border-box}
.dtna-root{padding:24px}
.dtna-config-provider{border:0;margin:0;padding:0;min-width:0}
.dtna-button,.dtna-input,.dtna-select,.dtna-text-area{font:inherit;color:inherit;border:1px solid var(--dtna-border);border-radius:var(--dtna-radius);background:var(--dtna-background);outline:none}
.dtna-button,.dtna-input,.dtna-select{height:var(--dtna-control-height);padding:4px 11px}
.dtna-input:hover,.dtna-select:hover,.dtna-text-area:hover{border-color:var(--dtna-primary-hover)}
.dtna-input:focus,.dtna-select:focus,.dtna-text-area:focus{outline:2px solid var(--dtna-primary-background);border-color:var(--dtna-primary)}
.dtna-button:disabled,.dtna-input:disabled,.dtna-select:disabled,.dtna-text-area:disabled{color:var(--dtna-disabled-text);background:var(--dtna-disabled-background);border-color:var(--dtna-border);cursor:default}
.dtna-size-small{--dtna-control-height:24px;--dtna-font-size:14px}
.dtna-size-large{--dtna-control-height:40px;--dtna-font-size:16px}
.dtna-text-area{padding:4px 11px;min-width:120px;vertical-align:top}
.dtna-input,.dtna-select{min-width:120px}
.dtna-status-error{border-color:var(--dtna-error)}
.dtna-status-warning{border-color:var(--dtna-warning)}
.dtna-icon{display:inline-block;vertical-align:-0.125em;flex-shrink:0;width:1em;height:1em}
.dtna-checkbox,.dtna-radio{display:inline-flex;align-items:center;gap:8px;cursor:pointer}
.dtna-checkbox input,.dtna-radio input{margin:0;accent-color:var(--dtna-primary)}
.dtna-switch{display:inline-flex;position:relative;align-items:center}
.dtna-switch input{width:44px;height:22px;margin:0;opacity:0;position:absolute;z-index:1;cursor:pointer}
.dtna-switch-track{display:inline-block;width:44px;height:22px;border-radius:11px;background:#bfbfbf;position:relative}
.dtna-switch-track:before{content:'';position:absolute;left:2px;top:2px;width:18px;height:18px;border-radius:50%;background:#fff}
.dtna-switch input:checked + .dtna-switch-track{background:var(--dtna-primary)}
.dtna-switch input:checked + .dtna-switch-track:before{left:24px}
.dtna-switch input:focus-visible + .dtna-switch-track{outline:2px solid var(--dtna-primary-background)}
.dtna-switch input:disabled + .dtna-switch-track{opacity:0.45}
.dtna-title{line-height:1.3;margin-bottom:0.5em;font-weight:600}
.dtna-paragraph{margin:0 0 1em}
.dtna-link{color:var(--dtna-primary);text-decoration:none}
.dtna-link:hover{text-decoration:underline}
.dtna-space-item{display:inline-flex;min-width:0;align-items:center}.dtna-space-separator{display:inline-flex;align-items:center}
.dtna-compact{display:inline-flex;border:0;padding:0;margin:0;min-width:0;align-items:stretch}
.dtna-compact-block{display:flex;width:100%}.dtna-compact-vertical{flex-direction:column}
.dtna-compact > *{border-radius:0;margin-inline-start:-1px}
.dtna-compact > :first-child{border-start-start-radius:var(--dtna-radius);border-end-start-radius:var(--dtna-radius);margin-inline-start:0}
.dtna-compact > :last-child{border-start-end-radius:var(--dtna-radius);border-end-end-radius:var(--dtna-radius)}
.dtna-compact-vertical > *{margin-inline-start:0;margin-top:-1px}
.dtna-compact-vertical > :first-child{border-radius:var(--dtna-radius) var(--dtna-radius) 0 0;margin-top:0}
.dtna-compact-vertical > :last-child{border-radius:0 0 var(--dtna-radius) var(--dtna-radius)}
.dtna-compact > :only-child{border-radius:var(--dtna-radius)}
.dtna-compact > :hover,.dtna-compact > :focus-visible{position:relative;z-index:1}
.dtna-divider{border-top:1px solid var(--dtna-border);margin:24px 0;text-align:center}
.dtna-divider-labelled{display:flex;align-items:center;width:100%;border-top:0}
.dtna-divider-line{flex:1;border-top:1px solid var(--dtna-border)}.dtna-divider-label{padding:0 1em;font-weight:600}
.dtna-divider-start > .dtna-divider-line:first-child,.dtna-divider-end > .dtna-divider-line:last-child{flex:0 0 5%}
.dtna-divider-dashed,.dtna-divider-dashed > .dtna-divider-line{border-top-style:dashed}.dtna-divider-plain .dtna-divider-label{font-weight:400}
.dtna-layout{display:flex;flex-direction:column;min-width:0;background:var(--dtna-surface)}
.dtna-layout-has-sider{flex-direction:row}
.dtna-layout-header,.dtna-layout-footer{padding:16px 24px}
.dtna-layout-header{background:#001529;color:#fff}
.dtna-layout-content{padding:24px;flex:1;min-width:0}
.dtna-layout-sider{background:#001529;color:#fff;width:200px;flex-shrink:0;padding:16px}
.dtna-row{--dtna-gutter:var(--dtna-row-base-x);row-gap:var(--dtna-row-base-y);display:flex;flex-wrap:wrap;align-items:var(--dtna-row-align-base);justify-content:var(--dtna-row-justify-base);width:calc(100% + var(--dtna-gutter,0px));margin-left:calc(var(--dtna-gutter,0px)*-0.5);margin-right:calc(var(--dtna-gutter,0px)*-0.5)}
.dtna-col{position:relative;inset-inline-start:var(--dtna-col-base-push);inset-inline-end:var(--dtna-col-base-pull);flex:0 0 var(--dtna-col-base-span);max-width:var(--dtna-col-base-span);display:var(--dtna-col-base-display);margin-inline-start:var(--dtna-col-base-offset);order:var(--dtna-col-base-order);min-width:0;padding-left:calc(var(--dtna-gutter,0px)*0.5);padding-right:calc(var(--dtna-gutter,0px)*0.5)}
.dtna-badge{display:inline-block;position:relative}
.dtna-badge-count{position:absolute;right:-10px;top:-10px;background:var(--dtna-error);color:#fff;min-width:20px;height:20px;line-height:20px;text-align:center;border-radius:10px;padding:0 6px;font-size:12px}
.dtna-card{border:1px solid var(--dtna-border);border-radius:8px;background:var(--dtna-background);overflow:hidden}
.dtna-card-header{display:flex;align-items:center;padding:16px 24px;border-bottom:1px solid var(--dtna-border);gap:16px}
.dtna-card-title{flex:1;font-weight:600;font-size:16px}.dtna-card-extra{color:var(--dtna-primary)}
.dtna-card-body{padding:24px}.dtna-card-actions{display:flex;padding:12px 24px;gap:24px;border-top:1px solid var(--dtna-border)}
.dtna-empty{text-align:center;color:var(--dtna-text-secondary);padding:32px}
.dtna-steps{display:flex;list-style:none;padding:0;margin:0;gap:24px}.dtna-step{display:flex;align-items:flex-start;gap:8px;flex:1}
.dtna-step-number{display:inline-flex;align-items:center;justify-content:center;width:32px;height:32px;border-radius:50%;background:var(--dtna-disabled-background);flex-shrink:0}
.dtna-step-current .dtna-step-number{background:var(--dtna-primary);color:#fff}.dtna-step-finished .dtna-step-number{background:var(--dtna-primary-background);color:var(--dtna-primary)}
.dtna-step-title{font-size:16px}.dtna-step-description{color:var(--dtna-text-secondary);font-size:12px}
.dtna-step-button{display:flex;align-items:flex-start;gap:8px;font:inherit;text-align:start;color:inherit;border:0;padding:0;background:transparent;cursor:pointer;width:100%}
.dtna-step-button:disabled{color:var(--dtna-disabled-text);cursor:default}
.dtna-step-status-error .dtna-step-number{color:var(--dtna-error);background:var(--dtna-error-background)}.dtna-step-status-error .dtna-step-title{color:var(--dtna-error)}
.dtna-step-subtitle{font-size:12px;color:var(--dtna-text-secondary);font-weight:400;margin-inline-start:8px}
.dtna-steps-vertical{flex-direction:column}.dtna-steps.dtna-size-small .dtna-step-number{width:24px;height:24px}
@media(max-width:575px){.dtna-steps-responsive{flex-direction:column}}
.dtna-breadcrumb ol{display:flex;list-style:none;padding:0;margin:0;gap:8px;color:var(--dtna-text-secondary)}
.dtna-breadcrumb-separator{padding-right:8px}.dtna-breadcrumb a{color:inherit;text-decoration:none}
.dtna-descriptions{margin:0}.dtna-description{display:flex;padding:8px 0;gap:16px}.dtna-description dt{color:var(--dtna-text-secondary);min-width:100px}.dtna-description dd{margin:0}
.dtna-result{text-align:center;padding:32px}.dtna-result-icon{font-size:48px;color:var(--dtna-primary)}
.dtna-close{border:0;background:transparent;cursor:pointer;padding:0 0 0 8px;color:var(--dtna-text-secondary);font:inherit}

.dtna-choice-list{display:flex;gap:4px;border-bottom:1px solid var(--dtna-border)}
.dtna-choice{font:inherit;border:0;border-bottom:2px solid transparent;padding:10px 16px;background:transparent;color:var(--dtna-text);cursor:pointer}
.dtna-choice-active{color:var(--dtna-primary);border-bottom-color:var(--dtna-primary)}
.dtna-choice:disabled{color:var(--dtna-disabled-text);cursor:default}
.dtna-choice:focus-visible{outline:2px solid var(--dtna-primary)}
.dtna-tab-panel{padding:16px 0}
.dtna-segmented{display:inline-flex;font-size:var(--dtna-font-size);border-radius:var(--dtna-radius)}
.dtna-segmented .dtna-choice-list{width:100%;border:0;padding:2px;gap:0;background:var(--dtna-disabled-background);border-radius:inherit}
.dtna-segmented .dtna-choice{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-height:calc(var(--dtna-control-height) - 4px);line-height:calc(var(--dtna-control-height) - 4px);border:0;border-radius:max(0px,calc(var(--dtna-radius) - 2px));padding:0 11px}
.dtna-segmented .dtna-choice-active{color:var(--dtna-text);background:var(--dtna-background);box-shadow:0 2px 8px #00000026}
.dtna-segmented-block{display:flex;width:100%}.dtna-segmented-block .dtna-choice-list{flex:1;min-width:0}.dtna-segmented-block .dtna-choice{flex:1;min-width:0}
.dtna-segmented.dtna-size-small .dtna-choice{padding-inline:7px;border-radius:max(0px,calc(var(--dtna-radius) - 4px))}
.dtna-segmented.dtna-size-large .dtna-choice{border-radius:var(--dtna-radius)}
.dtna-segmented-round,.dtna-segmented-round .dtna-choice{border-radius:9999px}
.dtna-choice-icon{display:inline-flex;align-items:center}.dtna-choice-label{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.dtna-pagination{display:flex;align-items:center;flex-wrap:wrap;gap:8px}.dtna-pagination button{font:inherit;min-width:32px;height:32px;border:1px solid var(--dtna-border);border-radius:var(--dtna-radius);background:var(--dtna-background);cursor:pointer}
.dtna-page-size{min-width:110px}.dtna-page-quick{display:inline-flex;align-items:center;gap:8px}.dtna-page-quick input{width:52px;min-width:0}
.dtna-pagination button:focus-visible,.dtna-step-button:focus-visible{outline:2px solid var(--dtna-primary)}
.dtna-page-simple{display:inline-flex;align-items:center;gap:8px}.dtna-page-simple .dtna-input{width:48px;min-width:0;text-align:center;padding-inline:4px}
.dtna-pagination.dtna-size-small button{min-width:24px;height:24px;border-color:transparent}.dtna-pagination.dtna-size-small .dtna-input,.dtna-pagination.dtna-size-small .dtna-select{height:24px}
.dtna-pagination .dtna-page-active{color:var(--dtna-primary);border-color:var(--dtna-primary)}.dtna-pagination button:disabled{color:var(--dtna-disabled-text)}

.dtna-form-item{margin-bottom:24px}.dtna-form-label{display:block;padding-bottom:8px}.dtna-required{color:var(--dtna-error);margin-right:4px}
.dtna-form-help{font-size:12px;color:var(--dtna-text-secondary);margin-top:4px}.dtna-form-item.dtna-status-error .dtna-form-help{color:var(--dtna-error)}

.dtna-button,.dtna-input,.dtna-select,.dtna-text-area{font-size:var(--dtna-font-size)}
.dtna-divider[aria-orientation=vertical]{display:inline-block;border-top:0;border-left:1px solid var(--dtna-border);height:0.9em;margin:0 8px;vertical-align:middle}
.dtna-divider-dashed[aria-orientation=vertical]{border-left-style:dashed}
.dtna-vertical>.dtna-choice-list{flex-direction:column;align-items:stretch}
.dtna-collapse{border:1px solid var(--dtna-border);border-radius:var(--dtna-radius);overflow:hidden}
.dtna-collapse-item+.dtna-collapse-item{border-top:1px solid var(--dtna-border)}
.dtna-collapse-header{display:flex;align-items:center;background:var(--dtna-surface);padding:0 16px;gap:16px}
.dtna-collapse-header>button{display:flex;align-items:center;gap:12px;flex:1;text-align:left;font:inherit;border:0;background:transparent;padding:12px 0;cursor:pointer;color:inherit}
.dtna-collapse-header>button:disabled{color:var(--dtna-disabled-text);cursor:default}
.dtna-collapse-header>button:focus-visible{outline:2px solid var(--dtna-primary)}
.dtna-collapse-panel{padding:16px;border-top:1px solid var(--dtna-border)}
.dtna-tree-node{display:flex;align-items:center;min-height:32px;gap:4px;cursor:pointer;border-radius:4px}
.dtna-tree-node:hover{background:var(--dtna-surface)}.dtna-tree-selected{background:var(--dtna-primary-background);color:var(--dtna-primary)}
.dtna-tree-node:focus-visible{outline:2px solid var(--dtna-primary)}.dtna-tree-node[aria-disabled=true]{color:var(--dtna-disabled-text);cursor:default}
.dtna-tree-expander,.dtna-tree-spacer{display:inline-flex;width:24px;height:24px;align-items:center;justify-content:center;flex-shrink:0}
.dtna-tree-expander{font:inherit;border:0;background:transparent;color:inherit;cursor:pointer}
.dtna-check{display:inline-flex;align-items:center;justify-content:center;width:16px;height:16px;border:1px solid var(--dtna-border);border-radius:3px;background:var(--dtna-background);color:var(--dtna-primary);font-size:12px;flex-shrink:0}
.dtna-tree-label{padding:2px 4px}
.dtna-table-scroll{overflow:auto}.dtna-table table{width:100%;border-collapse:collapse;text-align:left}
.dtna-table caption{padding:12px 0;text-align:left;font-weight:600;font-size:16px}
.dtna-table th,.dtna-table td{padding:12px 16px;border-bottom:1px solid var(--dtna-border)}
.dtna-table th{background:var(--dtna-surface);font-weight:600}.dtna-table-bordered th,.dtna-table-bordered td{border:1px solid var(--dtna-border)}
.dtna-table-selected{background:var(--dtna-primary-background)}.dtna-table-detail{background:var(--dtna-surface)}
.dtna-table .dtna-pagination{justify-content:flex-end;margin-top:16px}
.dtna-table-sort{display:inline-flex;align-items:center;gap:6px;font:inherit;font-weight:600;border:0;padding:0;background:transparent;color:inherit;cursor:pointer}
.dtna-table-filters{display:flex;gap:4px;margin-top:6px}.dtna-table-filters button{font:inherit;font-size:12px;border:0;border-radius:3px;padding:2px 6px;background:transparent;color:var(--dtna-text-secondary);cursor:pointer}
.dtna-table-filters button[aria-pressed=true]{color:var(--dtna-primary);background:var(--dtna-primary-background)}
.dtna-table-expand{font:inherit;border:1px solid var(--dtna-border);border-radius:3px;background:var(--dtna-background);width:22px;height:22px;cursor:pointer}
.dtna-table button:disabled{color:var(--dtna-disabled-text);cursor:default}.dtna-table button:focus-visible{outline:2px solid var(--dtna-primary)}
.dtna-table-loading{padding:12px;color:var(--dtna-primary)}.dtna-table.dtna-size-small th,.dtna-table.dtna-size-small td{padding:6px 8px}
" ++ layout_parts.css ++ display_parts.css ++ icon_parts.css ++ progress_parts.css ++ timeline_parts.css ++ statistic_parts.css ++ skeleton_parts.css ++ surfaces.css ++ typography.css ++ avatar_parts.css ++ tag_parts.css ++ rate_parts.css ++ alert_parts.css ++ spin_parts.css ++ button_parts.css
