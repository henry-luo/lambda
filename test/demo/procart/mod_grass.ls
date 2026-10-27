// curved ribbon blades share one mesh; keyed clumps produce reusable patch data (PG11).
import rng: lambda.procgen.rng
import solid: .mod_plant_solids
import s: lambda.scene3d
import camera: lambda.scene3d.camera
import composition: .mod_composition

pub fn validate(p) bool^ {
    if (not composition.bounded(p.clumps,1,48,true) or not composition.bounded(p.blades,3,16,true) or
        not composition.bounded(p.radius,0.1,3.0) or not composition.bounded(p.height,0.1,1.2) or
        not composition.bounded(p.width,0.01,0.15) or not composition.bounded(p.bend,0.05,1.0) or
        not (p.colors is array) or len(p.colors) < 1 or len(p.colors) > 4 or not all([for (c in p.colors) c is string]))
        raise error("procart grass: invalid clump, blade, dimension or palette settings") else true
}

pub fn geometry() element => s.geometry(
    [for (j in 0 to 4,let u = float(j)/4.0) for (side in [-0.5,0.5]) *[side*(1.0-u),u,u*u]],
    {id:"grass-blade",normals:[for (j in 0 to 4) for (side in 0 to 1) *camera.unit([0.0,2.0*float(j)/4.0,-1.0])],
     indices:[for (j in 0 to 3,let a = j*2) *[a,a+2,a+1,a+1,a+2,a+3]]})

pub fn generate(k,p) array^ {
    let checked = validate(p)^;
    [for (clump in 0 to (p.clumps-1),let ck = rng.at(k,clump),let a = rng.between(ck,0.0,2.0*math.pi),
        let r = sqrt(rng.unit(rng.derive(ck,"radius")))*p.radius,
        let base = [r*cos(a),0.0,r*sin(a)])
        for (blade in 0 to (p.blades-1),let bk = rng.at(rng.derive(ck,"blades"),blade),
            let turn = float(blade)*2.4+rng.between(bk,-0.4,0.4),let height = p.height*rng.between(rng.at(bk,1),0.65,1.2))
            solid.oval(camera.add(base,[0.045*cos(turn),0.0,0.045*sin(turn)]),
                [p.width*rng.between(rng.at(bk,2),0.8,1.2),height,p.bend*height],
                rng.pick(rng.derive(bk,"color"),p.colors),"grass",[0.0,turn,0.0],"grass-blade")]
}
