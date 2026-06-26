// Histogram / Otsu, connected-component labelling, and resize / warp
// (Typed Array 4, §1.4-1.6).

// --- histogram: integer images use bincount (one bin per value) ---
"histogram:";    histogram([0, 1, 1, 2, 2, 2], 3)       // [1, 2, 3]
"histogram 2D:"; histogram([[0, 1], [1, 2]], 3)         // [1, 2, 1]

// --- Otsu threshold on a bimodal image, then segment with it ---
"otsu:";          otsu([[50, 50], [200, 200]])          // plateau centre -> 124
"otsu segments:"; ([[50, 50], [200, 200]] > otsu([[50, 50], [200, 200]]))

// --- connected components (4-connectivity); count = max, areas = histogram ---
let mask = [[1, 1, 0, 1], [1, 0, 0, 1], [0, 0, 1, 1]]
"label:";       label(mask)                             // 2 components
"components:";  max(label(mask))                        // 2
"areas:";       histogram(label(mask), 3)               // [bg 5, c1 3, c2 4]

// --- resize (bilinear, half-pixel centres) ---
"resize same:"; resize([[1, 2], [3, 4]], 2, 2)          // identity
"resize 4->2:"; resize([[1.0, 2.0, 3.0, 4.0], [5.0, 6.0, 7.0, 8.0], [9.0, 10.0, 11.0, 12.0], [13.0, 14.0, 15.0, 16.0]], 2, 2)

// --- rotate (bilinear about centre); 90 deg on an odd image equals rot90 ---
"rotate 0:";  rotate([[1, 2], [3, 4]], 0.0)             // identity
"rotate 90:"; rotate([[1, 2, 3], [4, 5, 6], [7, 8, 9]], 90.0)

// --- affine warp: identity is a no-op; [[1,0,-1],[0,1,0]] shifts right by 1 ---
"affine identity:"; affine_warp([[1, 2, 3], [4, 5, 6]], [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0]])
"affine shift:";    affine_warp([[1, 2, 3], [4, 5, 6]], [[1.0, 0.0, -1.0], [0.0, 1.0, 0.0]])
