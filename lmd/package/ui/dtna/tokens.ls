import c: lambda.ui.core.component

pub let defaults = {
    primary:"#1677ff", success:"#52c41a", warning:"#faad14", error:"#ff4d4f",
    text:"#1f1f1f", text_secondary:"#666666", border:"#d9d9d9", background:"#ffffff",
    surface:"#fafafa", disabled_background:"#f5f5f5", disabled_text:"#bfbfbf",
    font_family:"-apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif",
    font_size:14, control_height:32, radius:6, spacing:8, line_height:1.5715
}
let digits = "0123456789abcdef"
fn byte(hex, offset) => index_of(digits, lower(slice(hex, offset, offset + 1))) * 16 +
    index_of(digits, lower(slice(hex, offset + 1, offset + 2)))
fn hex_byte(value) => slice(digits, int(value / 16), int(value / 16) + 1) ++ slice(digits, value % 16, value % 16 + 1)
pub fn color(value) bool => if (value is string) len(value) == 7 and starts_with(value, "#") and
    all([for (i in 1 to 6) (contains(digits, lower(slice(value, i, i + 1))) or false)]) else false
// HSV palette schedule follows the published Ant Design light-color algorithm.
// https://github.com/ant-design/ant-design-colors/blob/main/src/generate.ts
fn hsv(seed) {
    let rgb = [for (i in [1,3,5]) byte(seed,i) / 255.0]
    let high = max(rgb)
    let low = min(rgb)
    let delta = high - low
    let hue = if (delta == 0) 0 else if (high == rgb[0]) 60 * ((rgb[1]-rgb[2])/delta)
        else if (high == rgb[1]) 60 * ((rgb[2]-rgb[0])/delta + 2) else 60 * ((rgb[0]-rgb[1])/delta + 4);
    {h:if (hue < 0) hue+360 else hue, s:if (high == 0) 0 else delta/high, v:high}
}
fn rgb_hex(h, s, v) {
    let chroma = v*s
    let x = chroma*(1-abs((h/60)%2-1))
    let channels = [[chroma,x,0],[x,chroma,0],[0,chroma,x],[0,x,chroma],[x,0,chroma],[chroma,0,x]][int(floor(h/60))];
    "#" ++ join([for (channel in channels) hex_byte(int(round((channel+v-chroma)*255)))], "")
}
fn shade(base, step, light) {
    let hue = (round(base.h) + (if (round(base.h) >= 60 and round(base.h) <= 240) -1 else 1) * (if (light) 1 else -1) * 2 * step + 360) % 360
    let saturation = base.s + (if (light) -0.16*step else if (step == 4) 0.16 else 0.05*step)
    let bounded = if (base.s == 0) 0 else max(0.06,min(if (light and step == 5) 0.1 else 1,saturation))
    let brightness = max(0,min(1,base.v + (if (light) 0.05*step else -0.15*step)));
    rgb_hex(hue,round(bounded*100)/100,round(brightness*100)/100)
}
pub fn palette(seed) array^ {
    if (not color(seed)) raise c.fail("palette","seed must be #rrggbb")
    else (let base = hsv(seed), [*[for (step in [5,4,3,2,1]) shade(base,step,true)], lower(seed),
        *[for (step in [1,2,3,4]) shade(base,step,false)]])
}
pub fn resolve(overrides = {}) map^ {
    if (not (overrides is map)) raise c.fail("tokens", "overrides must be a map")
    else (
        let unknown = [for (key, value in overrides where not contains(defaults, key)) string(key)],
        let result = {*:defaults, *:overrides},
        let colors = ["primary", "success", "warning", "error", "text", "text_secondary", "border", "background", "surface", "disabled_background", "disabled_text"],
        if (len(unknown) > 0) raise c.fail("tokens", "unknown token " ++ unknown[0])
        else if (not all([for (key in colors) color(result[key])])) raise c.fail("tokens", "colors must be #rrggbb")
        else if (not all([for (key in ["font_size", "control_height", "radius", "spacing", "line_height"])
            c.finite(result[key]) and (if (key == "radius" or key == "spacing") result[key] >= 0 else result[key] > 0)])) raise c.fail("tokens", "dimensions must be positive numbers")
        else if (not (result.font_family is string) or any([for (char in [";","{","}","\n"]) contains(result.font_family,char)]))
            raise c.fail("tokens","font_family must be a CSS font-family value")
        else (let primary = palette(result.primary)^,
            {*:result, primary_hover:primary[4],primary_active:primary[6],primary_background:primary[0],
                error_background:(palette(result.error)^)[0], success_background:(palette(result.success)^)[0],
                warning_background:(palette(result.warning)^)[0]})
    )
}
pub fn variables(overrides = {}) string^ {
    let values = resolve(overrides)^;
    join([for (key, value in values)
        "--dtna-" ++ replace(string(key), "_", "-") ++ ":" ++ string(value) ++
        (if (contains(["font_size", "control_height", "radius", "spacing"], string(key))) "px" else "") ++ ";"], "")
}

pub fn scoped_variables(overrides = {}) string^ {
    let values = resolve(overrides)^
    let fields = [*[for (key, value in overrides) string(key)],
        *[for (key in ["primary", "error", "success", "warning"] where contains(overrides,symbol(key))) key ++ "_background"],
        *(if (contains(overrides,'primary')) ["primary_hover","primary_active"] else [])];
    join([for (key in fields) "--dtna-" ++ replace(key,"_","-") ++ ":" ++ string(values[key]) ++
        (if (contains(["font_size","control_height","radius","spacing"],key)) "px" else "") ++ ";"], "")
}
