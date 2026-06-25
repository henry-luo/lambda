// Point, colour, and geometric image ops (Typed Array 4, Scope 1.2).  Ops preserve
// element type (int images use a [0,255] white point, float images use [0,1]) and
// work on 2-D (H,W) and 3-D (H,W,C) arrays.

// --- point / colour ops (shape + type preserved) ---
"invert int:";      invert([[0, 255], [128, 64]])            // 255 - v
"invert float:";    invert([[0.0, 1.0], [0.25, 0.75]])       // 1 - v
"gamma 2.0:";       gamma([[0.0, 1.0], [0.25, 1.0]], 2.0)    // 0.25^2 = 0.0625
"threshold float:"; threshold([[0.2, 0.6], [0.5, 0.8]], 0.5) // v >= 0.5 -> 1
"threshold int:";   threshold([[100, 200], [128, 50]], 128)  // v >= 128 -> 255

// --- colour -> grayscale (Rec.601 luma, (H,W,C) -> (H,W)) ---
"grayscale:"; grayscale([[[255, 0, 0], [0, 255, 0]], [[0, 0, 255], [255, 255, 255]]])
// red->76, green->150, blue->29, white->255

// --- geometric ops ---
"flip vertical:";   flip([[1, 2, 3], [4, 5, 6]], 0)          // reverse rows
"flip horizontal:"; flip([[1, 2, 3], [4, 5, 6]], 1)          // reverse cols
"rot90 once (CCW):"; rot90([[1, 2, 3], [4, 5, 6]], 1)        // (2,3) -> (3,2)
"rot90 twice (180):"; rot90([[1, 2, 3], [4, 5, 6]], 2)
"crop rows 0..1 cols 1..2:"; crop([[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12]], 0 to 1, 1 to 2)

// --- 3-D (H,W,C): channels carried through geometric ops ---
"flip h (RGB):"; flip([[[1, 2], [3, 4]], [[5, 6], [7, 8]]], 1)
"crop (RGB):";   crop([[[1, 2], [3, 4], [5, 6]], [[7, 8], [9, 10], [11, 12]]], 0 to 1, 1 to 2)
