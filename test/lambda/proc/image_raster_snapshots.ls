import img: lambda.image

pn main() {
    var r = img.blank({width: 2, height: 1, color: [1.0, 0.5, 0.0, 1.0]})^
    let captured = r
    let red = r.r
    var samples = r.pixels
    samples[0, 0, 0] = u8(0)
    var names = r.channels
    names[0] = 'changed'
    r = img.fill(r, [0.0, 0.0, 1.0, 1.0])^
    let before = r
    let patch = img.blank({width: 1, height: 1, color: [0.0, 1.0, 0.0, 1.0]})^
    img.write_region(r, [1, 0, 2, 1], patch.pixels)^
    img.write_region(samples: patch.pixels, rect: [0, 0, 1, 1], raster: r, options: {})^
    print([r.g[0, 0], before.b[0, 0]])
    print("\n")
    let held = r
    var failed = false
    img.write_region(r, [-1, 0, 1, 1], patch.pixels) ^ { failed = true }
    print([failed, held == r])
    print("\n")
    print([before.b[0, 1], r.b[0, 1], r.g[0, 1]])
    print("\n")
    ; [samples[0, 0, 0], captured.r[0, 0], captured.channels[0], names[0], red[0, 0], r.r[0, 0]]
}
