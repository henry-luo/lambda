// Media (Lambda_Pkg_Paint.md PT5): pigments for the pigment medium and impasto lighting (PE7, PE8).
// A pigment is {K, S}, Kubelka-Munk absorption and scattering per band (B = 3, linear light) per
// unit pigment amount. A layer's film mixes pigments by concentration, so blue over yellow is
// green; it shows the KM reflectance of the film over the paper.
import watercolor: lambda.procart.watercolor

pub let BANDS = 3

fn unit(v) bool => ((v is int or v is float) and v >= 0.0 and v <= 1.0) ^ { false }
fn decode(c: float) float => if (c <= 0.04045) c / 12.92 else ((c + 0.055) / 1.055) ** 2.4

// pigment(colour) -> {K, S}: the pigment whose opaque film shows the sRGB colour [r, g, b], by the
// Washes inversion procart.watercolor uses (K/S = (1 - R)^2 / 2R at a scattering of 3), applied to
// linear reflectance. A thin film is a glaze of it.
pub fn pigment(color) map^ {
    if (not (color is array and (len(color) == 3 or len(color) == 4) and all([for (c in color) unit(c)])))
        raise error("paint: a pigment colour must be sRGB [r, g, b] in [0, 1]")
    else {
        let p = watercolor.pigment("", [decode(float(color[0])), decode(float(color[1])), decode(float(color[2]))]);
        {K: [for (k in p.K) float(k)], S: [for (s in p.S) float(s)]}
    }
}

fn pigment_ok(p) bool => (p is map and p.K is array and p.S is array and len(p.K) == BANDS and len(p.S) == BANDS and
    all([for (v in [*p.K, *p.S]) (v is int or v is float) and v >= 0.0 and v <= 10000.0])) ^ { false }

// the engine's pigment table: a numeric (count, 2B) array of K then S per pigment.
pub fn table(pigments) any^ {
    if (not (pigments is array and len(pigments) >= 1 and all([for (p in pigments) pigment_ok(p)])))
        raise error("paint: pigments must be {K, S} maps with 3 values each in [0, 10000]")
    else reshape([for (p in pigments, v in [*p.K, *p.S]) float(v)], [len(pigments), 2 * BANDS])
}

// a clean pigment layer's substrate: the paper's linear reflectance per band.
pub fn substrate(paper) array => [decode(float(paper[0])), decode(float(paper[1])), decode(float(paper[2]))]

// lighting(options) -> the engine's lighting for a pigment layer's present (PE7 step 5).
// azimuth: degrees the light comes from, clockwise from the canvas's +x axis with y down (225 is
// the top left); elevation: degrees above the canvas; relief: height per unit of paint; diffuse
// and specular in [0, 1]; shininess in [1, 512]. A flat film keeps its colour.
pub fn lighting(options = {}) map^ {
    let o = if (options == null) ({}) else options
    if (not (o is map)) raise error("paint: lighting options must be a map")
    else {
        let az = float(if (o.azimuth == null) 225.0 else o.azimuth) * math.pi / 180.0
        let el = float(if (o.elevation == null) 50.0 else o.elevation) * math.pi / 180.0
        if (el <= 0.0 or el > math.pi / 2.0) raise error("paint: lighting elevation must be in (0, 90] degrees")
        else ({light: [math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)],
               relief: float(if (o.relief == null) 0.4 else o.relief), diffuse: float(if (o.diffuse == null) 0.6 else o.diffuse),
               specular: float(if (o.specular == null) 0.2 else o.specular), shininess: float(if (o.shininess == null) 24.0 else o.shininess)})
    }
}
