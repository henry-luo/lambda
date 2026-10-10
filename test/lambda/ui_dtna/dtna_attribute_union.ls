// S10.1.1v3: singleton unions constrain values, including imported views.
view dtna_union_specific: <dtna_union_probe kind:'tabs' | 'menu'> { "choice" }
view dtna_union_fallback: <dtna_union_probe> { "fallback" }
[apply(<dtna_union_probe kind:'tabs'>),apply(<dtna_union_probe kind:'menu'>),
    apply(<dtna_union_probe kind:"tabs">),apply(<dtna_union_probe kind:'button'>),
    apply(<dtna_union_probe>)]

// S10.1.1v3: root alternatives retain their own tags and attribute predicates.
view dtna_root_union: <dtna_probe.button variant:'dashed'> | <dtna_probe.card> { "named tag" }
[apply(<dtna_probe.button variant:'dashed'>),apply(<dtna_probe.card>),
    apply(<dtna_probe.button variant:'primary'>) == <dtna_probe.button variant:'primary'>,
    apply(<button variant:'dashed'>) == <button variant:'dashed'>,
    apply(<other_probe.card>) == <other_probe.card>]
