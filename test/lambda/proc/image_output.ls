import img: lambda.image

pn main() {
    let raster = img.blank({width: 2, height: 1, color: [0.25, 0.5, 1.0, 1.0]})^
    let image = img.of_raster(raster)
    let png = img.write(image, "temp/image_pkg_roundtrip.png", {format: 'png'})^
    let png_copy = img.read("temp/image_pkg_roundtrip.png")^
    let psd = img.write(image, "temp/image_pkg_roundtrip.psd", {format: 'psd'})^
    let psd_copy = img.read("temp/image_pkg_roundtrip.psd")^
    let parsed = input("temp/image_pkg_roundtrip.psd", 'psd')^
    let automatic = input("temp/image_pkg_roundtrip.psd")^
    let adopted = img.adopt(parsed)^
    print([name(parsed), name(parsed[2][0][0]), parsed[2][0][0].pixels[0, 0, 2],
       name(automatic), img.render_frame(adopted)^ == raster])
    print("\n")
    ; [png, psd, img.render_frame(png_copy)^.pixels == raster.pixels,
       img.render_frame(psd_copy)^.pixels == raster.pixels]
}
