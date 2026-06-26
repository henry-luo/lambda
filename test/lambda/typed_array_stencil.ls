// Image stencil engine (Typed Array 4, Scope 1.3): convolution, morphology, rank
// filters, and pooling all reduce to the one N-D windowed primitive.

"=== convolve: identity kernel returns the input ==="
let img = [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0], [7.0, 8.0, 9.0]]
convolve(img, [[0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 0.0]])

"=== convolve: Sobel-x detects a vertical edge ==="
let edge = [[0.0, 0.0, 9.0, 9.0], [0.0, 0.0, 9.0, 9.0], [0.0, 0.0, 9.0, 9.0]]
convolve(edge, [[-1.0, 0.0, 1.0], [-2.0, 0.0, 2.0], [-1.0, 0.0, 1.0]])

"=== blur: box mean spreads an impulse ==="
let imp = [[0.0, 0.0, 0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 0.0, 0.0], [0.0, 0.0, 9.0, 0.0, 0.0], [0.0, 0.0, 0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 0.0, 0.0]]
blur(imp, 3)

"=== dilate (max) grows it; erode (min) clears it ==="
dilate(imp, 3)
erode(imp, 3)

"=== median filter removes the single-pixel impulse ==="
median_filter(imp, 3)

"=== maxpool / avgpool downsample (non-overlapping 2x2) ==="
let g = [[1.0, 2.0, 3.0, 4.0], [5.0, 6.0, 7.0, 8.0], [9.0, 10.0, 11.0, 12.0], [13.0, 14.0, 15.0, 16.0]]
maxpool(g, 2)
avgpool(g, 2)

"=== 3-D (H,W,C) image: filter applies per channel ==="
blur([[[10.0, 20.0], [10.0, 20.0]], [[10.0, 20.0], [10.0, 20.0]]], 3)
