// Image-op invariants, referenced against scikit-image semantics.  Exact-valued
// invariants use flatten(x) == flatten(y); the morphology duality (which differs
// only at float-ULP level) uses sum(abs(a - b)) == 0 over the ubyte-rounded
// result; orderings use all(flatten(a) <= flatten(b)).

let im  = reshape([10, 40, 70, 100, 130, 160, 190, 220, 25, 55, 85, 115, 145, 175, 205, 235], [4, 4])
let imf = as_float(im)                                  // same image as float [0,1]
let imp = [[0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 9, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]]
let cst = reshape([5, 5, 5, 5, 5, 5, 5, 5, 5], [3, 3])

// dtype conversions (skimage img_as_float / img_as_ubyte)
"ubyte->float->ubyte identity:"; (flatten(as_ubyte(as_float([0, 64, 128, 192, 255]))) == flatten([0, 64, 128, 192, 255]))
"as_float within [0,1]:";        all(as_float([0, 128, 255]) <= [1.0, 1.0, 1.0])

// geometric involutions (np.flip / np.rot90 identities)
"flip vert involution:";   (flatten(flip(flip(im, 0), 0)) == flatten(im))
"flip horiz involution:";  (flatten(flip(flip(im, 1), 1)) == flatten(im))
"rot90 x4 == identity:";   (flatten(rot90(rot90(rot90(rot90(im, 1), 1), 1), 1)) == flatten(im))
"rot90 1 then 3 == id:";   (flatten(rot90(rot90(im, 1), 3)) == flatten(im))
"rot90 x2 == flip both:";  (flatten(rot90(im, 2)) == flatten(flip(flip(im, 0), 1)))
"rotate 0 == identity:";   (flatten(rotate(im, 0.0)) == flatten(im))

// morphology (skimage.morphology)
"erosion <= image:";       all(flatten(erode(im, 3)) <= flatten(im))
"image <= dilation:";      all(flatten(im) <= flatten(dilate(im, 3)))
"opening <= image:";       all(flatten(dilate(erode(im, 3), 3)) <= flatten(im))
"closing >= image:";       all(flatten(im) <= flatten(erode(dilate(im, 3), 3)))
"erode/dilate duality:";   (sum(abs(flatten(as_ubyte(erode(imf, 3))) - flatten(as_ubyte(invert(dilate(invert(imf), 3)))))) == 0)
"median removes impulse:"; (flatten(median_filter(imp, 3)) == flatten(imp * 0))

// linear filters
"convolve identity kernel:"; (flatten(convolve(im, [[0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 0.0]])) == flatten(im))
"blur preserves constant:";  (flatten(blur(cst, 3)) == flatten(cst))

// histogram / threshold / segmentation
"histogram sums to N pixels:"; (sum(histogram(im, 256)) == 16)
"otsu within [min,max]:";      ((min(im) <= otsu(im)) and (otsu(im) <= max(im)))
let mask = [[1, 1, 0, 1], [1, 0, 0, 1], [0, 0, 1, 1]]
"label areas sum to N:";   (sum(histogram(label(mask), 3)) == 12)
"label component count:";  max(label(mask))             // 2

// crop / resize identities
"full crop == identity:";  (flatten(crop(im, 0 to 3, 0 to 3)) == flatten(im))
"resize same-size == id:"; (flatten(resize(im, 4, 4)) == flatten(im))
