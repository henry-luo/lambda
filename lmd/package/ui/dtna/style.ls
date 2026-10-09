// Shared scoped rules consume inherited tokens; instances never install a global reset.
pub let css = "
.dtna-root,.dtna-config-provider{font-family:var(--dtna-font-family);font-size:var(--dtna-font-size);line-height:var(--dtna-line-height);color:var(--dtna-text);background:var(--dtna-background)}
.dtna-root *{box-sizing:border-box}
.dtna-root{padding:24px}
.dtna-config-provider{border:0;margin:0;padding:0;min-width:0}
.dtna-button,.dtna-input,.dtna-select,.dtna-text-area{font:inherit;color:inherit;border:1px solid var(--dtna-border);border-radius:var(--dtna-radius);background:var(--dtna-background);outline:none}
.dtna-button,.dtna-input,.dtna-select{height:var(--dtna-control-height);padding:4px 11px}
.dtna-button{display:inline-flex;align-items:center;justify-content:center;gap:8px;cursor:pointer;white-space:nowrap}
.dtna-button:hover,.dtna-input:hover,.dtna-select:hover,.dtna-text-area:hover{border-color:var(--dtna-primary-hover)}
.dtna-button:focus-visible,.dtna-input:focus,.dtna-select:focus,.dtna-text-area:focus{outline:2px solid var(--dtna-primary-background);border-color:var(--dtna-primary)}
.dtna-button:active{border-color:var(--dtna-primary-active);color:var(--dtna-primary-active)}
.dtna-button.dtna-variant-primary{color:#fff;background:var(--dtna-primary);border-color:var(--dtna-primary)}
.dtna-button.dtna-variant-primary:hover{background:var(--dtna-primary-hover);border-color:var(--dtna-primary-hover)}
.dtna-button.dtna-variant-text,.dtna-button.dtna-variant-link{border-color:transparent;background:transparent}
.dtna-button.dtna-variant-link{color:var(--dtna-primary)}
.dtna-button.dtna-variant-dashed{border-style:dashed}
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
.dtna-title{line-height:1.3;margin:0 0 16px;font-weight:600}
.dtna-paragraph{margin:0 0 16px}
.dtna-link{color:var(--dtna-primary);text-decoration:none}
.dtna-link:hover{text-decoration:underline}
.dtna-space-compact{display:inline-flex}
.dtna-space-compact > *{border-radius:0;margin-left:-1px}
.dtna-space-compact > :first-child{border-radius:var(--dtna-radius) 0 0 var(--dtna-radius);margin-left:0}
.dtna-space-compact > :last-child{border-radius:0 var(--dtna-radius) var(--dtna-radius) 0}
.dtna-divider{border-top:1px solid var(--dtna-border);margin:24px 0;text-align:center}
.dtna-layout{display:flex;flex-direction:column;min-width:0;background:var(--dtna-surface)}
.dtna-layout-has-sider{flex-direction:row}
.dtna-layout-header,.dtna-layout-footer{padding:16px 24px}
.dtna-layout-header{background:#001529;color:#fff}
.dtna-layout-content{padding:24px;flex:1;min-width:0}
.dtna-layout-sider{background:#001529;color:#fff;width:200px;flex-shrink:0;padding:16px}
.dtna-row{width:100%}.dtna-col{min-width:0}
.dtna-avatar{display:inline-flex;align-items:center;justify-content:center;width:32px;height:32px;background:#bfbfbf;color:#fff;border-radius:50%;overflow:hidden}
.dtna-avatar img{width:100%;height:100%;object-fit:cover}
.dtna-badge{display:inline-block;position:relative}
.dtna-badge-count{position:absolute;right:-10px;top:-10px;background:var(--dtna-error);color:#fff;min-width:20px;height:20px;line-height:20px;text-align:center;border-radius:10px;padding:0 6px;font-size:12px}
.dtna-card{border:1px solid var(--dtna-border);border-radius:8px;background:var(--dtna-background);overflow:hidden}
.dtna-card-header{display:flex;align-items:center;padding:16px 24px;border-bottom:1px solid var(--dtna-border);gap:16px}
.dtna-card-title{flex:1;font-weight:600;font-size:16px}.dtna-card-extra{color:var(--dtna-primary)}
.dtna-card-body{padding:24px}.dtna-card-actions{display:flex;padding:12px 24px;gap:24px;border-top:1px solid var(--dtna-border)}
.dtna-empty{text-align:center;color:var(--dtna-text-secondary);padding:32px}
.dtna-statistic-title{color:var(--dtna-text-secondary);margin-bottom:4px}.dtna-statistic-value{font-size:24px}
.dtna-progress{display:flex;align-items:center;gap:8px}.dtna-progress-track{height:8px;flex:1;min-width:80px;background:var(--dtna-disabled-background);border-radius:4px;overflow:hidden}
.dtna-progress-fill{height:100%;background:var(--dtna-primary);border-radius:4px}.dtna-progress-label{font-size:12px;min-width:32px}
.dtna-timeline{list-style:none;padding:0 0 0 6px;margin:0}.dtna-timeline-item{position:relative;border-left:2px solid var(--dtna-border);padding:0 0 20px 20px}
.dtna-timeline-dot{position:absolute;left:-6px;top:4px;width:10px;height:10px;border:2px solid var(--dtna-primary);border-radius:50%;background:#fff}
.dtna-steps{display:flex;list-style:none;padding:0;margin:0;gap:24px}.dtna-step{display:flex;align-items:flex-start;gap:8px;flex:1}
.dtna-step-number{display:inline-flex;align-items:center;justify-content:center;width:32px;height:32px;border-radius:50%;background:var(--dtna-disabled-background);flex-shrink:0}
.dtna-step-current .dtna-step-number{background:var(--dtna-primary);color:#fff}.dtna-step-finished .dtna-step-number{background:var(--dtna-primary-background);color:var(--dtna-primary)}
.dtna-step-title{font-size:16px}.dtna-step-description{color:var(--dtna-text-secondary);font-size:12px}
.dtna-breadcrumb ol{display:flex;list-style:none;padding:0;margin:0;gap:8px;color:var(--dtna-text-secondary)}
.dtna-breadcrumb-separator{padding-right:8px}.dtna-breadcrumb a{color:inherit;text-decoration:none}
.dtna-descriptions{margin:0}.dtna-description{display:flex;padding:8px 0;gap:16px}.dtna-description dt{color:var(--dtna-text-secondary);min-width:100px}.dtna-description dd{margin:0}
.dtna-skeleton-line{height:16px;margin:12px 0;background:var(--dtna-disabled-background);border-radius:4px}
.dtna-spin{display:flex;align-items:center;justify-content:center;gap:8px;color:var(--dtna-primary)}
.dtna-result{text-align:center;padding:32px}.dtna-result-icon{font-size:48px;color:var(--dtna-primary)}
.dtna-alert,.dtna-tag{position:relative;display:block;border:1px solid var(--dtna-border);border-radius:var(--dtna-radius);padding:8px 12px;background:var(--dtna-primary-background)}
.dtna-alert.dtna-status-success{background:var(--dtna-success-background)}.dtna-alert.dtna-status-warning{background:var(--dtna-warning-background)}.dtna-alert.dtna-status-error{background:var(--dtna-error-background)}
.dtna-alert-description{margin-top:4px;color:var(--dtna-text-secondary)}
.dtna-tag{display:inline-flex;align-items:center;gap:6px;padding:0 7px;font-size:12px;line-height:20px}
.dtna-close{border:0;background:transparent;cursor:pointer;padding:0 0 0 8px;color:var(--dtna-text-secondary);font:inherit}

.dtna-choice-list{display:flex;gap:4px;border-bottom:1px solid var(--dtna-border)}
.dtna-choice{font:inherit;border:0;border-bottom:2px solid transparent;padding:10px 16px;background:transparent;color:var(--dtna-text);cursor:pointer}
.dtna-choice-active{color:var(--dtna-primary);border-bottom-color:var(--dtna-primary)}
.dtna-choice:disabled{color:var(--dtna-disabled-text);cursor:default}
.dtna-choice:focus-visible{outline:2px solid var(--dtna-primary)}
.dtna-tab-panel{padding:16px 0}.dtna-segmented .dtna-choice-list{border:0;padding:2px;background:var(--dtna-disabled-background);border-radius:var(--dtna-radius)}
.dtna-segmented .dtna-choice{border:0;border-radius:4px;padding:4px 12px}.dtna-segmented .dtna-choice-active{color:var(--dtna-text);background:var(--dtna-background)}
.dtna-pagination{display:flex;gap:8px}.dtna-pagination button{font:inherit;min-width:32px;height:32px;border:1px solid var(--dtna-border);border-radius:var(--dtna-radius);background:var(--dtna-background);cursor:pointer}
.dtna-pagination .dtna-page-active{color:var(--dtna-primary);border-color:var(--dtna-primary)}.dtna-pagination button:disabled{color:var(--dtna-disabled-text)}

.dtna-form-item{margin-bottom:24px}.dtna-form-label{display:block;padding-bottom:8px}.dtna-required{color:var(--dtna-error);margin-right:4px}
.dtna-form-help{font-size:12px;color:var(--dtna-text-secondary);margin-top:4px}.dtna-form-item.dtna-status-error .dtna-form-help{color:var(--dtna-error)}

.dtna-button,.dtna-input,.dtna-select,.dtna-text-area{font-size:var(--dtna-font-size)}
.dtna-button.dtna-block{width:100%}.dtna-button.dtna-shape-round{border-radius:999px}.dtna-button.dtna-shape-circle{width:var(--dtna-control-height);padding:0;border-radius:50%}
.dtna-button.dtna-danger{color:var(--dtna-error);border-color:var(--dtna-error)}.dtna-button.dtna-danger.dtna-variant-primary{color:#fff;background:var(--dtna-error)}
.dtna-divider[aria-orientation=vertical]{display:inline-block;border-top:0;border-left:1px solid var(--dtna-border);height:0.9em;margin:0 8px;vertical-align:middle}
.dtna-tag.dtna-status-success{color:var(--dtna-success);background:var(--dtna-success-background)}.dtna-tag.dtna-status-error{color:var(--dtna-error);background:var(--dtna-error-background)}
.dtna-progress.dtna-status-success .dtna-progress-fill{background:var(--dtna-success)}.dtna-progress.dtna-status-error .dtna-progress-fill{background:var(--dtna-error)}

.dtna-tag{white-space:nowrap}.dtna-alert{padding-right:36px}.dtna-alert>.dtna-close{position:absolute;right:12px;top:10px}
.dtna-statistic-suffix{font-size:14px;margin-left:4px}.dtna-timeline-label{margin-bottom:4px;font-weight:600}
.dtna-vertical>.dtna-choice-list{flex-direction:column;align-items:stretch}
"
