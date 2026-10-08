// S12.1.1v2/D7.4.4: clips are pure data; playback belongs to the declared document host.
import transforms: .transform

pub fn clip(label, tracks, options = {}) element => <'animation-clip' id: label, duration: -1.0, *: options, *tracks>
pub fn track(path, times, values, kind = 'number', options = {}) element =>
    <'keyframe-track' path: path, type: kind, times: times, values: values, *: options>

pub fn valid_track(n) bool {
    let kind = n.type
    let times = n.times, values = n.values
    let components = if (kind == 'quaternion') 4 else if (kind == 'color') 3
        else if (kind == 'vector' and times is array and values is array and len(times) > 0) len(values) / len(times) else 1
    let interpolation = if (n.interpolation == null) (if (kind == 'bool' or kind == 'string') 'discrete' else 'linear') else n.interpolation
    contains(['number', 'vector', 'color', 'quaternion', 'bool', 'string'], kind) and
        (n.path is string) and len(n.path) > 2 and
        (times is array) and len(times) > 0 and len(times) <= 1048576 and
        all([for (t in times) transforms.numeric(t)]) and
        (len(times) == 1 or all([for (i in 1 to (len(times) - 1)) times[i] >= times[i - 1]])) and
        (values is array) and components >= 1 and components <= 16 and floor(components) == components and len(values) == len(times) * components and
        all([for (v in values) if (kind == 'bool') v is bool else if (kind == 'string') (v is string) and len(v) < 256 else transforms.numeric(v)]) and
        (kind != 'quaternion' or all([for (i in 0 to (len(times) - 1)) sum([for (j in 0 to 3) values[i * 4 + j] ** 2]) > 0])) and
        (if (kind == 'quaternion') contains(['discrete', 'linear', 'hermite'], interpolation)
         else if (kind == 'bool' or kind == 'string') interpolation == 'discrete'
         else contains(['discrete', 'linear', 'smooth', 'bezier', 'hermite'], interpolation)) and
        (interpolation != 'hermite' or (n["in-tangents"] != null and n["out-tangents"] != null)) and
        all([for (key in ["in-tangents", "out-tangents"] where n[key] != null)
            (n[key] is array) and len(n[key]) == len(values) * (if (interpolation == 'hermite') 1 else 2) and all([for (v in n[key]) transforms.numeric(v)])])
}
