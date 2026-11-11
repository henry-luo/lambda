let zero=chr(0);
let middle="a" ++ zero ++ "b";
let tail=zero ++ "z";
let source=map([zero,11,middle,22,tail,33,7,77,"safe",44]);
let spread={*:source};
let nested={*:spread};
let mixed={prefix:1,*:source};
let values=[for(k,v in source) v];
let source_lookups=[source[zero],source[middle],source[tail],source[7],source["safe"]];
let spread_lookups=[spread[zero],spread[middle],spread[tail],spread["__v3"],spread["safe"]];
let nested_lookups=[nested[zero],nested[middle],nested[tail],nested["__v3"],nested["safe"]];
let cats={'{':1,*:map([zero,12])};
{source:source_lookups,iteration:values,spread:spread_lookups,nested:nested_lookups,
 mixed:[mixed.prefix,mixed[zero],mixed[middle],mixed[tail]],override:cats[zero],
 expected:source_lookups == [11,22,33,77,44] and values == [11,22,33,77,44] and
 spread_lookups == [11,22,33,77,44] and nested_lookups == [11,22,33,77,44] and cats[zero] == 12}
