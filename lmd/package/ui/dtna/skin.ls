// derived Ant palettes stay with dtna while neutral token validation remains in core.
import c: lambda.ui.core.component
import shared: lambda.ui.core.theme
import engine: .tokens

fn engine_values(values, extra) map => map([for (key,value in values where ((not contains(extra,c.text(key))) ^ { false })) (key,value)])
pub fn skin_resolve(defaults, overrides, lengths, zero_lengths, extra, family) map^ {
    let values = shared.resolve(defaults,overrides,lengths,zero_lengths,family)^
    let derived = engine.resolve(engine_values(values,extra)) ^ {raise error(replace(^.message,"dtna:",family ++ ":"))};
    {*:derived,*:values}
}
pub fn skin_variables(defaults, overrides, lengths, zero_lengths, extra, family, scoped = false) string^ {
    let values = skin_resolve(defaults,overrides,lengths,zero_lengths,extra,family)^
    let aliases = if (scoped) engine.scoped_variables(engine_values(overrides,extra))^
        else engine.variables(map([for (key,value in defaults where ((not contains(extra,c.text(key))) ^ { false })) (key,values[key])]))^
    let decoration = map([for (key,value in (if (scoped) overrides else values) where ((contains(extra,c.text(key))) ^ { false })) (key,value)]);
    replace(aliases,"--dtna-","--" ++ family ++ "-") ++ shared.variables(decoration,lengths,family) ++ aliases
}
