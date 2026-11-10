// S1.6 / S10.2.1: row batching preserves periodic point values and signed zero.
import noise: lambda.procart.noise
import rng: lambda.procart.rng
fn signs(rows) => [for (row in rows) [for (v in row) if (v==0.0) 1.0/v else v]]
all([for (kind in ['simplex','perlin','value','white','cellular'])
    for (fractal in ['none','fbm','ridged','billow'])
    for (period in [[2.5,3.75],[4.0,2.0,8.0]])
    for (frequency in [0.17,-0.17])
    for (size in [[0,0],[0,3],[3,0],[1,1],[5,4]])
    (let gen=noise.make(rng.key(31),{kind:kind,fractal:fractal,octaves:3,period:period,frequency:frequency})^,
     let rows=noise.field(gen,size[0],size[1]),
     let points=[for (y in 0 to (size[1]-1)) [for (x in 0 to (size[0]-1)) noise.sample(gen,float(x),float(y))]],
     rows==points and signs(rows)==signs(points))])^
