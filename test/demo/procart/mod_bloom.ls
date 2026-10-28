// independent keyed choices keep flower form reproducible (S12.1.1v3, PG2).
import rng: lambda.procart.rng
import camera: lambda.scene3d.camera

pub fn profile(k,count) map => {
    size:rng.between(rng.derive(k,"size"),0.72,1.2),
    phase:rng.between(rng.derive(k,"phase"),0.0,2.0*math.pi),
    lean:[rng.between(rng.derive(k,"pitch"),-0.16,0.16),rng.between(rng.derive(k,"roll"),-0.2,0.2)],
    petals:[for (i in 0 to (count-1),let p = rng.at(rng.derive(k,"petals"),i))
        {angle:float(i)*2.0*math.pi/float(count)+rng.between(p,-0.055,0.055),
         length:rng.between(rng.derive(p,"length"),0.8,1.18),width:rng.between(rng.derive(p,"width"),0.72,1.2),
         tilt:rng.between(rng.derive(p,"tilt"),0.3,0.68)}]
}

// a tapered, cupped surface runs from the petal's base along local +X.
pub fn point(u,v,kind="petal") array {
    if (kind == "oak-leaf") {
        let z = v*sin(math.pi*u)*(0.72+0.28*cos(10.0*math.pi*u));
        [u,0.05*u*u+0.03*z*z,z]
    }
    else if (kind == "fan-leaf") {
        let r = u*(1.0-0.16*exp(-v*v/0.0256))
        let x = r*cos(v*1.1),z = r*sin(v*1.1);
        [x,0.03*(x*x+z*z),z]
    }
    else if (kind == "round-petal") {
        let z = v*sqrt(max(0.0,4.0*u*(1.0-u)));
        [u,0.22*u*u+0.22*z*z,z]
    }
    else if (kind == "pad") {
        let a = 0.18+v*(2.0*math.pi-0.36);
        [u*cos(a),u*u*(0.04+0.015*sin(3.0*a)),u*sin(a)]
    }
    else [u,0.12*u*u+0.08*(1.0-v*v)*sin(math.pi*u),v*(0.04+0.96*sin(math.pi*u))]
}

pub fn normal(u,v,kind="petal") array {
    // graph normals remain defined at the rounded petal's collapsed end vertices.
    if (kind == "oak-leaf") camera.unit([-0.1*u,1.0,-0.06*point(u,v,kind)[2]])
    else if (kind == "fan-leaf") (let p = point(u,v,kind),camera.unit([-0.06*p[0],1.0,-0.06*p[2]]))
    else if (kind == "round-petal") camera.unit([-0.44*u,1.0,-0.44*point(u,v,kind)[2]])
    else if (kind == "pad") {
        let a = 0.18+v*(2.0*math.pi-0.36)
        let radial = 2.0*u*(0.04+0.015*sin(3.0*a))
        let tangent = 0.045*u*cos(3.0*a);
        camera.unit([-radial*cos(a)+tangent*sin(a),1.0,-radial*sin(a)-tangent*cos(a)])
    }
    else {
    let du = [1.0,0.24*u+0.08*math.pi*(1.0-v*v)*cos(math.pi*u),v*0.96*math.pi*cos(math.pi*u)]
    let dv = [0.0,-0.16*v*sin(math.pi*u),0.04+0.96*sin(math.pi*u)];
    camera.unit(camera.cross(dv,du))
    }
}

// analytic surface boxes preserve thin leaves when framing a whole specimen.
pub fn local_box(kind) =>
    if (kind == "petal") {center:[0.5,0.1,0.0],radius:[0.5,0.1,1.0]}
    else if (kind == "round-petal") {center:[0.5,0.22,0.0],radius:[0.5,0.22,1.0]}
    else if (kind == "pad") {center:[0.0,0.0275,0.0],radius:[1.0,0.0275,1.0]}
    else if (kind == "oak-leaf") {center:[0.5,0.04,0.0],radius:[0.5,0.04,1.0]}
    else if (kind == "fan-leaf") {center:[0.5,0.03,0.0],radius:[0.5,0.03,1.0]}
    else null
