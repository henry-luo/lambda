// Image I/O bridge (Typed Array 4, Scope 1.1): load / save PNG + the ubyte<->float
// conventions.  Round-trips a known RGBA image through PNG and checks each pixel
// survives, then verifies as_float/as_ubyte are inverse on 8-bit data.

// a 2x3 RGBA float image in [0,1]: row 0 = red/green/blue, row 1 = white/black/gray
let im = [
  [[1.0, 0.0, 0.0, 1.0], [0.0, 1.0, 0.0, 1.0], [0.0, 0.0, 1.0, 1.0]],
  [[1.0, 1.0, 1.0, 1.0], [0.0, 0.0, 0.0, 1.0], [0.5, 0.5, 0.5, 1.0]]
]

"save ok:"; save(im, "./temp/io_bridge_test.png")
let back = load("./temp/io_bridge_test.png")
"shape (H,W,4):"; shape(back)

// pixels survive the round-trip (float [0,1] -> ubyte [0,255])
"red:";   back[0][0]
"green:"; back[0][1]
"blue:";  back[0][2]
"white:"; back[1][0]
"black:"; back[1][1]
"gray (0.5*255 -> 128):"; back[1][2]

// as_float then as_ubyte is identity on 8-bit data
"ubyte(float(x)) == x:"; as_ubyte(as_float(back))[1][2]
