// procgen.field and procgen.adapt: field shaping and its hand-off to other packages
// (Lambda_Pkg_Proc_Gen.md PG4, PG5). Expected values are hand-checkable on a 4x3 ramp.
import rng: lambda.procgen.rng
import noise: lambda.procgen.noise
import field: lambda.procgen.field
import adapt: lambda.procgen.adapt
import img: lambda.image

let f = field.make(4, 3, (x, y) => x * y * 1.0)

"1 shape and bounds";
[field.width(f), field.height(f), field.bounds(f), field.width([])];
f;

"2 rescaling";
field.normalize(f);
field.remap(f, -1.0, 1.0);
field.clamp(f, 1.0, 3.0);
field.terrace(field.normalize(f), 2);
field.normalize(field.make(2, 2, (x, y) => 5.0));
field.transform(f, (v) => v * v);

"3 masks";
field.threshold(f, 2.0)^;
field.radial(5, 5);
field.normalize(f) * 0.5 + field.threshold(f, 4.0)^ * 0.25;

"4 sampling and blur";
[field.sample(f, 1.5, 1.5), field.sample(f, 3.0, 2.0), field.sample(f, -3.0, 9.0), field.sample(f, 2.25, 0.5)];
field.blur(f, 1)^;
field.blur(f, 0)^ == f;

"5 a normalized noise field stays in [0, 1]";
let n = field.normalize(noise.field(noise.make(rng.key(9), {octaves: 3, frequency: 0.07})^, 32, 20))
let b = field.bounds(n);
[b[0], b[1], len(n), len(n[0])];

"6 adapters";
adapt.pixels(field.normalize(f))[2];
adapt.pixels([[0.0, 0.45, 0.5, 0.7, 1.0]], adapt.terrain())[0];
adapt.pixels([[-2.0, 2.0]])[0];
let im = adapt.image(n, adapt.terrain())^
let info = img.info(im);
[info.width, info.height];
adapt.chart_grid(f, "height")
