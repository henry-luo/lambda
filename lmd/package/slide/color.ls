import c: .common
import ease: .easing
import colors: lambda.color

pub fn parse(value,path) array^ => colors.parse(value,path) ^ {
    raise c.fail(path,"interpolated colors require hex, rgb/rgba(), transparent or normalized RGBA[4]")
}

// sRGB channels with premultiplied alpha; transparent RGB has no contribution.
pub fn sample(a, b, t) {
    let alpha = ease.lerp(a[3], b[3], t);
    [*[for (i in 0 to 2) if (alpha == 0.0) 0.0 else ease.lerp(a[i] * a[3], b[i] * b[3], t) / alpha], alpha]
}

pub fn css(rgba) => "rgba(" ++ c.fmt(rgba[0] * 255.0) ++ "," ++ c.fmt(rgba[1] * 255.0) ++ "," ++
    c.fmt(rgba[2] * 255.0) ++ "," ++ c.fmt(rgba[3]) ++ ")"
