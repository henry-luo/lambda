// MyPaint brush files (.myb, JSON version 3) as lambda.paint brushes (Lambda_Pkg_Paint.md PT6).
// Pass the parsed JSON, e.g. input("pencil.myb", 'json').
import brushes: lambda.paint.brush
import dynamics: lambda.paint.dynamics

fn numeric(v) bool => v is int or v is float
// cross-module predicates are checked at run time, so a failed check counts as "no".
fn known(name) bool => brushes.supported(name) ^ { false }
fn color_setting(name) bool => ((string(name) ^ { "" }) in dynamics.COLOR_SETTINGS) ^ { false }

// inputs with fewer than two points carry no response in libmypaint.
fn curves(inputs) map => if (not (inputs is map)) ({})
    else map([for (key, points in inputs, v in (if ((points is array and len(points) >= 2) ^ { false })
        ([string(key) ^ { "" }, points]) else ([]))) v])
fn setting(spec) map => ({base: spec.base_value, inputs: curves(spec.inputs)})

fn default_of(name) => (dynamics.UNRENDERED[string(name) ^ { "" }]) ^ { null }
// a setting this engine does not render matters only if the file moves it off libmypaint's default.
fn changed(name, spec) bool => (not (spec is map) or not numeric(spec.base_value) or
    spec.base_value != default_of(name) or len(curves(spec.inputs)) > 0) ^ { true }

// from_myb(data, options) -> {brush, ignored}. `ignored` names the settings in the file that
// this engine does not render yet but that the file changes; the stroke colour replaces
// color_h, color_s and color_v. Options pass through to brush(): blend, seed.
pub fn from_myb(data, options = {}) map^ {
    if (not (data is map) or not (data.settings is map)) raise error("paint: a .myb brush is a JSON map with settings")
    else if (data.version != null and data.version != 3) raise error("paint: only version 3 .myb brushes are supported")
    else if (not all([for (name, spec in data.settings) ((not known(name)) or
        (spec is map and numeric(spec.base_value))) ^ { false }])) raise error("paint: a .myb setting needs a numeric base_value")
    else {
        let kept = [for (name, spec in data.settings, v in (if (known(name) ^ { false })
            ([string(name) ^ { "" }, setting(spec)]) else ([]))) v]
        let ignored = [for (name, spec in data.settings where ((not known(name) and not color_setting(name) and
            changed(name, spec)) ^ { false })) string(name) ^ { "" }]
        let b = brushes.brush({kind: 'mypaint', settings: map(kept), *: options})^;
        {brush: b, ignored: ignored}
    }
}
