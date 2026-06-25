# Proposal: Lambda Typed Array 4 — Image Processing, SIMD Auto-Vectorization, and Mutable Views

> **Three scopes.**
> **(1) Image processing** — an `skimage`-style toolkit built *on top of* `ArrayNum`, where an image is simply a 3-D typed array (`H × W × C`). Like scikit-image to NumPy: a library over the array type, not a new type.
> **(2) SIMD auto-vectorization** — restructure the `ArrayNum` numeric kernels so the compiler auto-vectorizes them (no new dependency, no hand intrinsics). A portable SIMD *library* (e.g. Google Highway) is explicitly **deferred to a future proposal**; this one is auto-vectorization only.
> **(3) Mutable views in procedural functions** — allow an `ArrayNum` view to be written *through* (in-place sub-region updates), but **only inside procedural (`pn`) functions**, so the pure-functional core keeps its value semantics. This is what lets image operations run in-place (`img[mask] = 0`, write a patch into a crop) without copying a whole frame per step.

> **Status:** design proposal. Builds on the typed-array work landed in Typed Array 1/2 (`ArrayNum`: 14 element kinds, N-D shape/strides, broadcasting, views, masks, axis reductions — baseline 3224/3224). The two scopes reinforce each other: image processing is the workload that most rewards SIMD (contiguous `uint8`/`float` pixel kernels), and the SIMD work makes the image kernels fast.

---

## 0. TL;DR

- **An image is an `ArrayNum`.** No new type — a grayscale image is a 2-D `ArrayNum (H, W)`, a colour image is 3-D `(H, W, C)`, dtype `ELEM_UINT8` for storage or `ELEM_FLOAT`/`ELEM_FLOAT32` for processing. This is exactly skimage's relationship to `ndarray`, and it means **most point/colour/geometric/statistical operations already work** via the typed-array primitives (threshold = `img > t` → mask; brightness = vectorized arithmetic; channel reductions = axis ops; crop = slicing; rotate90 = transpose).
- **The one genuinely new primitive is a generic N-D stencil (windowed) operation** — slide a kernel/window over the array and apply a reducer at each position. Convolution (blur/sharpen/Sobel), morphology (erode/dilate), rank filters (median), and pooling all reduce to it. It's the image-processing analogue of "axis reductions were the building block for the DataFrame."
- **SIMD: the kernels already exist but can't vectorize.** `lambda-vector.cpp` has contiguous per-type fast paths (`ELEM_INT64`/`INT`/`FLOAT32`, [lines 647‑725](../lambda/lambda-vector.cpp)) — but each has a `switch(op)` *inside* the loop body (a branch per element), no `restrict` (aliasing blocks the vectorizer), mixes vectorizable ops (`+ - *`) with non-vectorizable (`%`, `pow`), and covers only 3 of 14 element types. The auto-vectorization work is to **restructure these into straight-line, `restrict`-qualified, per-op, per-type loops**, expand type coverage (especially `uint8`/`float` for images), give the reduction walkers contiguous fast paths, and turn on `-O3 -march=native` + `#pragma omp simd`. No dependency; fits the C+ convention.
- **Mutable views: the machinery is already in place; only a guard is in the way.** Views (`is_view`, `base: Container*`, pinned base, compactor-skip) all landed in Typed Array 2 — they're just **read-only** (write rejected in `array_num_set_item` and the MIR index-assign fast path). Scope 3 adds an `is_mutable_view` distinction and relaxes that guard **only for views created inside procedural functions over a mutable base**, plus slice/region assignment (`img[r0 to r1, c0 to c1] = patch`) and masked assignment (`img[mask] = v` — the write counterpart to the mask *indexing* built this session). Pure `fn` code can't issue an assignment at all, so the functional world stays value-semantic by construction.

---

# Scope 1 — Image Processing on `ArrayNum`

## 1.1 Principle: reuse the array type, add a library

scikit-image's design lesson is that **you don't need an image type** — an image is an `ndarray`, and the library is a set of functions over it. Lambda is positioned identically after Typed Array 2: it has N-D arrays, broadcasting, masks, axis reductions, reshape/transpose, and (via Radiant) raster image machinery. So Scope 1 adds a **library**, not a type.

**Conventions** (matching skimage so the mental model transfers):

- **Shape:** grayscale `(H, W)`; colour `(H, W, C)` with the **channel as the last axis** (HWC — skimage/Pillow/OpenCV convention), `C ∈ {3 (RGB), 4 (RGBA)}`. Row-major, so a row is contiguous — the SIMD-friendly axis.
- **dtype:** `ELEM_UINT8` for storage (`[0, 255]`); `ELEM_FLOAT`/`ELEM_FLOAT32` for processing (`[0, 1]`). Conversion helpers `image.as_float(img)` / `image.as_ubyte(img)` (skimage's `img_as_float`/`img_as_ubyte`) handle the scale + clip.
- **No new container.** Functions take and return `ArrayNum`; an "image" is a documented shape/dtype convention, validated at the boundary.

## 1.2 What already works (free, via typed-array primitives)

| skimage-style operation | Lambda today |
|---|---|
| **Threshold** `img > t` → binary mask | vectorized comparison → `ELEM_BOOL` (built) |
| **Brightness / contrast** `img*a + b` | vectorized arithmetic + broadcast |
| **Gamma** `img ^ g`, **invert** `255 - img` | vectorized pow / sub |
| **Clip** to `[lo, hi]` | `clip(img, lo, hi)` (built) |
| **Crop** `img[r0 to r1, c0 to c1]` | N-D slicing / multi-dim index |
| **Channel split/merge** | leading/last-axis slicing + `stack` |
| **rgb → gray** (weighted channel sum) | axis reduction over the channel axis |
| **rot90 / transpose / flip-diag** | `transpose(img, perm)` (built) |
| **min / max / mean / sum** (per axis or whole) | axis reductions (built) |
| **Masked selection** `img[mask]` | boolean mask indexing (built) |

So a meaningful slice of image processing is *already expressible* — `image.as_float(img) > 0.5` is a threshold; `sum(img, axis: 2) / 3` is a (naive) grayscale. Scope 1 packages these as named functions and adds the parts that need new machinery.

## 1.3 The new core primitive: a generic N-D stencil engine

Almost every "real" image operation is a **windowed neighbourhood computation**: at each output pixel, gather a `Kh × Kw` window of the input and reduce it. One engine covers the whole family:

```c
// slide `window` (shape Kh×Kw[×C]) over `in`; at each position apply `op` over the
// window, writing one output element. Covers convolution, morphology, rank filters, pooling.
typedef enum { STENCIL_DOT, STENCIL_MIN, STENCIL_MAX, STENCIL_MEDIAN, STENCIL_MEAN } StencilOp;
typedef enum { BORDER_CONSTANT, BORDER_EDGE, BORDER_REFLECT, BORDER_WRAP } BorderMode;

Item array_num_stencil(ArrayNum* in, ArrayNum* kernel, StencilOp op,
                       BorderMode border, double border_value, int64_t stride_h, int64_t stride_w);
```

Built on the existing N-D stride machinery (`get_shape_strides`, `read_arr_elem_as_double`, `alloc_ndim_arraynum`). Each operation is then a thin call:

| Operation | Stencil call |
|---|---|
| Box / Gaussian **blur** | `STENCIL_DOT` with a normalized / Gaussian kernel (separable: two 1-D passes) |
| **Sobel / Scharr / Laplacian** (edges) | `STENCIL_DOT` with the gradient kernel |
| **Sharpen / unsharp** | `STENCIL_DOT` with a sharpen kernel (or `img + k*(img − blur)`) |
| **Median** / rank filter | `STENCIL_MEDIAN` |
| **Erode / dilate** (morphology) | `STENCIL_MIN` / `STENCIL_MAX` over the structuring element |
| **Open / close / tophat** | compositions of erode + dilate |
| **Max / avg pooling** | `STENCIL_MAX` / `STENCIL_MEAN` with `stride > 1` |
| **Local (adaptive) threshold** | `STENCIL_MEAN` then `img > local_mean − C` |

This single primitive is the highest-leverage thing to build — it's the convolution/morphology/pooling kernel the whole toolkit stands on, and (Scope 2) it's exactly the kind of contiguous inner loop that auto-vectorizes well.

## 1.4 The remaining new pieces

- **Histogram / `bincount`** — `histogram(img, bins)` → counts; the basis for Otsu thresholding, equalization, and per-channel stats. (Genuinely new — a scatter-add, not a reduction.)
- **Resize / interpolation** — `resize(img, (h, w), method)` with nearest / bilinear (bicubic later); a gather with fractional coordinates.
- **Geometric warp** — `flip(img, axis)` (new flip op), `rotate(img, deg)`, `affine_warp(img, M)` — coordinate gather.
- **Connected-components labeling** — `label(mask)` → integer-labeled regions; the basis for region measurement (`regionprops`). New (union-find over the mask).
- **Colour-space conversions** — `rgb2gray`, `rgb2hsv`/`hsv2rgb`, `rgb2lab` — per-pixel transforms; some are channel-axis matrix multiplies (reuse `matmul`), some are nonlinear (new but simple).
- **Otsu / adaptive thresholding** — histogram-based (reuses histogram + reductions).

## 1.5 Image I/O — bridging to Radiant

Radiant already has raster image machinery ([render_img.cpp](../radiant/render_img.cpp), [render_output.cpp](../radiant/render_output.cpp)) for decoding `<img>` content during layout and encoding raster output (PNG/SVG). Scope 1 exposes a clean **pixel-array bridge** on top of it:

```lambda
image.load("photo.png")     // → ArrayNum (H, W, C), ELEM_UINT8
image.save(arr, "out.png")  // ArrayNum → PNG/JPEG/…, dtype/shape inferred
```

If a standalone decoder/encoder is wanted independent of Radiant, **`stb_image` / `stb_image_write`** (single-header, public-domain) is the lightweight choice — it vendors into the Premake build with no dependency wiring, the same way the SIMD scope avoids heavy deps. Format coverage: PNG/JPEG/BMP/TGA in, PNG/JPEG out (the common cases); more via Radiant's existing codecs.

## 1.6 Syntax — an `image` module, composing through pipes

No special syntax; an `image` namespace of functions over `ArrayNum`, composing with the existing pipe:

```lambda
image.load("photo.png")
| image.as_float           // uint8 → float [0,1]
| image.gray               // (H,W,3) → (H,W) via channel-weighted reduction
| image.blur(sigma: 2.0)   // Gaussian — stencil engine
| (it > 0.5)               // threshold → bool mask (vectorized comparison)
| image.as_ubyte
| image.save("edges.png")
```

```lambda
// count bright regions
let img   = image.load("cells.png") | image.gray
let mask  = img > image.otsu(img)          // Otsu threshold → mask
let labels = image.label(mask)             // connected components
let n     = max(labels)                    // number of regions
```

Point/colour/geometric ops read as ordinary array expressions; filters/morphology read as `image.*` calls over the stencil engine.

## 1.7 Phasing (Scope 1)

1. **I/O bridge + conventions** — `image.load`/`save`, `as_float`/`as_ubyte`, shape/dtype validation. *Test:* round-trip a PNG.
2. **Point & colour & geometric** (mostly reuse) — threshold, gamma, invert, brightness/contrast, `gray`, channel split/merge, crop, `flip`, `rot90`. *Test:* against known small images.
3. **The stencil engine** — `array_num_stencil` + border modes; then `blur` (box + Gaussian, separable), `sobel`, `sharpen`, `median`, `erode`/`dilate`/`open`/`close`. *Test:* kernels vs reference outputs.
4. **Histogram-driven** — `histogram`, `otsu`, adaptive threshold, equalization.
5. **Segmentation/measurement** — `label` (connected components), basic `regionprops` (area/centroid/bbox via axis reductions).
6. **Resize/warp** — nearest/bilinear `resize`, `rotate`, `affine_warp`.
7. **Future** — Canny (gradient + NMS + hysteresis), corner/feature detection, watershed, registration. Out of scope here.

## 1.8 Non-goals / honest scoping (Scope 1)

- **Not** a full computer-vision suite (no SIFT/ORB/optical-flow/deep-learning); that's a far larger effort and likely belongs to bindings, not a built-in.
- **Eager, in-memory** (skimage/NumPy model), not a libvips-style streaming tile pipeline — gigapixel images and bounded-memory streaming are explicitly future.
- Colour management (ICC profiles), EXIF, animation — out of scope.

---

# Scope 2 — SIMD via Auto-Vectorization

> **In scope:** restructure the `ArrayNum` kernels so the compiler auto-vectorizes them, plus build flags and pragmas. **No dependency, no hand intrinsics.** **Out of scope (future):** a portable SIMD library (Google Highway / xsimd) with runtime ISA dispatch — a separate proposal.

## 2.1 The current state: kernels exist but cannot vectorize

`lambda-vector.cpp` already has contiguous, per-element-type fast paths for the common 1-D same-type cases. Here is the `ELEM_FLOAT32` add/sub/mul/… path ([lines 685‑705](../lambda/lambda-vector.cpp)):

```cpp
float* sa = (float*)vec_a.array_num->data;
float* sb = (float*)vec_b.array_num->data;
ArrayNum* result = array_num_new(ELEM_FLOAT32, len);
float* dst = (float*)result->data;
for (int64_t i = 0; i < len; i++) {
    switch (op) {                                 // ← branch inside the loop body
        case 0: dst[i] = sa[i] + sb[i]; break;
        case 1: dst[i] = sa[i] - sb[i]; break;
        ...
        case 5: dst[i] = powf(sa[i], sb[i]); break;  // ← non-vectorizable op mixed in
    }
}
```

It is *contiguous* and *native-typed* — good — but four things defeat the auto-vectorizer:

1. **The `switch(op)` is inside the loop.** A per-element branch the vectorizer generally won't cross (it relies on loop-unswitching that isn't guaranteed).
2. **No `restrict`.** The compiler must assume `dst`, `sa`, `sb` may alias, so it can't prove the loop is parallelizable.
3. **Non-vectorizable ops mixed in.** `pow`/`fmod`/`%` (with its `!= 0` branch) live in the same loop as `+ - *`; their presence poisons the whole body.
4. **Only 3 of 14 element kinds covered** (`INT64`, `INT`, `FLOAT32`). Everything else — including `ELEM_UINT8` (images!), `ELEM_FLOAT`/`FLOAT64`, `INT8/16/32` — falls through to `vec_broadcast_op`, which converts every element to `double` via a per-element `switch` (`read_arr_elem_as_double`) and walks strides. That path is the *opposite* of vectorizable.

The reduction walkers (`reduce_lane`, mask gather, `vec_cmp`) similarly read through `read_arr_elem_as_double` (strided + double), so axis sums/min/max don't vectorize either.

## 2.2 The auto-vectorization plan

**(a) Hoist `op` out of the loop — one straight-line body per op.** Templatize (or generate per-op functions) so each loop is a single SIMD-able statement:

```cpp
template <typename T, char OP>
static void vec_kernel(const T* __restrict a, const T* __restrict b,
                       T* __restrict dst, int64_t n) {
    #pragma omp simd
    for (int64_t i = 0; i < n; i++) {
        if      constexpr (OP == '+') dst[i] = a[i] + b[i];
        else if constexpr (OP == '-') dst[i] = a[i] - b[i];
        else if constexpr (OP == '*') dst[i] = a[i] * b[i];
    }
}
```

`if constexpr` collapses to one operation at compile time; `__restrict` removes the aliasing barrier; `#pragma omp simd` (or `#pragma clang loop vectorize(enable)`) requests vectorization. The dispatch `switch(op)` happens **once**, outside, selecting the kernel.

**(b) Separate vectorizable from non-vectorizable ops.** Route `+ - *`, bitwise, compare, `min`/`max`, abs/neg, and clamp to the vectorized kernels; keep `/` (optional), `%`, `pow`, `fmod` on a scalar path. Don't let the slow ops share the hot loop.

**(c) Expand per-type coverage — prioritize the image dtypes.** Add contiguous kernels for `ELEM_FLOAT`/`FLOAT64`, `ELEM_UINT8`, `INT8/16/32`, `UINT16/32`. `uint8` and `float` are both the most common image types *and* the widest SIMD lanes (16×`uint8` or 8×`float` per AVX2 register), so they give the biggest speedup and directly accelerate Scope 1. A small code-gen macro/template instantiates `{type} × {op}` so coverage isn't 14× hand-written.

**(d) Give the reduction walkers contiguous fast paths.** A contiguous `sum`/`min`/`max`/`mean`/`all`/`any`/`vec_cmp` over a single native type is highly vectorizable (horizontal reductions are well-supported). Add 1-D contiguous specializations alongside the existing strided/double `reduce_lane` (which stays as the N-D/view fallback). The stencil engine (Scope 1) gets the same treatment — its inner kernel loop is contiguous along the row.

**(e) Build flags + pragmas + verification.** Compile the math TU with `-O3` and an arch flag; **because Lambda is built from source (`make release`), `-march=native` is viable** — the compiler targets the actual machine and emits its best ISA (AVX2/AVX-512/NEON) with no runtime dispatch needed. Verify with `-Rpass=loop-vectorize` / `-Rpass-missed=loop-vectorize` (Clang) or `-fopt-info-vec` (GCC) to confirm which loops vectorized and why others didn't. Benchmark **only on the release build** (per CLAUDE.md) with a microbench harness over representative sizes.

**(f) Keep the general path as the fallback.** `vec_broadcast_op` (double, strided) remains correct for mixed-type, broadcast, and non-contiguous cases. The fast vectorized kernels are gated on "both 1-D contiguous, same native type, vectorizable op."

## 2.3 Why auto-vectorization (not a library) now

- **Zero dependency, fits "C+".** Plain `for` loops + `__restrict` + a pragma + a build flag — idiomatic to the codebase. A template-heavy SIMD library (Highway/xsimd) cuts against the C-compatible convention and adds real build integration.
- **Build-from-source makes it sufficient for most of the win.** `-march=native` means auto-vec already targets AVX-512/NEON on the machine that runs the binary; the main thing a library buys — *runtime dispatch across ISAs in a shipped binary* — Lambda doesn't currently need.
- **It's the necessary first step regardless.** A SIMD library can't help until the kernels are contiguous, per-type, branch-hoisted, and alias-free — which is exactly this work. Doing it first de-risks (and may obviate) a future library.

## 2.4 Deferred to a future proposal (explicitly out of scope)

- **A portable SIMD library** — **Google Highway** (write-once, runtime-dispatched across SSE/AVX/NEON/SVE — "NPYV as a library") or **xsimd** (header-only, compile-time). Adopt *with measurements*, and primarily if Lambda ever ships **prebuilt binaries** (where `-march=native` is unsafe and runtime dispatch becomes necessary). The staircase is: this proposal (auto-vec) → measure → library only for the hot kernels that remain under-vectorized.
- **`ELEM_FLOAT16`** — native f16 SIMD is uneven (NEON ARMv8.2+ vs x86 F16C/AVX-512-FP16); f16 likely stays on the widen-to-f32 path.
- **MIR-JIT-generated SIMD** — not viable; the vectorized ops are AOT C++ kernels called from JIT'd code, and MIR is a lightweight IR without rich vector ops. SIMD belongs in the AOT kernels.

## 2.5 Phasing (Scope 2)

1. **Refactor the existing fast paths** (`INT64`/`INT`/`FLOAT32`) to branch-hoisted, `__restrict`, per-op kernels; split off `mod`/`pow`. Verify they now vectorize (`-Rpass`). *Test:* baseline parity; microbench before/after.
2. **Expand type coverage** via a `{type}×{op}` instantiation macro — add `FLOAT`/`FLOAT64`, `UINT8`, `INT8/16/32`, `UINT16/32`. *Test:* parity across types.
3. **Contiguous reduction kernels** — `sum`/`min`/`max`/`mean`/`all`/`any`/`vec_cmp` 1-D specializations. *Test:* parity vs the strided walkers.
4. **Build flags + pragmas + bench harness** — `-O3 -march=native`, `#pragma omp simd`, vectorization-report verification, a `temp/`-based microbench. *Deliverable:* a before/after speedup table on release.
5. (Then Scope 1's stencil engine inherits the same contiguous-kernel treatment.)

## 2.6 Risks (Scope 2)

- **Numerical drift.** SIMD reductions reassociate float addition (pairwise/tree order), so a vectorized `sum`/`mean` may differ in the last ULP from the scalar left-fold. Acceptable (and matches NumPy's pairwise summation), but **the typed-array golden tests must tolerate it** — pin expected outputs that are reassociation-stable, or compare with an epsilon for float reductions.
- **`-march=native` and the build matrix.** Fine for build-from-source; if Lambda later ships binaries, this flag must drop to a safe baseline + (future) runtime dispatch. Document the constraint at the build-config layer.
- **Auto-vec is fragile.** A stray function call, conversion, or branch silently de-vectorizes. The `-Rpass` verification step is not optional — it's how you catch regressions where a loop quietly fell back to scalar.
- **Code-gen macro/template surface.** The `{type}×{op}` instantiation must stay readable and within the C+ convention (prefer a contained macro/`if constexpr` block over sprawling templates).

---

# Scope 3 — Mutable `ArrayNum` Views in Procedural Functions

> **In scope:** writing *through* an `ArrayNum` view to its base — in-place sub-region updates — **only inside procedural (`pn`) functions** over a mutable base. **Out of scope:** any mutation in pure `fn` code, mutable views escaping into functional context, or a general mutable-array model. Lambda stays value-semantic by default; this is a scoped, procedural-only relaxation for numeric/image buffers.

## 3.1 Motivation: in-place is mandatory for image-scale data

Lambda is pure-functional by default (`fn`), and Typed Array 2 made views **read-only** — mutation is rejected at both the runtime (`array_num_set_item` / `fn_array_set`) and the MIR index-assign fast path (the `is_view` bit `0x20`). That is *correct* for the functional core: it preserves value semantics and avoids the NumPy aliasing-mutation footgun this whole design has steered around.

But Lambda also has a **procedural** subset — `pn` functions, `run` / `main()` — where mutation *is* the model and index assignment (`arr[i, j] = v`) already exists (`INDEX_ASSIGN_STAM` → `array_num_set_nd`). And numeric/image work is fundamentally **in-place**: writing a patch into a crop, zeroing masked pixels, accumulating into a buffer. A 4K RGBA frame is ~33 MB; copying it on every operation (the only option if views are read-only) is ruinous. This is exactly where NumPy/skimage rely on mutable views — `img[r0:r1, c0:c1] = patch`, `img[mask] = 0`, `channel[:] = …`. Scope 1's image toolkit is impractical without it.

So Scope 3 grants **write-through views inside procedural code**, a controlled relaxation: the `fn` world stays pure (and, as §3.4 notes, *cannot even express* a mutation), while `pn` code gets in-place array updates.

## 3.2 What already exists (Typed Array 2/3 did the hard part)

The view substrate is fully built; only a guard blocks writing:

- `is_view` views alias a base buffer; the `ArrayNumShape` side-table carries `offset` + `base` (typed `Container*`) + strides; the GC tracer marks `base`; the base carries `is_pinned`; the **compactor skips view/pinned data** (`flags & 0x60`, [gc_heap.c:1209](../lib/gc/gc_heap.c)). All landed.
- The **only** thing preventing write-through is the read-only guard. (The current behavior is even covered by a test — `test/lambda/proc/proc_view_readonly.ls` asserts a view rejects writes; Scope 3 deliberately changes that for the mutable case.)
- Typed Array 3 already introduced the **`is_mutable_view`** concept for JS writable typed arrays. Scope 3 is the Lambda-procedural instance of the same capability — shared bit, shared write path.

## 3.3 Design

**An `is_mutable_view` flag** (one of the reserved upper-nibble `Container.flags` bits) marks a view as writable. A view is created mutable **iff** (a) it is produced inside a procedural function **and** (b) its base is a mutable binding (not a literal/constant/frozen array). Otherwise it stays read-only — so a view over a constant is never writable, even in `pn`.

**Write-through** removes the `is_view` rejection *when `is_mutable_view` is set*, and computes the strided base offset for the element:

```
base_offset = shape.offset + Σ idxᵢ · strideᵢ      // reuses array_num_set_nd's offset math
base->data[base_offset] = coerce(value, base.elem_type)
```

**Three procedural write forms** (the surface):

| Form | Meaning |
|---|---|
| `v[i] = x` / `v[i, j] = x` | element write through a (mutable) view → base |
| `img[r0 to r1, c0 to c1] = patch` | **slice / region assignment** — write a sub-block; RHS is a scalar (fill) or a shape-matched block (strided copy) |
| `img[mask] = v` / `img[mask] = values` | **masked assignment** — write where `mask` is true; the write counterpart to the `arr[mask]` *read* indexing built this session |

All three desugar to writes through a mutable view of the LHS region — so once write-through-view works, slice- and mask-assignment are thin layers on top.

## 3.4 The purity boundary — why this is safe

The functional core is protected **by construction**, not by runtime policing:

- **`fn` cannot issue an assignment statement.** Index/slice/mask assignment are *procedural* statements; a pure function has no `arr[i] = v` form. So no `fn` can ever mutate — the write paths are unreachable from functional code. The runtime guard only needs relaxing on the path that *only* procedural code can reach.
- **Escape rule.** Mutability is a property of the *context*, not the value: `is_mutable_view` is set only when a view is created in procedural scope over a mutable base, and a view handed to `fn` is consumed read-only (functions can read it, never assign). A conservative first cut keeps mutable views from being *returned* out of the procedure that owns the base; stricter escape analysis can come later. The contract is documented, not silently assumed.

This keeps Lambda's value-semantics guarantee intact where it matters (the functional language) while admitting the controlled aliasing that procedural numeric code needs — the same line R draws with `data.table`'s opt-in reference semantics, and the opposite of pandas exposing mutable aliasing everywhere (with its `SettingWithCopyWarning` consequences).

## 3.5 Hazards (honest)

- **Aliasing is real and intended.** `let v = img[2]; v[0] = 9` mutates row 2 of `img`. That's the point (it's how in-place works) — but it *is* the NumPy footgun, confined here to procedural code where the programmer opted into mutation.
- **Overlapping region assignment.** `a[1 to 3] = a[2 to 4]` has overlapping source/dest; use `memmove` semantics (like NumPy) or document as undefined. Region copy must pick one and stick to it.
- **Base liveness.** A mutable view extends the base's pinned lifetime; the existing `is_pinned` + tracer-marks-`base` handles it. Writing through a view whose base was detached/freed must be rejected (mirrors the JS detached-buffer rule from Typed Array 3).

## 3.6 Implementation changes

- Add the `is_mutable_view` flag bit (`Container.flags` upper nibble — a reserved bit remains from the Typed Array 2 layout).
- Relax the write rejection in `array_num_set_item`, `fn_array_set`, and the **MIR index-assign fast path** to allow when `is_mutable_view` (else keep rejecting).
- View-producing ops (`subview`, slicing `arr[start to end]`, leading-axis index `arr[i]`, `transpose`) propagate `is_mutable_view` when created in a procedural mutable context.
- Grammar/transpiler: **slice-assignment** `lhs[range, …] = rhs` and **masked-assignment** `lhs[mask] = rhs` statement forms (procedural only), desugaring to mutable-view writes.
- Region/mask write kernels (the in-place counterparts of slice/`fn_mask_index`) — contiguous where possible, so they're Scope 2 SIMD targets.

## 3.7 Phasing (Scope 3)

1. **`is_mutable_view` + element write-through.** Flag, relaxed guard, base-offset write. *Test:* `pn` `{ let v = subview(img, …); v[i, j] = x }` mutates `img`; a view over a constant still rejects; a view in `fn` still rejects.
2. **Slice / region assignment** `img[r0 to r1, c0 to c1] = patch` (scalar fill + block copy, `memmove` for overlap). *Test:* write a patch into a crop.
3. **Masked assignment** `img[mask] = v` / `= values`. *Test:* `img[img < 0.5] = 0` (the image idiom).
4. **Escape/mutable-base guarding + docs.** *Test:* mutation rejected across the `fn` boundary and over frozen/constant bases.

## 3.8 Non-goals (Scope 3)

- No mutation in pure `fn` — purity preserved.
- No mutable views escaping into functional code (read-only on crossing).
- Not a general mutable-array story — Lambda stays value-semantic by default; this is an in-place capability scoped to procedural numeric/image buffers.
- No copy-on-write or persistent-data-structure machinery — out of scope; the model is "explicit in-place in `pn`, copy elsewhere."

---

# Cross-cutting

## Synergy between the scopes

The three scopes lock together — image processing is both the workload that most rewards SIMD *and* the workload that most needs in-place mutation:

- Image pixels are `uint8`/`float`, **contiguous along rows** — the widest SIMD lanes and the easiest auto-vec targets. The Scope 2 `uint8`/`float` kernels directly make Scope 1 fast.
- The Scope 1 **stencil engine**'s inner loop is a contiguous dot/min/max along a row — exactly the shape Scope 2 vectorizes. Blur, Sobel, erode/dilate become SIMD kernels for free once both land.
- Point/threshold/arithmetic image ops *are* the `ArrayNum` elementwise kernels — so every Scope 2 speedup is an image-processing speedup.
- **Scope 3 makes the image ops in-place.** `img[mask] = 0`, writing a filtered tile back into a crop, accumulating into a buffer — without mutable views each is a full-frame copy. And those in-place region/mask writes are themselves **contiguous → SIMD targets** (Scope 2). So Scope 3 closes the loop: image algorithms (1) run in-place through mutable views (3) on vectorized kernels (2).

Recommended order: **Scope 2 phases 1–3 first** (vectorizer-friendly kernels + type coverage + reductions), then **Scope 3** (mutable write-through, so ops can be in-place), then **Scope 1** on top — so the image toolkit is fast *and* allocation-light from day one.

## Non-goals (combined)

- No streaming/out-of-core/gigapixel pipeline (libvips-style) — eager in-memory only.
- No SIMD library, no hand intrinsics, no JIT-emitted SIMD (Scope 2 future / non-goals).
- No full CV/ML image suite (Scope 1 future).
- No mutation in pure `fn`, no mutable views escaping to functional code, no general mutable-array model (Scope 3 — purity preserved by construction).
- No change to `ArrayNum`'s default semantics — all three scopes are additive: a library, faster kernels, and a procedural-only in-place capability. Functional `ArrayNum` stays value-semantic.

## Success criteria

- **Image:** `image.load → gray → blur → threshold → save` runs end-to-end; blur/Sobel/erode/dilate/median match reference outputs on test images; `histogram`/`otsu`/`label` work; an image is just an `ArrayNum` accepted by all existing array ops.
- **SIMD:** the refactored elementwise kernels and contiguous reductions **demonstrably vectorize** (`-Rpass=loop-vectorize` confirms), with a measured release-build speedup table (target: multiple× on `float`/`uint8` elementwise + reductions over large arrays); `make test-lambda-baseline` stays green (float reductions within tolerance).
- **Mutable views:** inside a `pn` function, `v[i,j] = x` / `img[r0 to r1, c0 to c1] = patch` / `img[mask] = 0` write through to the base in-place (a 4K frame is filtered with **no full-frame copy**); the same writes are **rejected** in `fn` context and over a constant/frozen base; `make test-lambda-baseline` stays green and the existing `proc_view_readonly` guarantee is updated, not broken (read-only still holds for non-mutable views).

---

## Appendix A — surfaces this builds on

| Surface | Location |
|---|---|
| `ArrayNum` (14 kinds, N-D shape/strides, broadcasting, masks, axis reductions) | [lambda-vector.cpp](../lambda/lambda-vector.cpp), [lambda-data-runtime.cpp](../lambda/lambda-data-runtime.cpp) (Typed Array 1/2) |
| Existing per-type contiguous fast paths (to be made vectorizer-friendly) | [lambda-vector.cpp:647‑725](../lambda/lambda-vector.cpp) |
| Strided/broadcast fallback + `read_arr_elem_as_double` (the double/scalar path) | [lambda-vector.cpp `vec_broadcast_op`](../lambda/lambda-vector.cpp) |
| N-D helpers (`get_shape_strides`, `alloc_ndim_arraynum`) for the stencil engine | [lambda-vector.cpp](../lambda/lambda-vector.cpp) |
| Boolean masks / vectorized comparison (threshold; mask-read counterpart of mask-assign) | [lambda-vector.cpp `fn_mask_index` / `vec_cmp`](../lambda/lambda-vector.cpp) |
| View machinery (`is_view`, `base: Container*`, pinned base, shape `offset`/strides) | [lambda-data-runtime.cpp](../lambda/lambda-data-runtime.cpp), [lambda.h](../lambda/lambda.h) (Typed Array 2 §2) |
| Read-only write guard to relax for mutable views | `array_num_set_item` / `fn_array_set` ([lambda-data-runtime.cpp](../lambda/lambda-data-runtime.cpp), [lambda-eval.cpp](../lambda/lambda-eval.cpp)); MIR index-assign fast path ([transpile-mir.cpp](../lambda/transpile-mir.cpp)) |
| Procedural index assignment + read-only-view test | `INDEX_ASSIGN_STAM` → `array_num_set_nd`; `test/lambda/proc/proc_view_readonly.ls` |
| `is_mutable_view` concept (shared with JS writable typed arrays) | [Lambda_Typed_Array3.md](Lambda_Typed_Array3.md) §6.4 |
| Radiant raster image machinery (I/O bridge) | [render_img.cpp](../radiant/render_img.cpp), [render_output.cpp](../radiant/render_output.cpp) |
| Release build (perf testing) + build config | `make release`, `build_lambda_config.json` |
| SIMD library staircase + rationale (future scope) | this thread's SIMD analysis; Highway / xsimd |
