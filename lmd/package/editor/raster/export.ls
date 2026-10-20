// S9.1.2 / S12.1.1v3: export derives pixels without changing the authored snapshot.
import img: lambda.image
import model: .model
import colors: lambda.slide.color

pub fn codec(kind) any^ {
    if (not (kind is string or kind is symbol) or not (string(kind) in ["png","jpeg","jpg"]))
        raise error("Raster: flat export supports PNG and JPEG")
    else if (string(kind) == "jpg") "jpeg" else string(kind)
}
pub fn destination(path,kind) any^ {
    let format = codec(kind)^
    let extension = if (format == "jpeg") ".jpg" else ".png"
    let old = [for (suffix in [".png",".jpeg",".jpg"] where ends_with(lower(path),suffix)) suffix][0];
    if (old == null) path ++ extension else slice(path,0,len(path)-len(old)) ++ extension
}
// shared by checked export settings and the dialog's aspect-locked draft.
pub fn proportional_extent(image,extent,from_width) => max(1,int(round(extent*
    (if (from_width) image.height/image.width else image.width/image.height))))
pub fn settings(image,kind,options={}) any^ {
    let format = codec(kind)^
    let config = img.checked_options(options,["width","height","alpha","background","quality"])^
    let sizes = [for (n in [config.width,config.height] where n != null) n]
    if (not all([for (n in sizes) n is int and n>=1 and n<=16000000]))
        raise error("Raster: export dimensions must be positive integers")
    else {
        let width = model.value(config.width,if (config.height == null) image.width else proportional_extent(image,config.height,false))
        let height = model.value(config.height,if (config.width == null) image.height else proportional_extent(image,config.width,true))
        let alpha = model.value(config.alpha,if (format == "jpeg") "flatten" else "preserve")
        let quality = model.value(config.quality,90)
        if (width>16000000/height) raise error("Raster: export exceeds the 16-million-pixel limit")
        else if (not (alpha in ["preserve","flatten"]) or (format == "jpeg" and alpha != "flatten"))
            raise error("Raster: JPEG requires flattened transparency")
        else if (format == "png" and config.quality != null)
            raise error("Raster: quality is a JPEG export option")
        else if (not (quality is int) or quality<0 or quality>100)
            raise error("Raster: JPEG quality must be an integer from 0 to 100")
        else if (alpha == "flatten" and config.background == null)
            raise error("Raster: choose an opaque export background")
        else if (alpha == "preserve" and config.background != null)
            raise error("Raster: a background requires flattened transparency")
        else {
            let background = if (alpha == "flatten") colors.parse(config.background,"export.background")^ else null;
            if (background != null and background[3] != 1) raise error("Raster: export background must be opaque")
            else {format:format,width:width,height:height,alpha:alpha,background:background,quality:quality}
        }
    }
}
pub fn prepare(image,kind,options={}) any^ {
    let config = settings(image,kind,options)^
    let source = img.render_frame(image)^
    let sized = if (source.width == config.width and source.height == config.height) source
        else img.resize(source,config.width,config.height)^
    let pixels = if (config.alpha == "preserve") sized else (
        let background = img.blank({width:config.width,height:config.height,color:config.background})^,
        img.render_frame(img.document(img.stack([img.layer(sized),img.layer(background)]),
            {width:config.width,height:config.height}))^)
    // retain source metadata/assets for the encoder's omission report, not its output pixels.
    let document = img.with_children(img.with_properties(image,{width:config.width,height:config.height})^,
        [*[for (child in content(image) where name(child) in ['assets','metadata']) child],img.stack([img.layer(pixels)])])^;
    {image:document,settings:config}
}
