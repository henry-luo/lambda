// D2.4.1–D2.4.3: a statement's descriptor must retain its native float carrier.
pub pn smooth(kind) array^ {
    var samples:float[]=fill(3,0.0)
    var value=0.0
    var i=0
    while (i<3) {
        value=value*(if (kind=="slow") 0.995 else if (kind=="water") 0.91 else 0.2)+
            0.5*(if (kind=="slow") 0.005 else if (kind=="water") 0.09 else 0.8)
        samples[i]=value
        i=i+1
    }
    return samples
}
pub pn declared(kind:any) float^ {
    var value:float=0.0
    for (i in 0 to 2) { value=value*(if (kind=="water") 0.91 else 0.2)+0.5*(if (kind=="water") 0.09 else 0.8) }
    return value
}
pn main() {
    let dispatch={filter:smooth}
    let step=dispatch.filter
    let slow=step("slow")^
    let water=step("water")^
    let fast=step("fast")^
    print([abs(slow[2]-0.0074625625)<1.0e-12,
        abs(water[2]-0.1232145)<1.0e-12,
        abs(fast[2]-0.496)<1.0e-12,
        abs(declared("water")^-water[2])<1.0e-12])
}
