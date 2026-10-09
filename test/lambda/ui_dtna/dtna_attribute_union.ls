// S10.1.1v3: singleton unions constrain values, including imported views.
view dtna_union_specific: <dtna_union_probe kind:'tabs' | 'menu'> { "choice" }
view dtna_union_fallback: <dtna_union_probe> { "fallback" }
[apply(<dtna_union_probe kind:'tabs'>),apply(<dtna_union_probe kind:'menu'>),
    apply(<dtna_union_probe kind:"tabs">),apply(<dtna_union_probe kind:'button'>),
    apply(<dtna_union_probe>)]
