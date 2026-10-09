import models: .model

// normalize the supported Style Specification v8 subset into the one native element model.
pub fn from_style(style, options = {}) {
    if (not (style is map) or style.version != 8 or not (style.sources is map) or not (style.layers is array))
        error("map: style requires version 8, a sources map and a layers array")
    else if (style.sprite != null or style.glyphs != null or style.terrain != null or style.sky != null)
        error("map: sprites, glyphs, terrain and sky are not yet implemented")
    else if (any([for (k,v in style)
        not contains(["version","name","metadata","center","zoom","bearing","pitch","projection","sources","layers"],string(k))]))
        error("map: unsupported style root property")
    else if (any([for (id,source in style.sources) not (source is map)]) or any(style.layers |> not (~ is map)))
        error("map: style sources and layers must be maps")
    else models.normalize(<geomap
        *:map([for (key in ["center","zoom","bearing","pitch","projection"] where style[key] != null) (key,style[key])]),
        *:options,
        for (id,source in style.sources) <source *:source,id:string(id)>
        for (layer in style.layers) <layer *:layer>
    >)
}
