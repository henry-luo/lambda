// PG14 / PG16: skin, feather and scale marks share the admitted surface sampler.
import surface: lambda.procart.surface
import atlas: lambda.procart.butterfly_atlas
let kinds=["spots","feathers","scales"]
let patterns=[for (kind in kinds) surface.prepare(<pattern id:"skin",generator:kind,version:1,parameters:{frequency:8.0,width:0.08}>,2026)^]
let samples=[for (p in patterns) [for (i in 0 to 15) surface.sample(p,[float(i)/16.0,0.37],true)]];
[len(unique(samples))==3,all([for (sample in samples) all([for (rgb in sample) all([for (c in rgb) c>=0.0 and c<=1.0])])]),
 all([for (i,kind in kinds) surface.prepare(<pattern id:"skin",generator:kind,version:1,parameters:{frequency:8.0,width:0.08}>,2026)^==patterns[i]]),
 surface.prepare(<pattern id:"skin",generator:"spots",version:1>,1)^!=surface.prepare(<pattern id:"skin",generator:"spots",version:1>,2)^,
 atlas.wing_svg({style:"stripes"})^!=atlas.wing_svg({style:"spots"})^,
 atlas.wing_svg({style:"spots",seed:2})^!=atlas.wing_svg({style:"spots",seed:3})^]
