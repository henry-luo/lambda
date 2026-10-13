import base: lambda.ui.dtna.style

// every material rule stays beneath the skin root; behavioral selectors remain shared.
pub let css = base.css ++ "
.blade-root,.blade-config-provider{--blade-bevel:inset 0 1px 0 #ffe8b866,inset 0 -3px 5px #00000088,0 5px 12px var(--blade-shadow)}
.blade-root{padding:36px;min-height:100vh;background:radial-gradient(ellipse at 50% 0%,#4b3829 0%,var(--blade-background) 78%)}
.blade-config-provider{background:transparent}
.blade-root .dtna-title{font-weight:600;color:var(--dtna-text);letter-spacing:0.025em;text-shadow:0 2px 2px #00000088}
.blade-root .dtna-link{color:var(--dtna-primary)}
.blade-root .dtna-typography mark{color:var(--blade-ink);background:var(--blade-gold-light)}
.blade-root .dtna-card,.blade-root .dtna-collapse,.blade-root .dtna-table{border:var(--blade-frame-width) solid var(--dtna-border);border-radius:var(--dtna-radius);background:linear-gradient(140deg,var(--dtna-surface),var(--dtna-background));box-shadow:var(--blade-bevel)}
.blade-root .dtna-card-header{background:linear-gradient(180deg,#ffffff0a,#00000033);border-bottom:1px solid var(--dtna-border);min-height:54px;padding:14px 20px}
.blade-root .dtna-card-title{font-size:20px;font-weight:600;letter-spacing:0.025em}
.blade-root .dtna-card-body{padding:20px}.blade-root .dtna-card-actions{background:#00000022}
.blade-root .dtna-card-grid:hover,.blade-root .dtna-card-hoverable:hover{box-shadow:var(--blade-bevel);border-color:var(--dtna-primary)}
.blade-root .dtna-button{--dtna-button-bg:linear-gradient(180deg,#644933,var(--blade-wood));--dtna-button-bg-hover:linear-gradient(180deg,#796044,#493223);--dtna-button-bg-active:var(--blade-wood);--dtna-button-color:var(--dtna-text);--dtna-button-color-hover:var(--blade-gold-light);--dtna-button-border:var(--dtna-border);--dtna-button-shadow:var(--blade-bevel);padding:0 18px;font-weight:600;letter-spacing:0.035em;text-shadow:0 1px 1px #00000077}
.blade-root .dtna-button.dtna-variant-primary{--dtna-button-bg:linear-gradient(180deg,var(--dtna-primary-hover),var(--dtna-primary));--dtna-button-bg-hover:var(--dtna-primary-hover);--dtna-button-bg-active:var(--dtna-primary-active);--dtna-button-color:var(--blade-ink);--dtna-button-color-hover:var(--blade-ink);--dtna-button-color-active:var(--blade-ink);--dtna-button-border:var(--dtna-primary);--dtna-button-shadow:var(--blade-bevel);text-shadow:0 1px 0 #ffffff44}
.blade-root .dtna-button.dtna-danger{--dtna-button-border:var(--dtna-error);--dtna-button-color:var(--dtna-error)}
.blade-root .dtna-button.dtna-danger.dtna-variant-primary{--dtna-button-bg:var(--dtna-error);--dtna-button-bg-hover:var(--dtna-error-hover);--dtna-button-color:#fff;--dtna-button-color-hover:#fff}
.blade-root .dtna-button.dtna-variant-text,.blade-root .dtna-button.dtna-variant-link{--dtna-button-bg:transparent;--dtna-button-bg-hover:#ffffff0d;--dtna-button-border:transparent;--dtna-button-shadow:none;color:var(--dtna-primary)}
.blade-root .dtna-button.dtna-button-ghost{--dtna-button-bg:transparent;--dtna-button-color:var(--dtna-primary);--dtna-button-border:var(--dtna-primary)}
.blade-root .dtna-button:disabled,.blade-root .dtna-button-disabled{background:var(--dtna-disabled-background);color:var(--dtna-disabled-text);border-color:var(--dtna-border);box-shadow:none;text-shadow:none}
.blade-root .dtna-button:active{box-shadow:inset 0 2px 7px #00000077}
.blade-root .dtna-input,.blade-root .dtna-text-area,.blade-root .dtna-select{background:var(--dtna-background);border-color:var(--dtna-border);padding:8px 12px;box-shadow:inset 0 2px 5px #00000077;color:var(--dtna-text)}
.blade-root .dtna-select option{font:inherit;background:var(--dtna-background);color:var(--dtna-text)}
.blade-root .dtna-select option:checked,.blade-root .dtna-select option:hover{background:var(--dtna-primary);color:var(--blade-ink)}
.blade-root .dtna-select option:disabled{background:var(--dtna-disabled-background);color:var(--dtna-disabled-text)}
.blade-root .dtna-input:focus,.blade-root .dtna-text-area:focus,.blade-root .dtna-select:focus{outline:2px solid var(--dtna-primary);outline-offset:2px;border-color:var(--dtna-primary)}
.blade-root .dtna-input.dtna-status-error,.blade-root .dtna-text-area.dtna-status-error,.blade-root .dtna-select.dtna-status-error{border-color:var(--dtna-error)}
.blade-root .dtna-input.dtna-status-warning,.blade-root .dtna-text-area.dtna-status-warning,.blade-root .dtna-select.dtna-status-warning{border-color:var(--dtna-warning)}
.blade-root .dtna-input:disabled,.blade-root .dtna-text-area:disabled,.blade-root .dtna-select:disabled{color:var(--dtna-disabled-text);background:var(--dtna-disabled-background)}
.blade-root .dtna-size-small{--dtna-control-height:32px}.blade-root .dtna-size-large{--dtna-control-height:50px}
.blade-root .dtna-form-label{font-weight:600;color:var(--dtna-text)}
.blade-root .dtna-form-control>.dtna-input,.blade-root .dtna-form-control>.dtna-select,.blade-root .dtna-form-control>.dtna-text-area{width:100%}
.blade-root .dtna-switch-track{background:var(--dtna-border);box-shadow:inset 0 2px 5px #00000099}.blade-root .dtna-switch-track:before{background:var(--blade-gold-light);box-shadow:inset 0 -2px 3px var(--blade-gold-dark)}
.blade-root .dtna-segmented .dtna-choice-list{background:var(--dtna-background);box-shadow:inset 0 2px 5px #00000077;border:1px solid var(--dtna-border)}
.blade-root .dtna-segmented .dtna-choice-active{color:var(--blade-ink);background:linear-gradient(180deg,var(--blade-gold-light),var(--dtna-primary));box-shadow:var(--blade-bevel)}
.blade-root .dtna-tabs>.dtna-choice-list{border-color:var(--dtna-border);gap:6px}.blade-root .dtna-tabs .dtna-choice-active{background:#d7ad6217}
.blade-root .dtna-menu .dtna-choice-active{background:#d7ad6217;border-color:var(--dtna-primary)}
.blade-root .dtna-pagination button{color:var(--dtna-text);background:var(--dtna-surface);box-shadow:var(--blade-bevel)}
.blade-root .dtna-pagination .dtna-page-active{border-color:var(--dtna-primary);color:var(--dtna-primary)}
.blade-root .dtna-pagination button:disabled{color:var(--dtna-disabled-text);box-shadow:none}
.blade-root .dtna-table table{font:inherit;color:inherit}.blade-root .dtna-table th{background:var(--dtna-surface);color:var(--dtna-primary)}
.blade-root .dtna-table th,.blade-root .dtna-table td{border-color:var(--dtna-border);padding:12px 16px}.blade-root .dtna-card .dtna-table{box-shadow:none;border:0}
.blade-root .dtna-collapse-header{background:var(--dtna-surface)}.blade-root .dtna-collapse-panel{background:var(--dtna-background)}
.blade-root .dtna-tree-node[aria-selected=true]{background:#d7ad6222}
.blade-root .dtna-avatar{background:var(--blade-wood);color:var(--blade-gold-light);border:2px solid var(--dtna-border);box-shadow:var(--blade-bevel)}
.blade-root .dtna-tag{background:var(--dtna-surface);border-color:var(--dtna-border);color:var(--dtna-text);border-radius:3px}
.blade-root .dtna-checkable-tag[aria-checked=true]{background:var(--dtna-primary);color:var(--blade-ink)}
.blade-root .dtna-alert{color:var(--dtna-text);background:var(--dtna-surface);border-color:var(--dtna-primary);box-shadow:var(--blade-bevel)}
.blade-root .dtna-alert.dtna-status-success{border-color:var(--dtna-success)}.blade-root .dtna-alert.dtna-status-warning{border-color:var(--dtna-warning)}.blade-root .dtna-alert.dtna-status-error{border-color:var(--dtna-error)}
.blade-root .dtna-alert-message{color:var(--dtna-text)}.blade-root .dtna-alert-description{color:var(--dtna-text-secondary)}
.blade-root .dtna-progress-track{background:var(--dtna-background);box-shadow:inset 0 2px 4px #00000088;border:1px solid var(--dtna-border)}
.blade-root .dtna-progress-fill{box-shadow:inset 0 1px 2px #ffffff66,inset 0 -2px 3px #00000077}
.blade-root .dtna-layout,.blade-root .dtna-layout-header,.blade-root .dtna-layout-sider,.blade-root .dtna-layout-footer{background:transparent;color:var(--dtna-text)}
.blade-root .dtna-skeleton-shape{background:var(--dtna-border)}.blade-root .dtna-step-number{background:var(--dtna-surface);border:1px solid var(--dtna-border)}
.blade-root .dtna-step-current .dtna-step-number{background:var(--dtna-primary);color:var(--blade-ink)}
.blade-root .dtna-compact>*{box-shadow:none}
.blade-root .blade-panel{position:relative;border:var(--blade-frame-width) solid var(--blade-gold-dark);border-radius:var(--dtna-radius);box-shadow:inset 0 0 0 1px var(--blade-gold),inset 0 0 28px #00000066,0 12px 25px #00000088;background:linear-gradient(110deg,#ffffff08,#00000022),var(--blade-wood);padding:8px}
.blade-root .blade-panel-header{position:relative;display:flex;align-items:center;justify-content:center;gap:12px;min-height:60px;border-bottom:1px solid #b68b4c66;padding:4px 20px 12px}
.blade-root .blade-panel-title{font:600 25px/1.2 var(--dtna-font-family);letter-spacing:0.035em;margin:0;padding:10px 28px;color:var(--blade-gold-light);text-align:center;text-shadow:0 2px 2px #000;background:linear-gradient(180deg,#574031,#281e19);border:1px solid var(--blade-gold);border-radius:5px;box-shadow:inset 0 0 0 2px var(--blade-gold-dark),0 3px 8px #00000077}
.blade-root .blade-panel-extra{font-size:12px;color:var(--dtna-text-secondary)}
.blade-root .blade-panel-body{padding:18px;position:relative}
.blade-root .blade-material-iron{background:linear-gradient(135deg,#424749,#222426);border-color:var(--blade-iron);box-shadow:inset 0 0 0 1px #b1bdbe,inset 0 0 24px #00000077,0 12px 25px #00000088}
.blade-root .blade-material-parchment{background:radial-gradient(ellipse at center,var(--blade-parchment),#b79664);color:var(--blade-ink);--dtna-text:var(--blade-ink);--dtna-text-secondary:#725a3d;--dtna-background:var(--blade-parchment);--dtna-surface:#ddc392}
.blade-root .blade-material-parchment .dtna-title{text-shadow:none}
.blade-root .blade-corner{position:absolute;width:42px;height:42px;pointer-events:none;z-index:2;color:var(--blade-gold)}
.blade-root .blade-corner-tl{left:-4px;top:-4px}.blade-root .blade-corner-tr{right:-4px;top:-4px;transform:scaleX(-1)}
.blade-root .blade-corner-bl{left:-4px;bottom:-4px;transform:scaleY(-1)}.blade-root .blade-corner-br{right:-4px;bottom:-4px;transform:rotate(180deg)}
.blade-root .blade-rpg-art{display:block;flex-shrink:0;pointer-events:none}
.blade-root .blade-empty-art{display:inline-block;vertical-align:middle;color:var(--blade-gold);pointer-events:none}
.blade-root .blade-item-slot,.blade-root .blade-ability{position:relative;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0;width:var(--blade-slot-size);height:var(--blade-slot-size);padding:5px;font:inherit;cursor:pointer;color:var(--blade-gold-light);border:3px solid var(--blade-iron);border-radius:8px;background:radial-gradient(ellipse at 40% 30%,#5c4b39,#151516 85%);box-shadow:inset 0 0 0 1px #c6c7ba,inset 0 0 10px #000,0 3px 5px #00000099;outline:none}
.blade-root .blade-item-slot>.blade-rpg-art,.blade-root .blade-ability-face>.blade-rpg-art{width:100%;height:100%}
.blade-root .blade-rarity-uncommon{border-color:var(--blade-stamina)}.blade-root .blade-rarity-rare{border-color:var(--blade-mana);background:radial-gradient(ellipse at 40% 30%,#31586e,#151516 85%)}
.blade-root .blade-rarity-epic{border-color:var(--blade-epic);background:radial-gradient(ellipse at 40% 30%,#573567,#151516 85%)}
.blade-root .blade-rarity-legendary{border-color:var(--blade-gold);background:radial-gradient(ellipse at 40% 30%,#78532d,#1e1410 85%)}
.blade-root .blade-item-slot[aria-pressed=true]{border-color:var(--blade-gold-light);box-shadow:0 0 14px #e8be6688,inset 0 0 0 1px var(--blade-gold),inset 0 0 14px #c9a55944}
.blade-root .blade-item-slot:hover,.blade-root .blade-ability:hover{border-color:var(--blade-gold-light)}
.blade-root .blade-item-slot:focus-visible,.blade-root .blade-ability:focus-visible{outline:2px solid var(--dtna-primary);outline-offset:4px}
.blade-root .blade-item-slot:disabled,.blade-root .blade-ability:disabled{opacity:0.5;cursor:default}.blade-root .blade-item-slot:disabled>*,.blade-root .blade-ability:disabled>*{pointer-events:none}
.blade-root .blade-empty-slot{font-size:40px;color:#a1927755;pointer-events:none}
.blade-root .blade-slot-quantity{position:absolute;right:4px;bottom:1px;font-size:14px;color:#fff4cc;text-shadow:0 1px 2px #000;pointer-events:none}
.blade-root .blade-ability{border-radius:50%;border-color:var(--blade-gold);background:radial-gradient(ellipse at 40% 30%,#65563b,#171919 85%);overflow:visible}
.blade-root .blade-ability-face{position:relative;display:flex;align-items:center;justify-content:center;width:100%;height:100%;border-radius:50%;overflow:hidden;pointer-events:none}
.blade-root .blade-hotkey{position:absolute;right:-5px;bottom:-3px;min-width:24px;height:24px;padding:1px 5px;font:13px/20px Georgia,serif;text-align:center;background:#2d2118;border:1px solid var(--blade-gold);border-radius:50%;box-shadow:0 2px 3px #000;color:var(--blade-gold-light);pointer-events:none;z-index:2}
.blade-root .blade-cooldown{position:absolute;inset:0;background:#06090a66;pointer-events:none}
.blade-root .blade-meter{width:100%;min-width:180px}.blade-root .blade-meter-heading{text-align:center;font-size:15px;letter-spacing:0.08em;color:var(--dtna-text-secondary)}
.blade-root .blade-meter-shell{position:relative;height:76px}.blade-root .blade-meter-frame{position:absolute;inset:0;width:100%;height:100%;pointer-events:none;color:var(--blade-gold)}
.blade-root .blade-meter-rail{position:absolute;left:10.5%;right:10.5%;top:28px;height:20px;background:#100f13;border-radius:3px;box-shadow:inset 0 2px 7px #000,0 1px 0 #ffe0a866;overflow:hidden}
.blade-root .blade-meter-fill{height:100%;background:linear-gradient(180deg,#ffffff77,transparent 45%,#00000055),var(--blade-health);box-shadow:inset 0 1px 2px #ffffffaa,0 0 12px var(--blade-health)}
.blade-root .blade-meter-mana .blade-meter-fill{background:linear-gradient(180deg,#ffffff77,transparent 45%,#00000055),var(--blade-mana)}
.blade-root .blade-meter-stamina .blade-meter-fill{background:linear-gradient(180deg,#ffffff77,transparent 45%,#00000055),var(--blade-stamina)}
.blade-root .blade-meter-experience .blade-meter-fill{background:linear-gradient(180deg,#ffffff77,transparent 45%,#00000055),var(--blade-epic)}
.blade-root .blade-meter-value{position:absolute;left:12%;right:12%;top:28px;height:20px;text-align:center;font-size:12px;line-height:20px;letter-spacing:0.04em;color:#fff4d4;text-shadow:0 1px 2px #000;pointer-events:none}
@media(max-width:700px){.blade-root{padding:20px}.blade-root .blade-panel-title{font-size:21px;padding:10px 18px}.blade-root .blade-panel-body{padding:14px}}
"
