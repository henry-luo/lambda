#lang racket/base

;; layout-grid.rkt — CSS Grid layout algorithm
;;
;; Implements CSS Grid Layout Level 1 (https://www.w3.org/TR/css-grid-1/)
;; Covers:
;;   - Track definition and sizing (px, fr, auto, minmax, %)
;;   - Explicit and auto item placement
;;   - Fr unit distribution
;;   - Grid alignment (justify/align items/content/self)
;;   - Grid auto tracks (grid-auto-columns/rows)
;;   - Absolute positioning within grid containers
;;
;; Corresponds to Radiant's layout_grid_multipass.cpp.

(require racket/match
         racket/list
         racket/math
         "css-layout-lang.rkt"
         "layout-common.rkt"
         "layout-positioned.rkt")

(provide layout-grid)

;; ============================================================
;; Baseline Computation
;; ============================================================

;; compute the first baseline of a view recursively
;; if no children, synthesized baseline = bottom of box
(define (compute-view-baseline view)
  (define h (view-height view))
  (define children (view-children view))
  (if (or (null? children) (not (pair? children)))
      h  ;; synthesized baseline: bottom of box
      (let ([first-child (car children)])
        (+ (view-y first-child) (compute-view-baseline first-child)))))

;; ============================================================
;; Grid Item — internal representation
;; ============================================================

(struct grid-item
  (box styles bm
   row-start row-end     ; 0-based track indices
   col-start col-end
   view                  ; laid out view
   )
  #:transparent #:mutable)

;; ============================================================
;; Track — represents a single row or column track
;; ============================================================

(struct track
  (index
   track-size             ; original TrackSize spec
   base-size              ; resolved base size
   growth-limit           ; resolved growth limit
   )
  #:transparent #:mutable)

;; ============================================================
;; Grid Layout — Main Entry Point
;; ============================================================

(define (layout-grid box avail dispatch-fn)
  (match box
    [`(grid ,id ,styles (grid-def (,row-defs ...) (,col-defs ...)) (,children ...))
     (define avail-w (avail-width->number (cadr avail)))
     (define avail-w-spec (cadr avail)) ;; keep the raw avail-width spec for min/max-content detection
     ;; content-sized?: the container width was derived from content (shrink-to-fit),
     ;; not from stretching to fill a containing block. This affects fr distribution:
     ;; sub-1 flex factor sums should NOT be floored to 1 (CSS Grid §11.7.1) because
     ;; the available space already equals the content-determined size.
     (define content-sized? (and (pair? avail-w-spec) (eq? (car avail-w-spec) 'content-sized)))
     (define avail-h (avail-height->number (caddr avail)))
     (define bm (extract-box-model styles avail-w))

     ;; extract grid properties
     (define col-gap-val (get-style-prop styles 'column-gap 0))
     (define row-gap-val (get-style-prop styles 'row-gap 0))
     (define row-gap (resolve-gap row-gap-val avail-h))
     (define col-gap (resolve-gap col-gap-val avail-w))
     (define justify-items (get-style-prop styles 'justify-items 'align-stretch))
     (define align-items (get-style-prop styles 'align-items 'align-stretch))
     (define justify-content (get-style-prop styles 'justify-content 'start))
     (define align-content (get-style-prop styles 'align-content 'content-stretch))

     ;; grid-auto-columns/rows for implicit tracks
     (define auto-col-defs (get-style-prop styles 'grid-auto-columns '()))
     (define auto-row-defs (get-style-prop styles 'grid-auto-rows '()))

     ;; resolve container content width/height
     (define content-w (if avail-w (resolve-block-width styles avail-w) 0))
     (define explicit-h (resolve-block-height styles avail-h avail-w))

     ;; === Phase 1: Create tracks from definitions ===
     (define col-tracks (create-tracks col-defs))
     (define row-tracks (create-tracks row-defs))

     ;; === Phase 2: Separate flow children from absolute children ===
     ;; partition-children returns (dom-index . child) pairs
     (define-values (flow-indexed abs-indexed)
       (partition-children children))
     (define flow-children (map cdr flow-indexed))
     (define flow-dom-indices (map car flow-indexed))
     (define abs-children (map cdr abs-indexed))
     (define abs-dom-indices (map car abs-indexed))

     ;; === Phase 3: Place items into grid cells ===
     ;; place-grid-items handles negative line resolution internally,
     ;; shifting all positions to non-negative and returning the offsets
     (define-values (items row-before-count col-before-count)
       (place-grid-items flow-children styles
                         (length row-tracks) (length col-tracks) content-w))

     ;; === Phase 3.5: Create implicit tracks before/after the explicit grid ===
     ;; create "before" implicit tracks with REVERSE auto-track cycling
     ;; per CSS Grid spec: implicit tracks before explicit cycle backwards
     (define (make-before-tracks count auto-defs)
       (for/list ([i (in-range count)])
         (define def
           (if (null? auto-defs)
               'auto
               ;; track i at distance (count-i) from explicit
               ;; distance 1 -> last auto-def, distance 2 -> second-to-last, etc.
               (let ([distance (- count i)])
                 (list-ref auto-defs
                           (modulo (- (length auto-defs) distance)
                                   (length auto-defs))))))
         (track i def 0 +inf.0)))

     ;; insert before-tracks and renumber existing tracks
     (when (> row-before-count 0)
       (define before-rows (make-before-tracks row-before-count auto-row-defs))
       (for ([t (in-list row-tracks)] [i (in-naturals row-before-count)])
         (set-track-index! t i))
       (set! row-tracks (append before-rows row-tracks)))
     (when (> col-before-count 0)
       (define before-cols (make-before-tracks col-before-count auto-col-defs))
       (for ([t (in-list col-tracks)] [i (in-naturals col-before-count)])
         (set-track-index! t i))
       (set! col-tracks (append before-cols col-tracks)))

     ;; ensure enough implicit tracks AFTER the explicit grid for placed items
     (define max-row (if (null? items) (length row-tracks)
                         (apply max (length row-tracks) (map grid-item-row-end items))))
     (define max-col (if (null? items) (length col-tracks)
                         (apply max (length col-tracks) (map grid-item-col-end items))))
     ;; for after-explicit tracks, use forward cycling but offset by before-count + explicit-count
     (define (ensure-tracks-after tracks n auto-defs before-count explicit-count)
       (define current (length tracks))
       (if (>= current n)
           tracks
           (append tracks
                   (for/list ([i (in-range current n)])
                     (define def
                       (if (null? auto-defs)
                           'auto
                           ;; cycle forward from index 0 for tracks after explicit
                           (list-ref auto-defs (modulo (- i before-count explicit-count)
                                                       (length auto-defs)))))
                     (track i def 0 +inf.0)))))
     (set! row-tracks (ensure-tracks-after row-tracks max-row auto-row-defs
                                            row-before-count (length row-defs)))
     (set! col-tracks (ensure-tracks-after col-tracks max-col auto-col-defs
                                            col-before-count (length col-defs)))

     ;; === Phase 4: Resolve column track sizes ===
     (define total-col-gaps (* col-gap (max 0 (sub1 (length col-tracks)))))
     (define col-available (- content-w total-col-gaps))
     ;; when the grid has a definite height, pass it as cross-axis available
     ;; so that percentage heights and aspect-ratio can resolve during column sizing
     (define col-cross-avail (or explicit-h +inf.0))
     ;; detect min-content measurement mode: when the grid is being sized for
     ;; min-content contribution, the "maximize tracks" phase (§12.6) should NOT
     ;; grow tracks to their growth limits — only base sizes from intrinsic
     ;; contributions matter for the grid's min-content width.
     (define is-min-content-mode? (eq? avail-w-spec 'av-min-content))
     (resolve-track-sizes! col-tracks col-available items 'col dispatch-fn avail col-gap row-gap col-cross-avail content-w is-min-content-mode? #f 0 content-sized?)

     ;; === Phase 4.1: Re-resolve percentage column gap for indefinite containers ===
     ;; CSS Grid spec: percentage gaps resolve to 0 during intrinsic sizing, then
     ;; re-resolve against the intrinsic track total, and tracks are re-sized with the new gap.
     ;; The container width stays at the intrinsic size (from gap=0), items overflow.
     ;; When the container IS definite but percentage gap applies, the tracks should still
     ;; be sized with indefinite available (intrinsic sizing) so max-content tracks reach
     ;; their full contributions and overflow if needed.
     (define col-gap-is-pct? (and (pair? col-gap-val) (eq? (car col-gap-val) '%)))
     (define pct-gap-intrinsic-w #f) ;; saved intrinsic width for final_content_w
     (when col-gap-is-pct?
       (cond
         ;; case 1: indefinite container — gap resolved to 0, need intrinsic sizing first
         [(= col-gap 0)
          ;; compute intrinsic track total (with gap=0)
          (define intrinsic-track-total (tracks-total-size col-tracks))
          (set! pct-gap-intrinsic-w intrinsic-track-total)
          ;; resolve percentage gap against track total
          (define resolved-gap (* (/ (cadr col-gap-val) 100) intrinsic-track-total))
          (set! col-gap resolved-gap)
          ;; re-resolve tracks with the new gap using indefinite available
          ;; (tracks size to intrinsic contributions, gap only affects spanning distribution)
          (set! total-col-gaps (* col-gap (max 0 (sub1 (length col-tracks)))))
          (for ([t (in-list col-tracks)])
            (set-track-base-size! t 0)
            (set-track-growth-limit! t +inf.0))
          (resolve-track-sizes! col-tracks +inf.0 items 'col dispatch-fn avail col-gap row-gap col-cross-avail intrinsic-track-total)]
         ;; case 2: definite container with percentage gap — re-size tracks with indefinite
         ;; available so max-content tracks reach full contributions (grid content overflows)
         [else
          (set! total-col-gaps (* col-gap (max 0 (sub1 (length col-tracks)))))
          (for ([t (in-list col-tracks)])
            (set-track-base-size! t 0)
            (set-track-growth-limit! t +inf.0))
          (resolve-track-sizes! col-tracks +inf.0 items 'col dispatch-fn avail col-gap row-gap col-cross-avail content-w)]))

     ;; === Phase 4.5: Re-resolve column tracks for indefinite containers ===
     ;; CSS Grid spec: intrinsic sizing determines the container width, then the
     ;; grid is laid out with that definite width. The fr distribution may differ
     ;; because the "find size of an fr" algorithm (§11.7.1) with definite space
     ;; applies the max(sum,1) clamping differently than the indefinite flex fraction.
     ;; However, when fr sum < 1, the definite path's cap would shrink tracks below
     ;; what items need, so we skip re-resolution in that case.
     (define col-indefinite-for-rerun? (or (infinite? col-available) (<= col-available 0)))
     (define total-col-fr
       (for/sum ([t (in-list col-tracks)])
         (match (track-track-size t)
           [`(fr ,n) n]
           [`(auto-fit-track (fr ,n)) n]
           [`(auto-fit-track (minmax ,_ (fr ,n))) n]
           [_ 0])))
     (define intrinsic-col-total #f)
     (when (and col-indefinite-for-rerun? (ormap fr-track? col-tracks) (>= total-col-fr 1))
       (set! intrinsic-col-total (+ (tracks-total-size col-tracks) total-col-gaps))
       (define definite-col-avail (max 0 (- intrinsic-col-total total-col-gaps)))
       ;; reset column tracks for re-resolution with definite width
       (for ([t (in-list col-tracks)])
         (set-track-base-size! t 0)
         (set-track-growth-limit! t +inf.0))
       (resolve-track-sizes! col-tracks definite-col-avail items 'col dispatch-fn avail col-gap row-gap col-cross-avail intrinsic-col-total))

     ;; === Phase 5: Resolve row track sizes ===
     ;; === Phase 4.6: Apply max-width constraint for auto-width grid containers ===
     ;; When the grid has width:auto and max-width, the intrinsic track total may
     ;; exceed max-width. In that case, re-resolve tracks with max-width as the
     ;; definite available space so tracks fit within the constraint.
     (define max-w-val-raw (get-style-prop styles 'max-width 'none))
     (define max-w-resolved (resolve-size-value max-w-val-raw (or avail-w 0)))
     (define has-max-w-constraint?
       (and (not (has-explicit-size? styles 'width)) max-w-resolved (not (infinite? max-w-resolved))))
     (when has-max-w-constraint?
       (define max-content-w
         (if (eq? (box-model-box-sizing bm) 'border-box)
             (max 0 (- max-w-resolved (horizontal-pb bm)))
             max-w-resolved))
       (define current-track-total (+ (tracks-total-size col-tracks) total-col-gaps))
       (when (> current-track-total max-content-w)
         ;; re-resolve tracks with max-width as the available space
         (define max-w-col-avail (max 0 (- max-content-w total-col-gaps)))
         (for ([t (in-list col-tracks)])
           (set-track-base-size! t 0)
           (set-track-growth-limit! t +inf.0))
         (resolve-track-sizes! col-tracks max-w-col-avail items 'col dispatch-fn avail col-gap row-gap col-cross-avail max-content-w)
         ;; update content-w so the rest of the layout uses the constrained width
         (set! content-w max-content-w)))

     (define total-row-gaps (* row-gap (max 0 (sub1 (length row-tracks)))))
     (define row-available
       (if explicit-h
           (- explicit-h total-row-gaps)
           +inf.0))
     (resolve-track-sizes! row-tracks row-available items 'row dispatch-fn avail col-gap row-gap content-w content-w #f col-tracks col-gap)

     ;; === Phase 5.5: Collapse empty auto-fit tracks ===
     (collapse-empty-auto-fit-tracks! col-tracks items 'col)
     (collapse-empty-auto-fit-tracks! row-tracks items 'row)

     ;; recalculate gaps: collapsed tracks don't contribute gaps
     (define non-collapsed-cols (filter (lambda (t) (> (track-base-size t) 0)) col-tracks))
     (define non-collapsed-rows (filter (lambda (t) (> (track-base-size t) 0)) row-tracks))
     (define effective-col-gaps (* col-gap (max 0 (sub1 (length non-collapsed-cols)))))
     (define effective-row-gaps (* row-gap (max 0 (sub1 (length non-collapsed-rows)))))

     ;; === Phase 5.7: Resolve percentage tracks for indefinite containers ===
     ;; When the grid container's size is indefinite, percentage tracks were treated
     ;; as auto during intrinsic sizing (Phases 4-5). Now resolve them against
     ;; the content-determined grid size.
     (define has-explicit-w (has-explicit-size? styles 'width))
     (define col-indefinite? (or (infinite? col-available) (<= col-available 0)))
     (define row-indefinite? (or (infinite? row-available) (<= row-available 0)))
     (define has-pct-col-tracks?
       (ormap (lambda (t) (match (track-track-size t) [`(% ,_) #t] [_ #f])) col-tracks))
     (define has-pct-row-tracks?
       (ormap (lambda (t) (match (track-track-size t) [`(% ,_) #t] [_ #f])) row-tracks))

     ;; save content-determined totals before percentage resolution
     (define content-det-col-total (+ (tracks-total-size col-tracks) effective-col-gaps))
     (define content-det-row-total (+ (tracks-total-size row-tracks) effective-row-gaps))

     (when (and col-indefinite? has-pct-col-tracks?)
       (define resolve-base (max content-w content-det-col-total))
       (for ([t (in-list col-tracks)])
         (match (track-track-size t)
           [`(% ,pct)
            (define resolved (* (/ pct 100) resolve-base))
            (set-track-base-size! t resolved)
            (set-track-growth-limit! t resolved)]
           [_ (void)])))

     (when (and row-indefinite? has-pct-row-tracks?)
       (define resolve-base content-det-row-total)
       (for ([t (in-list row-tracks)])
         (match (track-track-size t)
           [`(% ,pct)
            (define resolved (* (/ pct 100) resolve-base))
            (set-track-base-size! t resolved)
            (set-track-growth-limit! t resolved)]
           [_ (void)])))

     ;; === Phase 6: Apply content alignment ===
     (define total-col-size (+ (tracks-total-size col-tracks) effective-col-gaps))
     (define total-row-size (+ (tracks-total-size row-tracks) effective-row-gaps))

     (define content-h-for-align (or explicit-h total-row-size))

     ;; align-content: distribute rows in block direction
     (define row-offsets
       (compute-content-alignment-offsets
        align-content row-tracks row-gap content-h-for-align))

     ;; justify-content: distribute columns in inline direction
     (define col-offsets
       (compute-content-alignment-offsets
        justify-content col-tracks col-gap content-w))

     ;; === Phase 7: Position items with alignment ===
     (define offset-x (+ (box-model-padding-left bm) (box-model-border-left bm)))
     (define offset-y (+ (box-model-padding-top bm) (box-model-border-top bm)))

     ;; --- Pass 1: Layout all items (compute child views, store layout info) ---
     (define layout-infos
       (for/list ([item (in-list items)]
                  [flow-idx (in-naturals)])
         (define dom-idx (list-ref flow-dom-indices flow-idx))
         (define col-start (grid-item-col-start item))
         (define col-end (grid-item-col-end item))
         (define row-start (grid-item-row-start item))
         (define row-end (grid-item-row-end item))
         (define item-styles (grid-item-styles item))

         ;; compute cell position and size using alignment offsets
         (define item-x (track-offset-with-alignment col-tracks col-start col-gap col-offsets))
         (define item-y (track-offset-with-alignment row-tracks row-start row-gap row-offsets))
         (define cell-w (track-span-size col-tracks col-start col-end col-gap))
         (define cell-h (track-span-size row-tracks row-start row-end row-gap))

         ;; determine item alignment (justify-self / align-self override container)
         (define ji (resolve-item-alignment item-styles 'justify-self justify-items))
         (define ai (resolve-item-alignment item-styles 'align-self align-items))

         ;; extract item margins for alignment positioning
         ;; percentage margins resolve against grid area inline size (cell-w)
         (define item-bm (extract-box-model item-styles cell-w))
         (define ml (box-model-margin-left item-bm))
         (define mr (box-model-margin-right item-bm))
         (define mt (box-model-margin-top item-bm))
         (define mb (box-model-margin-bottom item-bm))

         ;; check for auto margins — per CSS Grid spec, auto margins take precedence
         ;; over justify-self/align-self and consume free space in the grid area
         (define-values (raw-mt raw-mr raw-mb raw-ml) (get-raw-margins item-styles))
         (define margin-left-auto? (eq? raw-ml 'auto))
         (define margin-right-auto? (eq? raw-mr 'auto))
         (define margin-top-auto? (eq? raw-mt 'auto))
         (define margin-bottom-auto? (eq? raw-mb 'auto))

         ;; check if item has explicit width/height
         (define item-has-width (has-explicit-size? item-styles 'width))
         (define item-has-height (has-explicit-size? item-styles 'height))
         (define item-has-aspect-ratio (get-style-prop item-styles 'aspect-ratio #f))

         ;; for stretch: inject cell size minus margins as definite width/height
         ;; when aspect-ratio is set with one explicit dimension, don't stretch the other
         ;; baseline-aligned items do NOT stretch (per CSS spec)
         ;; auto margins override stretch (per CSS Grid §11.3)
         (define effective-ai (if (eq? ai 'align-baseline) 'align-start ai))
         (define has-auto-margin-inline? (or margin-left-auto? margin-right-auto?))
         (define has-auto-margin-block? (or margin-top-auto? margin-bottom-auto?))
         (define raw-stretch-w (and (eq? ji 'align-stretch) (not item-has-width)
                                    (not has-auto-margin-inline?)
                                    (not (and item-has-aspect-ratio item-has-height))))
         (define raw-stretch-h (and (eq? effective-ai 'align-stretch) (not item-has-height)
                                    (not has-auto-margin-block?)
                                    (not (and item-has-aspect-ratio item-has-width))))
         ;; per CSS Grid Level 2 §6.6.1: when aspect-ratio + both stretch →
         ;; decide which axis to stretch based on transferred minimum sizes.
         (define-values (stretch-w stretch-h)
           (if (and item-has-aspect-ratio raw-stretch-w raw-stretch-h)
               (let* ([ar item-has-aspect-ratio]
                      [min-h-val (get-style-prop item-styles 'min-height 'auto)]
                      [min-h-px (or (resolve-size-value min-h-val cell-h) 0)]
                      [transferred-min-w (if (> min-h-px 0) (* min-h-px ar) 0)]
                      [inner-cell-w (max 0 (- cell-w ml mr))])
                 (if (and (> transferred-min-w 0) (>= transferred-min-w inner-cell-w))
                     (values #f raw-stretch-h)
                     (values raw-stretch-w #f)))
               (values raw-stretch-w raw-stretch-h)))

         ;; when stretching, subtract margins from cell size
         (define stretch-cell-w (max 0 (- cell-w ml mr)))
         (define stretch-cell-h (max 0 (- cell-h mt mb)))

         ;; lay out child with cell dimensions
         ;; Per CSS Grid: the grid cell is the item's containing block.
         ;; Available width is always the cell width (even for non-stretched items),
         ;; so text wraps within the cell. For non-stretched items without explicit
         ;; width, the item shrinks to content within this constraint.
         (define actual-box
           (if (or stretch-w stretch-h)
               (inject-stretch-size (grid-item-box item) stretch-w stretch-cell-w stretch-h stretch-cell-h)
               (grid-item-box item)))
         (define avail-w-for-child `(definite ,cell-w))
         (define avail-h-for-child `(definite ,cell-h))
         (define child-avail
           `(avail ,avail-w-for-child ,avail-h-for-child))
         (define child-view (dispatch-fn actual-box child-avail))

         ;; compute baseline for this item
         (define item-baseline (compute-view-baseline child-view))

         ;; store all info needed for positioning
         (list dom-idx item-styles child-view item-baseline
               item-x item-y cell-w cell-h
               ji ai ml mr mt mb
               margin-left-auto? margin-right-auto? margin-top-auto? margin-bottom-auto?
               row-start row-end)))

     ;; --- Baseline pass: compute per-row max baselines ---
     (define num-rows (length row-tracks))
     (define row-max-baselines (make-vector (max 1 num-rows) 0))
     (for ([info (in-list layout-infos)])
       (define ai (list-ref info 9))
       (define row-start (list-ref info 18))
       (define row-end (list-ref info 19))
       (define mt (list-ref info 12))
       (define item-baseline (list-ref info 3))
       ;; only single-row baseline items participate
       (when (and (eq? ai 'align-baseline)
                  (= (- row-end row-start) 1)
                  (< row-start num-rows))
         (define outer-baseline (+ mt item-baseline))
         (when (> outer-baseline (vector-ref row-max-baselines row-start))
           (vector-set! row-max-baselines row-start outer-baseline))))

     ;; --- Baseline-induced row growth ---
     ;; Per CSS Grid spec: baseline alignment may increase the row track size
     ;; to accommodate items shifted down for baseline alignment.
     ;; For each row, compute the maximum total height needed:
     ;; max(baseline-shift + item-border-box-height + margin-bottom) across baseline items
     (define row-baseline-min-heights (make-vector (max 1 num-rows) 0))
     (for ([info (in-list layout-infos)])
       (define ai (list-ref info 9))
       (define row-start (list-ref info 18))
       (define row-end (list-ref info 19))
       (define mt (list-ref info 12))
       (define mb (list-ref info 13))
       (define child-view (list-ref info 2))
       (define item-baseline (list-ref info 3))
       (when (and (eq? ai 'align-baseline)
                  (= (- row-end row-start) 1)
                  (< row-start num-rows))
         (define cview-h (view-height child-view))
         (define row-max-bl (vector-ref row-max-baselines row-start))
         ;; baseline shift = row-max-bl - item-baseline (item shifts down this much)
         (define shift (- row-max-bl (+ mt item-baseline)))
         ;; total needed = margin-top + shift + item-height + margin-bottom
         (define needed (+ mt shift cview-h mb))
         (when (> needed (vector-ref row-baseline-min-heights row-start))
           (vector-set! row-baseline-min-heights row-start needed))))
     ;; grow row tracks if needed
     (define rows-grew? #f)
     (for ([r (in-range num-rows)])
       (define needed (vector-ref row-baseline-min-heights r))
       (define track (list-ref row-tracks r))
       (when (> needed (track-base-size track))
         (set-track-base-size! track needed)
         (set! rows-grew? #t)))
     ;; if rows grew, recompute row offsets for content alignment
     (when rows-grew?
       (define new-total-row-size (+ (tracks-total-size row-tracks) effective-row-gaps))
       (define new-content-h (or explicit-h new-total-row-size))
       (define new-row-offsets
         (compute-content-alignment-offsets
          align-content row-tracks row-gap new-content-h))
       ;; update the layout-infos with new item-y values
       (set! layout-infos
             (for/list ([info (in-list layout-infos)])
               (define row-start (list-ref info 18))
               (define row-end (list-ref info 19))
               (define col-start (let ([ci (list-ref info 4)])
                                   ;; need to reverse-compute col-start from item-x
                                   ;; but we don't have col-start stored... use row-start to look up
                                   ci))  ; keep original item-x
               ;; recompute item-y using new row offsets
               (define new-item-y (track-offset-with-alignment row-tracks row-start row-gap new-row-offsets))
               ;; also recompute cell-h since row track may have grown
               (define new-cell-h (track-span-size row-tracks row-start row-end row-gap))
               ;; update info: item-y is at index 5, cell-h is at index 7
               (list (list-ref info 0) (list-ref info 1) (list-ref info 2) (list-ref info 3)
                     (list-ref info 4) new-item-y (list-ref info 6) new-cell-h
                     (list-ref info 8) (list-ref info 9) (list-ref info 10) (list-ref info 11)
                     (list-ref info 12) (list-ref info 13)
                     (list-ref info 14) (list-ref info 15) (list-ref info 16) (list-ref info 17)
                     (list-ref info 18) (list-ref info 19)))))

     ;; --- Pass 2: Position items using alignment and baselines ---
     (define child-views
       (for/list ([info (in-list layout-infos)])
         (define dom-idx (list-ref info 0))
         (define item-styles (list-ref info 1))
         (define child-view (list-ref info 2))
         (define item-baseline (list-ref info 3))
         (define item-x (list-ref info 4))
         (define item-y (list-ref info 5))
         (define cell-w (list-ref info 6))
         (define cell-h (list-ref info 7))
         (define ji (list-ref info 8))
         (define ai (list-ref info 9))
         (define ml (list-ref info 10))
         (define mr (list-ref info 11))
         (define mt (list-ref info 12))
         (define mb (list-ref info 13))
         (define margin-left-auto? (list-ref info 14))
         (define margin-right-auto? (list-ref info 15))
         (define margin-top-auto? (list-ref info 16))
         (define margin-bottom-auto? (list-ref info 17))
         (define row-start (list-ref info 18))
         (define row-end (list-ref info 19))

         (define cview-w (view-width child-view))
         (define cview-h (view-height child-view))
         (define free-w (- cell-w cview-w ml mr))
         (define free-h (- cell-h cview-h mt mb))

         (define align-x
           (cond
             [(and margin-left-auto? margin-right-auto?)
              (+ ml (/ (max 0 free-w) 2))]
             [margin-left-auto?
              (+ ml (max 0 free-w))]
             [margin-right-auto?
              ml]
             [else
              (case ji
                [(align-start) ml]
                [(align-end) (- cell-w cview-w mr)]
                [(align-center) (+ ml (/ free-w 2))]
                [(align-stretch) ml]
                [else ml])]))
         (define align-y
           (cond
             [(and margin-top-auto? margin-bottom-auto?)
              (+ mt (/ (max 0 free-h) 2))]
             [margin-top-auto?
              (+ mt (max 0 free-h))]
             [margin-bottom-auto?
              mt]
             [else
              (case ai
                [(align-start) mt]
                [(align-end) (- cell-h cview-h mb)]
                [(align-center) (+ mt (/ free-h 2))]
                [(align-stretch) mt]
                [(align-baseline)
                 ;; baseline alignment: shift item down so its baseline aligns
                 ;; with the row's max baseline
                 (if (and (= (- row-end row-start) 1) (< row-start num-rows))
                     (let* ([outer-baseline (+ mt item-baseline)]
                            [row-max-bl (vector-ref row-max-baselines row-start)])
                       (- row-max-bl item-baseline))
                     mt)]  ; multi-row fallback to start
                [else mt])]))

         (cons dom-idx
               (let ([positioned-view
                      (set-view-pos child-view
                                    (+ offset-x item-x align-x)
                                    (+ offset-y item-y align-y))])
                 (apply-relative-offset positioned-view item-styles cell-w cell-h)))))

     ;; === Phase 8: Layout absolute children ===
     ;; if container has explicit width/height, use that; otherwise use content size
     ;; per CSS spec: when a percentage width resolves to 0 in a measurement context
     ;; (e.g., containing block is shrink-to-fit), treat width as auto and use
     ;; the intrinsic content-based width from track sizes
     (define css-width-val (get-style-prop styles 'width 'auto))
     (define width-is-pct-zero?
       (and (match css-width-val [`(% ,_) #t] [_ #f])
            (= content-w 0)))
     (define final-content-w
       (cond
         [(and has-explicit-w (not width-is-pct-zero?)) content-w]
         ;; indefinite container with percentage gap: use intrinsic width (gap=0 pass)
         ;; the re-resolved tracks + gaps overflow; container stays at intrinsic size
         [pct-gap-intrinsic-w (max content-w pct-gap-intrinsic-w)]
         ;; indefinite container with percentage tracks: use content-determined total
         ;; (percentage tracks resolve against this but don't inflate the container)
         [(and col-indefinite? has-pct-col-tracks?)
          (max content-w content-det-col-total)]
         ;; indefinite container with fr tracks: use the intrinsic width from the
         ;; first pass (before re-resolution), as the container width is determined
         ;; by intrinsic sizing, not by the re-resolved track sizes
         [(and intrinsic-col-total) (max content-w intrinsic-col-total)]
         [else (max content-w total-col-size)]))
     (define final-content-h
       (cond
         [explicit-h explicit-h]
         ;; indefinite container with percentage tracks: use content-determined total
         [(and row-indefinite? has-pct-row-tracks?) content-det-row-total]
         [else total-row-size]))

     ;; absolute children are positioned relative to the padding box
     (define padding-box-w (+ final-content-w
                              (box-model-padding-left bm) (box-model-padding-right bm)))
     (define padding-box-h (+ final-content-h
                              (box-model-padding-top bm) (box-model-padding-bottom bm)))

     ;; offset from border-box origin to padding-box origin
     (define abs-offset-x (box-model-border-left bm))
     (define abs-offset-y (box-model-border-top bm))

     ;; number of explicit track definitions (for grid-line resolution)
     (define num-explicit-cols (length col-defs))
     (define num-explicit-rows (length row-defs))

     ;; helper: compute position of an explicit grid line within the content area.
     ;; idx is a 0-based explicit grid line index (0 = before first explicit track,
     ;; num-explicit = after last explicit track).
     (define (explicit-grid-line-content-pos axis idx)
       (define tracks (if (eq? axis 'col) col-tracks row-tracks))
       (define offsets (if (eq? axis 'col) col-offsets row-offsets))
       (define gap (if (eq? axis 'col) col-gap row-gap))
       (define before (if (eq? axis 'col) col-before-count row-before-count))
       (define full-idx (+ before idx))
       (cond
         [(null? tracks) 0]
         [(< full-idx 0)
          (track-offset-with-alignment tracks 0 gap offsets)]
         [(>= full-idx (length tracks))
          (let ([last-idx (sub1 (length tracks))])
            (+ (track-offset-with-alignment tracks last-idx gap offsets)
               (track-base-size (list-ref tracks last-idx))))]
         [else
          (track-offset-with-alignment tracks full-idx gap offsets)]))

     ;; helper: resolve a CSS grid line value to a position within the padding box.
     ;; returns #f for auto/span (no definite grid line).
     (define (grid-line-to-pb-pos line-val axis)
       (define num-explicit (if (eq? axis 'col) num-explicit-cols num-explicit-rows))
       (define names (if (eq? axis 'col)
                         (get-style-prop styles 'grid-col-line-names '())
                         (get-style-prop styles 'grid-row-line-names '())))
       (define idx (resolve-line-value line-val num-explicit names))
       (and idx
            (let ([pad-start (if (eq? axis 'col)
                                 (box-model-padding-left bm)
                                 (box-model-padding-top bm))])
              (+ pad-start (explicit-grid-line-content-pos axis idx)))))

     (define abs-views
       (for/list ([child (in-list abs-children)]
                  [abs-idx (in-naturals)])
         (define dom-idx (list-ref abs-dom-indices abs-idx))
         (define child-styles (get-box-styles child))

         ;; === Containing block from grid placement (CSS Grid §9.2) ===
         ;; If a grid placement property specifies a line, that line is the
         ;; containing block edge.  If auto, the padding edge is used.
         (define gc-start (get-grid-line child-styles 'grid-column-start))
         (define gc-end   (get-grid-line child-styles 'grid-column-end))
         (define gr-start (get-grid-line child-styles 'grid-row-start))
         (define gr-end   (get-grid-line child-styles 'grid-row-end))

         (define cb-left   (or (grid-line-to-pb-pos gc-start 'col) 0))
         (define cb-right  (or (grid-line-to-pb-pos gc-end   'col) padding-box-w))
         (define cb-top    (or (grid-line-to-pb-pos gr-start 'row) 0))
         (define cb-bottom (or (grid-line-to-pb-pos gr-end   'row) padding-box-h))

         (define cb-w (max 0 (- cb-right cb-left)))
         (define cb-h (max 0 (- cb-bottom cb-top)))

         ;; lay out the child with the grid-line-based containing block
         (define abs-avail `(avail (definite ,cb-w) (definite ,cb-h)))
         (define raw-view (dispatch-fn child abs-avail))

         ;; === Alignment for axes where all insets are auto (CSS Grid §10.8) ===
         (define css-top-v    (get-style-prop child-styles 'top 'auto))
         (define css-bottom-v (get-style-prop child-styles 'bottom 'auto))
         (define css-left-v   (get-style-prop child-styles 'left 'auto))
         (define css-right-v  (get-style-prop child-styles 'right 'auto))

         (define h-inset? (or (not (eq? css-left-v 'auto))
                              (not (eq? css-right-v 'auto))))
         (define v-inset? (or (not (eq? css-top-v 'auto))
                              (not (eq? css-bottom-v 'auto))))

         (define vw (view-width raw-view))
         (define vh (view-height raw-view))

         ;; extract child margins for alignment (only when needed)
         (define child-bm-for-align
           (if (or (not h-inset?) (not v-inset?))
               (extract-box-model child-styles cb-w)
               #f))

         ;; horizontal position
         (define final-x
           (if h-inset?
               ;; insets determined position via layout-positioned
               (+ abs-offset-x cb-left (view-x raw-view))
               ;; no insets: apply justify-self alignment
               (let ()
                 (define ji (resolve-item-alignment child-styles 'justify-self justify-items))
                 (define ml (box-model-margin-left child-bm-for-align))
                 (define mr (box-model-margin-right child-bm-for-align))
                 (case ji
                   [(align-end)    (+ abs-offset-x cb-left (- cb-w vw mr))]
                   [(align-center) (+ abs-offset-x cb-left ml (/ (- cb-w vw ml mr) 2))]
                   [else           (+ abs-offset-x cb-left ml)]))))

         ;; vertical position
         (define final-y
           (if v-inset?
               (+ abs-offset-y cb-top (view-y raw-view))
               (let ()
                 (define ai (resolve-item-alignment child-styles 'align-self align-items))
                 (define mt (box-model-margin-top child-bm-for-align))
                 (define mb (box-model-margin-bottom child-bm-for-align))
                 (case ai
                   [(align-end)    (+ abs-offset-y cb-top (- cb-h vh mb))]
                   [(align-center) (+ abs-offset-y cb-top mt (/ (- cb-h vh mt mb) 2))]
                   [else           (+ abs-offset-y cb-top mt)]))))

         (define final-view (set-view-pos raw-view final-x final-y))
         (cons dom-idx final-view)))

     ;; === Phase 9: Compute final container size ===
     (define border-box-w (compute-border-box-width bm final-content-w))
     (define border-box-h (compute-border-box-height bm final-content-h))

     ;; merge flow and absolute child views in DOM source order
     (define all-indexed-views (append child-views abs-views))
     (define sorted-views
       (map cdr (sort all-indexed-views < #:key car)))

     (make-view id 0 0 border-box-w border-box-h sorted-views)]

    [_ (error 'layout-grid "expected grid box, got: ~a" box)]))

;; ============================================================
;; Content Alignment (align-content / justify-content for grid)
;; ============================================================

;; compute per-track offsets for content alignment
;; returns a vector of absolute offsets (from start of content area)
(define (compute-content-alignment-offsets alignment tracks gap total-available)
  (define n (length tracks))
  (if (= n 0)
      (make-vector 0)
      (let ()
        ;; count non-collapsed tracks for alignment purposes
        (define non-collapsed (filter (lambda (t) (> (track-base-size t) 0)) tracks))
        (define nc (length non-collapsed))
        (define total-tracks-size (tracks-total-size tracks))
        (define total-gaps (* gap (max 0 (sub1 nc))))
        (define content-size (+ total-tracks-size total-gaps))
        (define free-space (- total-available content-size))

        ;; map flex/content names to canonical form
        (define effective-align
          (case alignment
            [(flex-start content-start start) 'start]
            [(flex-end content-end end) 'end]
            [(center content-center) 'center]
            [(content-stretch stretch) 'stretch]
            [(space-between content-space-between) 'space-between]
            [(space-around content-space-around) 'space-around]
            [(space-evenly content-space-evenly) 'space-evenly]
            [else 'stretch]))

        ;; for stretch: distribute free space equally among auto-sized tracks only
        ;; fixed-size tracks (px, fr, %) are NOT stretched
        (when (and (eq? effective-align 'stretch) (> free-space 0))
          (define stretchable
            (filter (lambda (t)
                      (define ts (track-track-size t))
                      (eq? ts 'auto))
                    tracks))
          (when (not (null? stretchable))
            (define extra-per-track (/ free-space (length stretchable)))
            (for ([t (in-list stretchable)])
              (set-track-base-size! t (+ (track-base-size t) extra-per-track)))))

        ;; recompute after possible stretch
        (define adj-total (+ (tracks-total-size tracks) total-gaps))
        (define adj-free (- total-available adj-total))

        ;; compute start-offset and per-gap extra space
        ;; use nc (non-collapsed count) for spacing calculations
        (define-values (start-offset gap-extra)
          (case effective-align
            [(start stretch)
             (values 0 0)]
            [(end)
             (values adj-free 0)]
            [(center)
             (values (/ adj-free 2) 0)]
            [(space-between)
             (if (> nc 1)
                 (values 0 (if (> adj-free 0) (/ adj-free (sub1 nc)) 0))
                 (values 0 0))]
            [(space-around)
             (if (> nc 0)
                 (let ([sp (if (> adj-free 0) (/ adj-free nc) 0)])
                   (values (/ sp 2) sp))
                 (values 0 0))]
            [(space-evenly)
             (if (> nc 0)
                 (let ([sp (if (> adj-free 0) (/ adj-free (add1 nc)) 0)])
                   (values sp sp))
                 (values 0 0))]
            [else (values 0 0)]))

        ;; build per-track offset vector
        ;; collapsed tracks don't add gap
        (define offsets (make-vector n 0))
        (let loop ([i 0] [pos start-offset])
          (when (< i n)
            (vector-set! offsets i pos)
            (define t (list-ref tracks i))
            (define collapsed? (= (track-base-size t) 0))
            ;; only add gap between non-collapsed tracks
            (define this-gap (if collapsed? 0 gap))
            (define this-extra (if (and (not collapsed?) (< i (sub1 n))) gap-extra 0))
            (define next-pos (+ pos (track-base-size t) this-gap this-extra))
            (loop (add1 i) next-pos)))
        offsets)))

;; ============================================================
;; Item Alignment
;; ============================================================

;; resolve per-item alignment (self overrides container default)
(define (resolve-item-alignment item-styles self-prop container-default)
  (define self-val (get-style-prop item-styles self-prop 'self-auto))
  (case self-val
    [(self-auto) container-default]
    [(self-start) 'align-start]
    [(self-end) 'align-end]
    [(self-center) 'align-center]
    [(self-stretch) 'align-stretch]
    [(self-baseline) 'align-baseline]  ; pass through for per-row baseline computation
    [else container-default]))

;; ============================================================
;; Child Partitioning
;; ============================================================

(define (partition-children children)
  (define flow '())
  (define absolute '())
  (for ([child (in-list children)]
        [dom-idx (in-naturals)])
    (define styles (get-box-styles child))
    (define pos (get-style-prop styles 'position 'static))
    (if (or (eq? pos 'absolute) (eq? pos 'fixed))
        (set! absolute (cons (cons dom-idx child) absolute))
        (match child
          [`(none ,_) (void)]
          [_ (set! flow (cons (cons dom-idx child) flow))])))
  (values (reverse flow) (reverse absolute)))

;; ============================================================
;; Gap Resolution
;; ============================================================

(define (resolve-gap gap-val avail)
  (cond
    [(number? gap-val) gap-val]
    [(and (pair? gap-val) (eq? (car gap-val) '%))
     (if (and avail (number? avail) (not (infinite? avail)))
         (* (/ (cadr gap-val) 100) avail)
         0)]
    [else 0]))

;; ============================================================
;; Track Creation
;; ============================================================

(define (create-tracks defs)
  (for/list ([def (in-list defs)]
             [i (in-naturals)])
    (track i def 0 +inf.0)))

;; ensure we have at least n tracks (add auto or grid-auto-* tracks)
(define (ensure-tracks tracks n auto-defs)
  (define current (length tracks))
  (if (>= current n)
      tracks
      (append tracks
              (for/list ([i (in-range current n)])
                (define def
                  (if (null? auto-defs)
                      'auto
                      (list-ref auto-defs (modulo (- i current) (length auto-defs)))))
                (track i def 0 +inf.0)))))

;; ============================================================
;; Track Size Resolution
;; ============================================================

(define (resolve-track-sizes! tracks available items axis dispatch-fn avail col-gap row-gap [cross-avail +inf.0] [containing-width #f] [skip-maximize? #f] [cross-tracks #f] [cross-gap 0] [auto-container-size? #f])
  ;; flag: is the available space indefinite? (auto-sizing container)
  (define indefinite? (or (infinite? available) (<= available 0)))
  ;; phase 1: initialize track sizes from definitions
  (for ([t (in-list tracks)])
    (define raw-ts (track-track-size t))
    ;; unwrap auto-fit-track wrapper (keep for later collapse detection)
    (define ts (match raw-ts
                 [`(auto-fit-track ,inner) inner]
                 [_ raw-ts]))
    (match ts
      [`(px ,n)
       (set-track-base-size! t n)
       (set-track-growth-limit! t n)]
      [`(% ,pct)
       (if indefinite?
           ;; indefinite container: treat percentage tracks as auto for intrinsic sizing
           (begin
             (set-track-base-size! t 0)
             (set-track-growth-limit! t +inf.0))
           ;; definite container: resolve percentage against available space
           (let ([resolved (* (/ pct 100) available)])
             (set-track-base-size! t resolved)
             (set-track-growth-limit! t resolved)))]
      ['auto
       (set-track-base-size! t 0)
       (set-track-growth-limit! t +inf.0)]
      ['min-content
       (set-track-base-size! t 0)
       (set-track-growth-limit! t 0)]
      ['max-content
       (set-track-base-size! t 0)
       (set-track-growth-limit! t +inf.0)]
      [`(minmax ,min-sv ,max-sv)
       (define min-v (or (resolve-track-value min-sv available) 0))
       (define max-v (or (resolve-track-value max-sv available) +inf.0))
       ;; per CSS Grid spec: when the container auto-sizes (available ≤ 0 or infinite)
       ;; and both min and max are definite, use max as the base size
       (define base-v
         (if (and (or (infinite? available) (<= available 0))
                  (not (infinite? max-v)))
             max-v
             min-v))
       (set-track-base-size! t base-v)
       (set-track-growth-limit! t max-v)]
      [`(fr ,_)
       (set-track-base-size! t 0)
       (set-track-growth-limit! t +inf.0)]
      ;; fit-content(limit): behaves like minmax(auto, min(max-content, limit))
      ;; base size starts at 0 (will grow from content contributions)
      ;; growth limit starts infinite; Phase 2 Step 2 resolves it to
      ;; min(max-content, limit), then the §12.4 floor ensures >= base
      [`(fit-content ,limit-sv)
       (set-track-base-size! t 0)
       (set-track-growth-limit! t +inf.0)]
      [_ (void)]))

  ;; helper to extract fr value from a track (used in phases 2.5 and 4)
  (define (get-fr-value t)
    (match (track-track-size t)
      [`(fr ,n) n]
      [`(auto-fit-track (fr ,n)) n]
      [`(minmax ,_ (fr ,n)) n]
      [`(auto-fit-track (minmax ,_ (fr ,n))) n]
      [_ 0]))

  ;; helper: check if a grid item has overflow != visible in the given axis
  ;; Per CSS Grid §6.6: items with overflow other than visible have automatic
  ;; minimum size of 0 (their content can be clipped, so no min-content floor).
  (define (item-has-scroll-overflow? item axis)
    (define styles (grid-item-styles item))
    (define overflow-prop (get-style-prop styles 'overflow 'visible))
    (define overflow-axis
      (if (eq? axis 'col)
          (get-style-prop styles 'overflow-x overflow-prop)
          (get-style-prop styles 'overflow-y overflow-prop)))
    (not (eq? overflow-axis 'visible)))

  ;; helper: compute the actual cross-axis span size for a grid item.
  ;; When cross-tracks are provided (row-axis sizing with resolved column tracks),
  ;; the item's available width is the sum of its column track base sizes + gaps.
  ;; This ensures text wraps correctly within its actual column span.
  (define (item-cross-avail item)
    (if (and cross-tracks (eq? axis 'row))
        (let* ([cs (grid-item-col-start item)]
               [ce (grid-item-col-end item)]
               [span-width
                (for/sum ([i (in-range cs (min ce (length cross-tracks)))])
                  (track-base-size (list-ref cross-tracks i)))]
               [span-gaps (* cross-gap (max 0 (- (min ce (length cross-tracks)) cs 1)))])
          (+ span-width span-gaps))
        cross-avail))

  ;; phase 2: size items spanning single tracks
  ;; Step 1: update base sizes using intrinsic contributions
  ;; Per CSS Grid §12.5: for tracks with intrinsic minimum sizing function:
  ;;  - min-content → base = min-content contribution
  ;;  - max-content → base = max-content contribution
  ;;  - auto → base = automatic minimum (0 for overflow:hidden, else min-content)
  (for ([item (in-list items)])
    (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
    (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
    (when (and (= (- end start) 1) (< start (length tracks)))
      (define t (list-ref tracks start))
      (define ts (track-track-size t))
      ;; determine the minimum sizing function
      ;; Per CSS Grid §12.5: fr = minmax(auto, <flex>), so min-func is auto
      (define min-func
        (match ts
          ['auto 'auto]
          ['min-content 'min-content]
          ['max-content 'max-content]
          [`(minmax ,mn ,_) mn]
          [`(fr ,_) 'auto]  ; fr = minmax(auto, <flex>), min-func is auto
          [`(fit-content ,_) 'auto]  ; fit-content = minmax(auto, min(max-content, limit))
          [`(% ,_) (if indefinite? 'auto #f)]
          [_ #f]))
      (when min-func
        ;; use max-content measurement when the minimum function is max-content
        (define use-max? (eq? min-func 'max-content))
        ;; Per CSS Grid §6.6: for auto min, overflow != visible → automatic minimum = 0
        (define item-size
          (cond
            [(and (eq? min-func 'auto) (item-has-scroll-overflow? item axis)) 0]
            [use-max?
             (measure-grid-item-max item axis dispatch-fn avail (item-cross-avail item) containing-width)]
            [else
             (measure-grid-item-min item axis dispatch-fn avail (item-cross-avail item) containing-width)]))
        ;; Per CSS Grid §12.5: for auto minimum with finite growth-limit,
        ;; clamp to growth-limit: base = min(contribution, growth-limit).
        ;; For min-content/max-content minimum, NO clamping — the §12.4 floor
        ;; step will increase growth-limit to match if base exceeds it.
        (define effective-size
          (if (and (eq? min-func 'auto)
                   (not (infinite? (track-growth-limit t))))
              (min item-size (track-growth-limit t))
              item-size))
        (when (> effective-size (track-base-size t))
          (set-track-base-size! t effective-size)))))

  ;; Step 2: resolve growth limits using intrinsic max sizing contributions
  ;; Per CSS Grid §12.5: for tracks with intrinsic maximum sizing function:
  ;;  - auto / max-content → growth limit = max-content contribution
  ;;  - min-content → growth limit = min-content contribution
  ;;  - fit-content(limit) → growth limit = min(max-content, limit)
  (for ([item (in-list items)])
    (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
    (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
    (when (and (= (- end start) 1) (< start (length tracks)))
      (define t (list-ref tracks start))
      (define ts (track-track-size t))
      ;; determine the maximum sizing function
      ;; per CSS Grid §12.5: when container is indefinite, % is treated as auto
      (define max-func
        (match ts
          ['auto 'auto]
          ['min-content 'min-content]
          ['max-content 'max-content]
          [`(minmax ,_ ,mx)
           (if (and indefinite? (pair? mx) (eq? (car mx) '%))
               'auto  ; % treated as auto in indefinite context
               mx)]
          [`(fit-content ,_) 'fit-content]
          [`(% ,_) (if indefinite? 'auto #f)]
          [_ #f]))
      ;; intrinsic max functions: auto, max-content, min-content, fit-content
      (define needs-max?
        (and max-func
             (or (eq? max-func 'auto) (eq? max-func 'max-content)
                 (eq? max-func 'min-content)
                 (eq? max-func 'fit-content))))
      (when needs-max?
        ;; for min-content max, use min-content contribution; otherwise max-content
        (define use-min-for-limit? (eq? max-func 'min-content))
        (define item-contrib
          (if use-min-for-limit?
              (measure-grid-item-min item axis dispatch-fn avail (item-cross-avail item) containing-width)
              (measure-grid-item-max item axis dispatch-fn avail (item-cross-avail item) containing-width)))
        ;; for fit-content, cap growth limit at the argument
        (define effective-max
          (if (and (pair? ts) (eq? (car ts) 'fit-content))
              (let ([limit-v (resolve-track-value (cadr ts) available)])
                (if limit-v (min item-contrib limit-v) item-contrib))
              item-contrib))
        ;; Update growth-limit: take max with current (handles multiple items).
        ;; For the first item (growth still infinite from Phase 1), always set.
        ;; For subsequent items, only update if contribution is larger.
        (define current-gl (track-growth-limit t))
        (when (or (infinite? current-gl) (> effective-max current-gl))
          (set-track-growth-limit! t effective-max)))))
  ;; per CSS Grid §12.4: "If the growth limit is less than the base size, increase
  ;; the growth limit to match the base size."
  (for ([t (in-list tracks)])
    (when (and (not (infinite? (track-growth-limit t)))
               (< (track-growth-limit t) (track-base-size t)))
      (set-track-growth-limit! t (track-base-size t))))

  ;; phase 2.5: distribute spanning items across multiple tracks
  ;; Per CSS Grid §12.5: for each group of items spanning N tracks (smallest N first):
  ;; Step 1: increase base sizes from min-content contributions
  ;; Step 2: increase growth limits from max-content contributions
  (define spanning-items
    (sort
     (filter (lambda (item)
               (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
               (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
               (> (- end start) 1))
             items)
     < #:key (lambda (item)
               (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
               (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
               (- end start))))

  (for ([item (in-list spanning-items)])
    (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
    (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
    (define span-tracks
      (for/list ([i (in-range start (min end (length tracks)))])
        (list-ref tracks i)))
    (when (not (null? span-tracks))
      (define gap-total (* (max 0 (- (length span-tracks) 1))
                           (if (eq? axis 'col) col-gap row-gap)))

      ;; Step 1: increase base sizes from intrinsic minimum contributions
      ;; Per CSS Grid §12.5, three sub-steps:
      ;; 3.1 Intrinsic minimums: distribute to tracks with min-content/max-content min
      ;;     using item's min-content contribution (unaffected by overflow)
      ;; 3.2 Content-based minimums: distribute to tracks with auto min
      ;;     using item's automatic minimum (0 for overflow:hidden)
      ;; 3.3 Max-content minimums: distribute to tracks with max-content min
      ;;     using item's max-content contribution
      ;; Helper: get the min sizing function of a track
      (define (track-min-func t)
        (define ts (track-track-size t))
        (match ts
          ['auto 'auto]
          ['min-content 'min-content]
          ['max-content 'max-content]
          [`(minmax ,mn ,_) mn]
          [`(fit-content ,_) 'auto]
          [`(fr ,_) 'auto]  ; fr = minmax(auto, <flex>), min-func is auto
          [`(% ,_) (if indefinite? 'auto #f)]
          [_ #f]))

      ;; Per CSS Grid §12.5.2: sub-steps for spanning item base-size distribution
      ;; Sub-step 1 (intrinsic minimums): targets = min-content/max-content min tracks
      ;; Sub-step 2 (content-based minimums): targets = auto min tracks, EXCLUDING flexible
      ;; Sub-step 3 (max-content minimums): targets = max-content min tracks
      (define min-content-min-tracks
        (filter (lambda (t)
                  (define mf (track-min-func t))
                  (or (eq? mf 'min-content) (eq? mf 'max-content)))
                span-tracks))
      ;; auto-min tracks: includes fr tracks (they have auto min-func)
      (define auto-min-tracks
        (filter (lambda (t) (eq? (track-min-func t) 'auto)) span-tracks))
      ;; non-flexible auto tracks: auto min tracks that are NOT fr
      ;; Per CSS Grid §12.5.2: "excluding flexible tracks" in content-based minimums
      ;; Fr tracks are handled by Phase 4's flex fraction algorithm, which
      ;; distributes proportionally rather than equally.
      (define non-flexible-auto-min-tracks
        (filter (lambda (t) (not (fr-track? t))) auto-min-tracks))

      ;; determine item contribution based on overflow for auto tracks
      (define item-min-size (measure-grid-item-min item axis dispatch-fn avail (item-cross-avail item) containing-width))
      (define item-is-scroll? (item-has-scroll-overflow? item axis))
      (define item-auto-min (if item-is-scroll? 0 item-min-size))

      ;; Determine distribution targets per CSS Grid §12.5.2:
      ;; - Intrinsic minimums (min-content/max-content min) + content-based (auto, excl flexible)
      ;; - For overflow:hidden items, exclude auto tracks (their automatic min = 0)
      (define intrinsic-min-targets
        (append min-content-min-tracks non-flexible-auto-min-tracks))
      (define base-growable
        (cond
          ;; overflow:hidden: exclude auto tracks (their automatic min = 0)
          ;; distribute only to min-content/max-content tracks
          [(and item-is-scroll? (not (null? min-content-min-tracks)))
           min-content-min-tracks]
          ;; has intrinsic (non-flexible) min targets → distribute to them
          [(not (null? intrinsic-min-targets))
           intrinsic-min-targets]
          ;; if span contains fr tracks, skip distribution (Phase 4 handles them)
          [(ormap fr-track? span-tracks)
           '()]
          ;; fallback: all tracks (e.g., when all tracks are fixed and non-intrinsic)
          [else span-tracks]))

      ;; compute effective item contribution:
      ;; for overflow:hidden with only auto/fr/fixed tracks → use automatic minimum (0)
      ;; otherwise → use min-content contribution
      (define effective-min-contribution
        (if (and item-is-scroll? (null? min-content-min-tracks))
            item-auto-min  ; no min-content tracks → use automatic minimum
            item-min-size)) ; has min-content/max-content tracks → use min-content

      (define current-base-sum (for/sum ([t (in-list span-tracks)]) (track-base-size t)))
      (define base-extra (- effective-min-contribution current-base-sum gap-total))
      (when (and (> base-extra 0) (not (null? base-growable)))
        ;; Per CSS Grid §12.7.1 "distribute extra space" sub-algorithm:
        ;; 1. Filter to non-frozen tracks (base < growth-limit)
        ;; 2. Distribute equally, capping at growth-limit
        ;; 3. Repeat with remaining extra if any tracks were capped
        ;; 4. If no growable tracks remain, distribute to all targets equally
        ;;    (growth-limits become "infinitely growable" for spanning items)
        (define (distribute-base-extra! targets extra)
          ;; separate frozen (base >= finite growth-limit) from growable
          (define growable
            (filter (lambda (t)
                      (or (infinite? (track-growth-limit t))
                          (< (track-base-size t) (track-growth-limit t))))
                    targets))
          (cond
            [(null? growable)
             ;; all target tracks frozen — check if flexible tracks exist in span
             ;; For INDEFINITE case: fr tracks are excluded from targets, so if fr exists
             ;; in span, discard extra (Phase 4's flex fraction handles it proportionally).
             ;; For DEFINITE case: fr tracks ARE in targets, so distribute beyond limits.
             (define has-fr-in-span? (ormap fr-track? span-tracks))
             (unless has-fr-in-span?
               (define per-track (/ extra (length targets)))
               (for ([t (in-list targets)])
                 (set-track-base-size! t (+ (track-base-size t) per-track))))]
            [else
             ;; distribute to growable tracks, capping at growth-limit
             (define remaining extra)
             (define newly-frozen '())
             (for ([t (in-list growable)])
               (define share (/ extra (length growable)))
               (define room
                 (if (infinite? (track-growth-limit t))
                     share
                     (- (track-growth-limit t) (track-base-size t))))
               (define actual (min share room))
               (set-track-base-size! t (+ (track-base-size t) actual))
               (set! remaining (- remaining actual))
               (when (< actual share)
                 (set! newly-frozen (cons t newly-frozen))))
             ;; if capped tracks left extra, redistribute
             (when (and (> remaining 0.001) (not (null? newly-frozen)))
               (define still-growable
                 (filter (lambda (t) (not (memq t newly-frozen))) growable))
               (if (not (null? still-growable))
                   (distribute-base-extra! still-growable remaining)
                   ;; all growable frozen: apply same fr-discard logic
                   (let ([has-fr? (ormap fr-track? span-tracks)])
                     (unless has-fr?
                       (let ([per-track (/ remaining (length targets))])
                         (for ([t (in-list targets)])
                           (set-track-base-size! t (+ (track-base-size t) per-track))))))))]))
        (distribute-base-extra! base-growable base-extra))

      ;; Per CSS Grid §12.4: maintain invariant growth-limit >= base-size
      ;; after Step 1 increases base sizes, floor growth limits before Step 2
      ;; so that the extra space computation correctly accounts for base-size increases
      (for ([t (in-list span-tracks)])
        (when (and (not (infinite? (track-growth-limit t)))
                   (< (track-growth-limit t) (track-base-size t)))
          (set-track-growth-limit! t (track-base-size t))))

      ;; Step 2: increase growth limits from max-content contribution
      ;; Per CSS Grid §12.5: distribute extra space to tracks with growable
      ;; max sizing function (auto, max-content, fit-content). Min-content max
      ;; tracks are excluded — their growth limits come only from min-content
      ;; contributions and shouldn't grow from spanning max-content.
      ;; Priority order: max-content → auto/fit-content
      ;; fit-content tracks are capped at their argument.
      ;; IMPORTANT: when the span crosses flex tracks, skip growth-limit distribution
      ;; to non-fr tracks. Phase 4's find-fr-size algorithm handles the proportional
      ;; distribution holistically, accounting for both fr and non-fr tracks.
      ;; Without this guard, non-fr tracks absorb all the spanning item's contribution
      ;; in growth limits, leaving nothing for fr tracks in find-fr-size (the non-fr
      ;; base sizes after maximize would equal the item's max-content contribution).
      (define span-has-fr? (ormap fr-track? span-tracks))
      (define item-max-size (measure-grid-item-max item axis dispatch-fn avail (item-cross-avail item) containing-width))
      ;; resolve effective growth-limit for sum (use base-size for infinite)
      (define (effective-gl t)
        (if (infinite? (track-growth-limit t))
            (track-base-size t)
            (track-growth-limit t)))
      (define total-limit-sum (for/sum ([t (in-list span-tracks)]) (effective-gl t)))
      (define total-limit-extra (- item-max-size total-limit-sum gap-total))
      (when (and (> total-limit-extra 0) (not span-has-fr?))
        ;; set any infinite growth-limits to base-size before distributing
        (for ([t (in-list span-tracks)])
          (when (infinite? (track-growth-limit t))
            (set-track-growth-limit! t (track-base-size t))))
        ;; determine which tracks can receive growth-limit increases
        ;; exclude min-content max tracks (they only get min-content contributions)
        (define (has-growable-max? t)
          (define ts (track-track-size t))
          (match ts
            ['auto #t]
            ['max-content #t]
            [`(fit-content ,_) #t]
            [`(minmax ,_ auto) #t]
            [`(minmax ,_ max-content) #t]
            [`(% ,_) indefinite?]  ; % → auto in indefinite
            [_ #f]))
        ;; get the fit-content cap for a track (or +inf.0 if not fit-content)
        (define (fit-content-cap t)
          (match (track-track-size t)
            [`(fit-content ,limit-sv)
             (or (resolve-track-value limit-sv available) +inf.0)]
            [_ +inf.0]))
        ;; classify growable tracks by priority
        (define (get-max-priority t)
          (define ts (track-track-size t))
          (match ts
            ['max-content 1]
            [`(minmax ,_ max-content) 1]
            ['auto 2]
            [`(minmax ,_ auto) 2]
            [`(fit-content ,_) 2]
            [`(% ,_) 2]  ; treated as auto in indefinite
            [_ 3]))
        (define growable-tracks (filter has-growable-max? span-tracks))
        ;; distribute in priority order with fit-content caps
        (define remaining-extra total-limit-extra)
        (for ([priority (in-list '(1 2 3))])
          (when (> remaining-extra 0)
            (define targets
              (filter (lambda (t) (= (get-max-priority t) priority)) growable-tracks))
            (when (not (null? targets))
              ;; distribute with iterative freeze for fit-content caps
              (let loop ([active targets] [extra remaining-extra])
                (when (and (> extra 0) (not (null? active)))
                  (define per-track (/ extra (length active)))
                  (define frozen '())
                  (define used 0)
                  (for ([t (in-list active)])
                    (define old-gl (track-growth-limit t))
                    (define cap (fit-content-cap t))
                    (define room (max 0 (- cap old-gl)))
                    (cond
                      [(<= per-track room)
                       ;; can absorb full share
                       (set-track-growth-limit! t (+ old-gl per-track))
                       (set! used (+ used per-track))]
                      [else
                       ;; capped at fit-content limit
                       (set-track-growth-limit! t (max old-gl cap))
                       (set! used (+ used (max 0 room)))
                       (set! frozen (cons t frozen))]))
                  (set! remaining-extra (- extra used))
                  ;; if any froze, redistribute excess to remaining unfrozen
                  (when (not (null? frozen))
                    (define unfrozen
                      (filter (lambda (t) (not (memq t frozen))) active))
                    (when (not (null? unfrozen))
                      (loop unfrozen remaining-extra)))))))))

    ;; ensure growth-limit >= base-size after spanning distribution
    (for ([t (in-list span-tracks)])
      (when (and (not (infinite? (track-growth-limit t)))
                 (< (track-growth-limit t) (track-base-size t)))
        (set-track-growth-limit! t (track-base-size t))))))

  ;; phase 3: grow non-fr tracks up to their growth limit
  ;; When available space is indefinite (intrinsic sizing), grow auto/max-content
  ;; tracks to their growth limits directly (CSS Grid §12.5: "maximize tracks").
  ;; For auto tracks, growth-limit = max-content contribution from Phase 2 Step 2.
  ;; skip-maximize? suppresses this for min-content sizing: the grid's min-content
  ;; width is the sum of the min-content base sizes, not the maximized growth-limits.
  (when (and indefinite? (not skip-maximize?))
    (for ([t (in-list tracks)])
      (when (and (not (fr-track? t))
                 (not (infinite? (track-growth-limit t)))
                 (< (track-base-size t) (track-growth-limit t)))
        (set-track-base-size! t (track-growth-limit t)))))
  ;; When available space is definite, distribute free space equally
  (when (and (not (infinite? available))
             (> available 0)
             (> (- available (tracks-total-size tracks)) 0))
    (let loop ()
      (define remaining (- available (tracks-total-size tracks)))
      (when (> remaining 0.001)
        (define growable
          (filter (lambda (t)
                    (and (not (fr-track? t))
                         (not (infinite? (track-growth-limit t)))
                         (< (track-base-size t) (track-growth-limit t))))
                  tracks))
        (when (not (null? growable))
          (define per-track (/ remaining (length growable)))
          (define grew? #f)
          (for ([t (in-list growable)])
            (define room (- (track-growth-limit t) (track-base-size t)))
            (define grow-by (min room per-track))
            (when (> grow-by 0)
              (set-track-base-size! t (+ (track-base-size t) grow-by))
              (set! grew? #t)))
          (when grew? (loop))))))

  ;; "Find the size of an fr" sub-algorithm (CSS Grid §12.7.1)
  ;; Given a set of tracks and a space to fill, returns the hypothetical fr size.
  ;; Note: the "max(sum, 1)" floor is NOT applied here — that rule only applies
  ;; in the Phase 4 definite-case loop to prevent over-distribution.
  ;; For find-fr-size (used in indefinite per-item computation), we use the raw
  ;; sum so that items fit their space (e.g., 0.2fr+0.3fr spanning 60px → fr=120).
  (define (find-fr-size given-tracks space)
    (define non-flex-total
      (for/sum ([t (in-list given-tracks)]
                #:when (not (fr-track? t)))
        (track-base-size t)))
    (define leftover (max 0 (- space non-flex-total)))
    (let loop ([active (filter fr-track? given-tracks)]
               [space leftover])
      (define sum-fr (for/sum ([t (in-list active)]) (get-fr-value t)))
      (if (<= sum-fr 0)
          0
          (let* ([hyp (/ space sum-fr)])
            (define-values (frozen unfrozen)
              (partition (lambda (t) (> (track-base-size t) (* (get-fr-value t) hyp)))
                         active))
            (if (null? frozen)
                hyp
                (let ([frozen-total (for/sum ([t (in-list frozen)]) (track-base-size t))])
                  (loop unfrozen (max 0 (- space frozen-total)))))))))

  ;; phase 4: distribute fr units (CSS Grid §11.7: "Expand Flexible Tracks")
  (define total-fr
    (for/sum ([t (in-list tracks)]
              #:when (fr-track? t))
      (get-fr-value t)))

  (when (> total-fr 0)
    (cond
      ;; === Indefinite container: compute used flex fraction directly (§11.7) ===
      [indefinite?
       ;; Per-track contributions: if fr > 1, base/fr; else base (§11.7)
       (define flex-fraction-from-tracks
         (for/fold ([best 0]) ([t (in-list tracks)] #:when (fr-track? t))
           (define fv (get-fr-value t))
           (max best (if (> fv 1) (/ (track-base-size t) fv) (track-base-size t)))))
       ;; Per-item contributions: for ALL items crossing flexible tracks,
       ;; run "find the size of an fr" with those tracks and the item's max-content
       ;; Per CSS Grid §12.7.1: "For each grid item that crosses a flexible track,
       ;; the result of finding the size of an fr using all the grid tracks that the
       ;; item crosses and a space to fill of the item's max-content contribution."
       (define flex-fraction-from-items
         (for/fold ([best 0]) ([item (in-list items)])
           (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
           (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
           (if (< start (length tracks))
               (let* ([item-tracks
                       (for/list ([i (in-range start (min end (length tracks)))])
                         (list-ref tracks i))]
                      [has-flex? (ormap fr-track? item-tracks)])
                 (if has-flex?
                     (let* ([item-size (measure-grid-item-max item axis dispatch-fn avail (item-cross-avail item) containing-width)]
                            [fr-size (find-fr-size item-tracks item-size)])
                       (max best fr-size))
                     best))
               best)))
       ;; Used flex fraction = max of per-track and per-item contributions
       (define used-flex-fraction (max flex-fraction-from-tracks flex-fraction-from-items))
       ;; Apply directly — no freeze loop for indefinite (§11.7)
       (for ([t (in-list tracks)] #:when (fr-track? t))
         (define fv (get-fr-value t))
         (define new-size (* fv used-flex-fraction))
         (when (> new-size (track-base-size t))
           (set-track-base-size! t new-size)
           (set-track-growth-limit! t new-size)))]
      ;; === Definite container: "Find the size of an fr" with freeze loop (§11.7.1) ===
      [else
       (define fixed-total
         (for/sum ([t (in-list tracks)]
                   #:when (not (fr-track? t)))
           (track-base-size t)))
       (define fr-space (max 0 (- available fixed-total)))
       ;; iterative freeze algorithm (§12.7.1)
       (let loop ([active-tracks (filter fr-track? tracks)]
                  [space fr-space])
         (define sum-fr (for/sum ([t (in-list active-tracks)]) (get-fr-value t)))
         (when (> sum-fr 0)
           ;; CSS Grid §11.7.1: "if the sum of flex factors < 1, set to 1"
           ;; This prevents sub-1 sums from exceeding available space in
           ;; explicitly-sized grids. However, for auto-width grids (where the
           ;; definite available was derived from the intrinsic sizing result),
           ;; the raw sum must be used so tracks fill the content-determined width.
           (define effective-sum (if auto-container-size? sum-fr (max sum-fr 1)))
           (define hyp-fr-size (/ space effective-sum))
           ;; partition into frozen (base > proportional share) and unfrozen
           (define-values (frozen unfrozen)
             (partition (lambda (t) (> (track-base-size t) (* (get-fr-value t) hyp-fr-size)))
                        active-tracks))
           (cond
             [(null? frozen)
              ;; no more tracks need freezing: assign final fr sizes
              (for ([t (in-list active-tracks)])
                (define size (max (track-base-size t) (* (get-fr-value t) hyp-fr-size)))
                (set-track-base-size! t size)
                (set-track-growth-limit! t size))]
             [else
              ;; freeze oversize tracks at base-size, redistribute remaining space
              (for ([t (in-list frozen)])
                (set-track-growth-limit! t (track-base-size t)))
              (define frozen-total (for/sum ([t (in-list frozen)]) (track-base-size t)))
              (loop unfrozen (max 0 (- space frozen-total)))])))]))

  ;; phase 5: maximize remaining auto tracks
  ;; per CSS Grid spec 12.5: distribute free space equally among ALL auto tracks
  (define auto-tracks
    (filter (lambda (t) (eq? (track-track-size t) 'auto))
            tracks))
  (when (and (not (null? auto-tracks))
             (not (infinite? available))
             (> (- available (tracks-total-size tracks)) 0))
    (define remaining (- available (tracks-total-size tracks)))
    (define per-track (/ remaining (length auto-tracks)))
    (for ([t (in-list auto-tracks)])
      (set-track-base-size! t (+ (track-base-size t) per-track)))))

;; resolve a track-size value to pixels (for minmax arguments)
(define (resolve-track-value sv available)
  (match sv
    [`(px ,n) n]
    [`(% ,pct) (if (and (number? available) (> available 0) (not (infinite? available)))
                   (* (/ pct 100) available)
                   #f)]
    [`(fr ,_) #f]
    ['auto #f]
    ['min-content #f]
    ['max-content #f]
    [(? number?) sv]
    [_ #f]))

(define (fr-track? t)
  (match (track-track-size t)
    [`(fr ,_) #t]
    [`(auto-fit-track (fr ,_)) #t]
    [`(minmax ,_ (fr ,_)) #t]
    [`(auto-fit-track (minmax ,_ (fr ,_))) #t]
    [_ #f]))

;; check if a track is an auto-fit track
(define (auto-fit-track? t)
  (match (track-track-size t)
    [`(auto-fit-track ,_) #t]
    [_ #f]))

;; collapse empty auto-fit tracks to 0 size
(define (collapse-empty-auto-fit-tracks! tracks items axis)
  (for ([t (in-list tracks)])
    (when (auto-fit-track? t)
      ;; check if any item spans this track
      (define idx (track-index t))
      (define occupied?
        (for/or ([item (in-list items)])
          (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
          (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
          (and (>= idx start) (< idx end))))
      (unless occupied?
        (set-track-base-size! t 0)
        (set-track-growth-limit! t 0)))))

;; measure minimum size of a grid item in the given axis
;; cross-avail: definite cross-axis available size (e.g. container height when sizing columns)
;; allows percentage sizes and aspect-ratio to resolve during intrinsic sizing
(define (measure-grid-item-min item axis dispatch-fn avail [cross-avail +inf.0] [containing-width #f])
  (define child-avail
    (if (and (number? cross-avail) (not (infinite? cross-avail)))
        (if (eq? axis 'col)
            `(avail av-min-content (definite ,cross-avail))
            `(avail (definite ,cross-avail) av-min-content))
        `(avail av-min-content av-min-content)))
  (define view (dispatch-fn (grid-item-box item) child-avail))
  (define border-box-size (if (eq? axis 'col) (view-width view) (view-height view)))
  ;; CSS aspect-ratio: when measuring row contribution, if the item has
  ;; aspect-ratio and no explicit height, derive height from the laid-out width.
  ;; aspect-ratio = width/height, so height = width / aspect-ratio
  (define adjusted-size
    (if (eq? axis 'row)
        (let ([ar (get-style-prop (grid-item-styles item) 'aspect-ratio #f)])
          (if (and ar (number? ar) (> ar 0)
                   (not (has-explicit-size? (grid-item-styles item) 'height)))
              (let ([item-w (view-width view)])
                (max border-box-size (/ item-w ar)))
              border-box-size))
        border-box-size))
  ;; apply min/max width/height constraints per CSS Grid spec
  ;; the contribution is the outer size clamped by specified min-width/max-width
  (define clamped-size (clamp-item-contribution adjusted-size (grid-item-styles item) axis containing-width))
  ;; add item margins to get the outer size for track contribution.
  ;; Per CSS Grid §11.5: percentage margins resolve against the grid area's inline
  ;; size IF definite, otherwise treat as 0. During column track sizing the grid area
  ;; is unknown, so use 0. During row track sizing, cross-avail = column span width.
  (define margin-resolve-base
    (if (eq? axis 'col) 0 (or containing-width 0)))
  (define item-bm (extract-box-model (grid-item-styles item) margin-resolve-base))
  (define margin-sum
    (if (eq? axis 'col)
        (+ (box-model-margin-left item-bm) (box-model-margin-right item-bm))
        (+ (box-model-margin-top item-bm) (box-model-margin-bottom item-bm))))
  (+ clamped-size margin-sum))

;; measure maximum (max-content) size of a grid item in the given axis
;; used for resolving growth limits of auto and max-content tracks
(define (measure-grid-item-max item axis dispatch-fn avail [cross-avail +inf.0] [containing-width #f])
  (define child-avail
    (if (and (number? cross-avail) (not (infinite? cross-avail)))
        (if (eq? axis 'col)
            `(avail av-max-content (definite ,cross-avail))
            `(avail (definite ,cross-avail) av-max-content))
        `(avail av-max-content av-max-content)))
  (define view (dispatch-fn (grid-item-box item) child-avail))
  (define border-box-size (if (eq? axis 'col) (view-width view) (view-height view)))
  ;; CSS aspect-ratio: same as measure-grid-item-min
  (define adjusted-size
    (if (eq? axis 'row)
        (let ([ar (get-style-prop (grid-item-styles item) 'aspect-ratio #f)])
          (if (and ar (number? ar) (> ar 0)
                   (not (has-explicit-size? (grid-item-styles item) 'height)))
              (let ([item-w (view-width view)])
                (max border-box-size (/ item-w ar)))
              border-box-size))
        border-box-size))
  ;; apply min/max width/height constraints
  (define clamped-size (clamp-item-contribution adjusted-size (grid-item-styles item) axis containing-width))
  ;; Per CSS Grid §11.5: percentage margins resolve against the grid area if definite,
  ;; otherwise 0. During column sizing, grid area unknown → use 0.
  (define margin-resolve-base
    (if (eq? axis 'col) 0 (or containing-width 0)))
  (define item-bm (extract-box-model (grid-item-styles item) margin-resolve-base))
  (define margin-sum
    (if (eq? axis 'col)
        (+ (box-model-margin-left item-bm) (box-model-margin-right item-bm))
        (+ (box-model-margin-top item-bm) (box-model-margin-bottom item-bm))))
  (+ clamped-size margin-sum))

;; clamp a border-box size by the item's min-width/max-width or min-height/max-height
;; per CSS Grid spec, contributions are clamped by specified constraints
(define (clamp-item-contribution border-box-size styles axis [containing-width #f])
  (define resolve-base (or containing-width 0))
  (if (eq? axis 'col)
      (let* ([min-w-raw (get-style-prop styles 'min-width 'auto)]
             [max-w-raw (get-style-prop styles 'max-width 'none)]
             [min-w (or (resolve-size-value min-w-raw resolve-base) 0)]
             [max-w (or (resolve-size-value max-w-raw resolve-base) +inf.0)])
        (max min-w (min max-w border-box-size)))
      (let* ([min-h-raw (get-style-prop styles 'min-height 'auto)]
             [max-h-raw (get-style-prop styles 'max-height 'none)]
             [min-h (or (resolve-size-value min-h-raw resolve-base) 0)]
             [max-h (or (resolve-size-value max-h-raw resolve-base) +inf.0)])
        (max min-h (min max-h border-box-size)))))

;; ============================================================
;; Item Placement
;; ============================================================

;; occupancy grid for auto-placement
(define (make-occupied-grid rows cols)
  (define grid (make-vector (* rows cols) #f))
  (values
   ;; is-occupied?
   (lambda (r c)
     (and (< r rows) (< c cols) (vector-ref grid (+ (* r cols) c))))
   ;; mark-occupied!
   (lambda (r c)
     (when (and (< r rows) (< c cols))
       (vector-set! grid (+ (* r cols) c) #t)))))

;; check if a rectangular region [r-start, r-end) x [c-start, c-end) is free
(define (region-free? is-occupied? r-start r-end c-start c-end)
  (for*/and ([r (in-range r-start r-end)]
             [c (in-range c-start c-end)])
    (not (is-occupied? r c))))

;; mark a rectangular region as occupied
(define (mark-region! mark-occupied! r-start r-end c-start c-end)
  (for* ([r (in-range r-start r-end)]
         [c (in-range c-start c-end)])
    (mark-occupied! r c)))

;; resolve a single grid line value to a 0-based index, or #f if auto
;; may return negative values for lines before the explicit grid
;; CSS line numbering: positive 1..N+1, negative -1 = N+1 (last line)
;; line-names: alist of (name . 1-based-line-number) from grid-template
(define (resolve-line-value v num-tracks [line-names '()])
  (match v
    [`(line ,n) (sub1 (if (> n 0) n (+ num-tracks 2 n)))]
    [`(named ,name)
     (define entry (assoc name line-names))
     (if entry (sub1 (cdr entry)) #f)]
    [_ #f]))

;; get span count from a grid-line value
(define (get-span-value v)
  (match v
    [`(span ,n) n]
    [_ 1]))

;; resolve placement for an item, returns (values start end) or #f for auto components
(define (resolve-item-placement-info rs re cs ce num-rows num-cols
                                     [col-line-names '()] [row-line-names '()])
  (define r-start-raw (resolve-line-value rs num-rows row-line-names))
  (define r-end-raw (resolve-line-value re num-rows row-line-names))
  (define c-start-raw (resolve-line-value cs num-cols col-line-names))
  (define c-end-raw (resolve-line-value ce num-cols col-line-names))

  ;; determine span from start/end line values
  (define r-span (match rs [`(span ,n) n] [_ (if (match re [`(span ,n) #t] [_ #f]) (get-span-value re) 1)]))
  (define c-span (match cs [`(span ,n) n] [_ (if (match ce [`(span ,n) #t] [_ #f]) (get-span-value ce) 1)]))

  ;; resolve start/end for definite positions
  ;; NOTE: values may be negative (meaning implicit tracks before the explicit grid)
  (define row-start
    (cond
      [r-start-raw r-start-raw]
      [r-end-raw (- r-end-raw r-span)]
      [else #f]))
  (define row-end
    (cond
      [r-end-raw (max (if row-start (add1 row-start) 1) r-end-raw)]
      [row-start (+ row-start r-span)]
      [else #f]))
  (define col-start
    (cond
      [c-start-raw c-start-raw]
      [c-end-raw (- c-end-raw c-span)]
      [else #f]))
  (define col-end
    (cond
      [c-end-raw (max (if col-start (add1 col-start) 1) c-end-raw)]
      [col-start (+ col-start c-span)]
      [else #f]))

  (values row-start row-end col-start col-end r-span c-span))

(define (place-grid-items children container-styles num-rows num-cols [containing-width #f])
  (define auto-flow (get-style-prop container-styles 'grid-auto-flow 'grid-row))
  (define is-dense? (or (eq? auto-flow 'grid-row-dense) (eq? auto-flow 'grid-column-dense)))
  (define is-column-flow? (or (eq? auto-flow 'grid-column) (eq? auto-flow 'grid-column-dense)))

  ;; named line maps from container's grid-template definitions
  (define col-line-names (get-style-prop container-styles 'grid-col-line-names '()))
  (define row-line-names (get-style-prop container-styles 'grid-row-line-names '()))

  ;; grid-template-areas map from container (list of (name row-start row-end col-start col-end))
  (define area-map (get-style-prop container-styles 'grid-template-areas '()))

  ;; first pass: determine max grid extent from definite placements
  (define max-r num-rows)
  (define max-c num-cols)

  ;; pre-read all item placement info (positions may be negative for items before explicit grid)
  (define item-infos
    (for/list ([child (in-list children)]
               [idx (in-naturals)])
      (define styles (get-box-styles child))
      (define item-bm (extract-box-model styles containing-width))
      ;; check for grid-area-name → resolve from grid-template-areas map
      (define area-name (get-style-prop styles 'grid-area-name #f))
      (define area-entry
        (and area-name (not (null? area-map))
             (assoc area-name area-map)))
      (define-values (row-start row-end col-start col-end r-span c-span)
        (cond
          [area-entry
           ;; area found: use line numbers directly (already 1-based, convert to 0-based)
           (define ar-start (- (list-ref area-entry 1) 1))
           (define ar-end (- (list-ref area-entry 2) 1))
           (define ac-start (- (list-ref area-entry 3) 1))
           (define ac-end (- (list-ref area-entry 4) 1))
           (values ar-start ar-end ac-start ac-end
                   (- ar-end ar-start) (- ac-end ac-start))]
          [else
           ;; normal placement via grid-row/column-start/end
           (define rs (get-grid-line styles 'grid-row-start))
           (define re (get-grid-line styles 'grid-row-end))
           (define cs (get-grid-line styles 'grid-column-start))
           (define ce (get-grid-line styles 'grid-column-end))
           (resolve-item-placement-info rs re cs ce num-rows num-cols
                                        col-line-names row-line-names)]))
      ;; track maximum extent
      (when (and row-end (> row-end max-r)) (set! max-r row-end))
      (when (and col-end (> col-end max-c)) (set! max-c col-end))
      (list child styles item-bm row-start row-end col-start col-end r-span c-span idx)))

  ;; find minimum row/col (may be negative for items referencing implicit before-tracks)
  (define min-r (apply min 0 (filter number? (map (lambda (i) (list-ref i 3)) item-infos))))
  (define min-c (apply min 0 (filter number? (map (lambda (i) (list-ref i 5)) item-infos))))
  (define row-offset (if (< min-r 0) (- min-r) 0))
  (define col-offset (if (< min-c 0) (- min-c) 0))

  ;; shift all definite positions to make non-negative before auto-placement
  (when (or (> row-offset 0) (> col-offset 0))
    (set! item-infos
      (for/list ([info (in-list item-infos)])
        (match-define (list child styles item-bm rs re cs ce r-span c-span idx) info)
        (list child styles item-bm
              (and rs (+ rs row-offset)) (and re (+ re row-offset))
              (and cs (+ cs col-offset)) (and ce (+ ce col-offset))
              r-span c-span idx)))
    (set! max-r (+ max-r row-offset))
    (set! max-c (+ max-c col-offset)))

  ;; use at least the explicit track count (adjusted for offset) for the grid
  ;; also account for auto-placed items in the appropriate flow direction
  (define eff-num-rows (+ num-rows row-offset))
  (define eff-num-cols (+ num-cols col-offset))
  (define auto-item-count
    (length (filter (lambda (info)
                      (or (not (list-ref info 3)) (not (list-ref info 5))))
                    item-infos)))
  (define safe-cols
    (if is-column-flow?
        (max max-c eff-num-cols (+ max-c (ceiling (/ (max 1 auto-item-count) (max eff-num-rows 1)))))
        (max max-c eff-num-cols 1)))
  (define safe-rows
    (if is-column-flow?
        (max max-r eff-num-rows 1)
        (max max-r eff-num-rows (+ max-r (ceiling (/ (max 1 auto-item-count) safe-cols))))))
  (define grid-rows safe-rows)
  (define grid-cols safe-cols)

  ;; create occupancy grid
  (define-values (is-occupied? mark-occupied!) (make-occupied-grid grid-rows grid-cols))

  ;; results vector (index -> grid-item)
  (define results (make-vector (length item-infos) #f))

  ;; === Pass 1: Place items with definite row AND column ===
  (for ([info (in-list item-infos)])
    (match-define (list child styles item-bm row-start row-end col-start col-end r-span c-span idx) info)
    (when (and row-start col-start)
      (mark-region! mark-occupied! row-start row-end col-start col-end)
      (vector-set! results idx (grid-item child styles item-bm row-start row-end col-start col-end #f))))

  ;; === Pass 2: Place items locked to the major axis ===
  ;; row-flow: definite row, auto column -> scan columns
  ;; column-flow: definite column, auto row -> scan rows
  (for ([info (in-list item-infos)])
    (match-define (list child styles item-bm row-start row-end col-start col-end r-span c-span idx) info)
    (cond
      ;; for row-flow: definite row, auto column
      [(and (not is-column-flow?) row-start (not col-start))
       (define placed? #f)
       (for ([c (in-range 0 grid-cols)]
             #:break placed?)
         (define ce-here (+ c c-span))
         (when (and (<= ce-here grid-cols)
                    (region-free? is-occupied? row-start row-end c ce-here))
           (mark-region! mark-occupied! row-start row-end c ce-here)
           (vector-set! results idx (grid-item child styles item-bm row-start row-end c ce-here #f))
           (set! placed? #t)))
       (unless placed?
         (define c grid-cols)
         (define ce-here (+ c c-span))
         (mark-region! mark-occupied! row-start row-end c ce-here)
         (vector-set! results idx (grid-item child styles item-bm row-start row-end c ce-here #f)))]
      ;; for column-flow: definite column, auto row
      [(and is-column-flow? col-start (not row-start))
       (define placed? #f)
       (for ([r (in-range 0 grid-rows)]
             #:break placed?)
         (define re-here (+ r r-span))
         (when (and (<= re-here grid-rows)
                    (region-free? is-occupied? r re-here col-start col-end))
           (mark-region! mark-occupied! r re-here col-start col-end)
           (vector-set! results idx (grid-item child styles item-bm r re-here col-start col-end #f))
           (set! placed? #t)))
       (unless placed?
         (define r grid-rows)
         (define re-here (+ r r-span))
         (mark-region! mark-occupied! r re-here col-start col-end)
         (vector-set! results idx (grid-item child styles item-bm r re-here col-start col-end #f)))]))

  ;; === Pass 3: Place remaining items using auto-placement cursor ===
  (define cursor-row 0)
  (define cursor-col 0)

  (for ([info (in-list item-infos)])
    (match-define (list child styles item-bm row-start row-end col-start col-end r-span c-span idx) info)
    (when (not (vector-ref results idx))  ; not yet placed
      (if is-column-flow?
          ;; === Column-major auto-placement ===
          (cond
            ;; definite row, auto column: lock row, scan columns from cursor
            [row-start
             (when is-dense? (set! cursor-col 0))
             (define placed? #f)
             (for ([c (in-range cursor-col grid-cols)]
                   #:break placed?)
               (define ce-here (+ c c-span))
               (when (and (<= ce-here grid-cols)
                          (region-free? is-occupied? row-start row-end c ce-here))
                 (mark-region! mark-occupied! row-start row-end c ce-here)
                 (vector-set! results idx (grid-item child styles item-bm row-start row-end c ce-here #f))
                 (unless is-dense? (set! cursor-col c))
                 (set! placed? #t)))
             (unless placed?
               (define c grid-cols)
               (define ce-here (+ c c-span))
               (mark-region! mark-occupied! row-start row-end c ce-here)
               (vector-set! results idx (grid-item child styles item-bm row-start row-end c ce-here #f)))]
            ;; fully auto: column-first cursor (advance down rows, then across columns)
            [else
             (when is-dense? (set! cursor-row 0) (set! cursor-col 0))
             (define placed? #f)
             (for ([c (in-range cursor-col grid-cols)]
                   #:break placed?)
               (define start-r (if (= c cursor-col) cursor-row 0))
               (for ([r (in-range start-r grid-rows)]
                     #:break placed?)
                 (define re-here (+ r r-span))
                 (define ce-here (+ c c-span))
                 (when (and (<= re-here grid-rows)
                            (<= ce-here grid-cols)
                            (region-free? is-occupied? r re-here c ce-here))
                   (mark-region! mark-occupied! r re-here c ce-here)
                   (vector-set! results idx (grid-item child styles item-bm r re-here c ce-here #f))
                   (unless is-dense? (set! cursor-col c) (set! cursor-row re-here))
                   (set! placed? #t))))
             (unless placed?
               ;; place beyond current grid (new column)
               (define c grid-cols)
               (mark-region! mark-occupied! 0 r-span c (+ c c-span))
               (vector-set! results idx (grid-item child styles item-bm 0 r-span c (+ c c-span) #f)))])
          ;; === Row-major auto-placement ===
          (cond
            ;; definite column, auto row: lock column, scan rows from cursor
            [col-start
             (when is-dense? (set! cursor-row 0))
             (define placed? #f)
             (for ([r (in-range cursor-row grid-rows)]
                   #:break placed?)
               (define re-here (+ r r-span))
               (when (and (<= re-here grid-rows)
                          (region-free? is-occupied? r re-here col-start col-end))
                 (mark-region! mark-occupied! r re-here col-start col-end)
                 (vector-set! results idx (grid-item child styles item-bm r re-here col-start col-end #f))
                 (unless is-dense? (set! cursor-row r))
                 (set! placed? #t)))
             (unless placed?
               (define r grid-rows)
               (define re-here (+ r r-span))
               (mark-region! mark-occupied! r re-here col-start col-end)
               (vector-set! results idx (grid-item child styles item-bm r re-here col-start col-end #f)))]
            ;; fully auto: row-first cursor (advance across columns, then down rows)
            [else
             (when is-dense? (set! cursor-row 0) (set! cursor-col 0))
             (define placed? #f)
             (for ([r (in-range cursor-row grid-rows)]
                   #:break placed?)
               (define start-c (if (= r cursor-row) cursor-col 0))
               (for ([c (in-range start-c grid-cols)]
                     #:break placed?)
                 (define re-here (+ r r-span))
                 (define ce-here (+ c c-span))
                 (when (and (<= re-here grid-rows)
                            (<= ce-here grid-cols)
                            (region-free? is-occupied? r re-here c ce-here))
                   (mark-region! mark-occupied! r re-here c ce-here)
                   (vector-set! results idx (grid-item child styles item-bm r re-here c ce-here #f))
                   (unless is-dense? (set! cursor-row r) (set! cursor-col ce-here))
                   (set! placed? #t))))
             (unless placed?
               ;; place beyond current grid (new row)
               (define r grid-rows)
               (mark-region! mark-occupied! r (+ r r-span) 0 c-span)
               (vector-set! results idx (grid-item child styles item-bm r (+ r r-span) 0 c-span #f)))]))))

  ;; return items and before-offsets for track creation
  (values (vector->list results) row-offset col-offset))

(define (get-grid-line styles prop-name)
  (get-style-prop styles prop-name 'grid-auto))

;; resolve CSS grid line to 0-based track index
(define (resolve-grid-placement start-line end-line num-tracks auto-pos)
  ;; extract span from start-line if present (for propagation to end when end is auto)
  (define start-span
    (match start-line
      [`(span ,n) n]
      [_ #f]))
  (define s
    (match start-line
      [`(line ,n) (sub1 (if (> n 0) n (+ num-tracks 1 n)))]
      [`(span ,_) auto-pos]
      ['grid-auto auto-pos]
      [_ auto-pos]))
  (define e
    (match end-line
      [`(line ,n) (sub1 (if (> n 0) n (+ num-tracks 1 n)))]
      [`(span ,n) (+ s n)]
      ['grid-auto (if start-span (+ s start-span) (add1 s))]
      [_ (if start-span (+ s start-span) (add1 s))]))
  (values (max 0 s) (max (add1 s) e)))

;; ============================================================
;; Track Utilities
;; ============================================================

;; total size of all tracks
(define (tracks-total-size tracks)
  (for/sum ([t (in-list tracks)])
    (track-base-size t)))

;; offset of a track from the start (no alignment)
(define (track-offset tracks index gap)
  (define result 0)
  (for ([t (in-list tracks)]
        [i (in-naturals)]
        #:break (= i index))
    (set! result (+ result (track-base-size t) (if (> i 0) gap 0))))
  result)

;; offset using content-alignment vector
(define (track-offset-with-alignment tracks index gap offsets)
  (if (and offsets (vector? offsets) (< index (vector-length offsets)))
      (vector-ref offsets index)
      (track-offset tracks index gap)))

;; size of tracks spanning [start, end)
(define (track-span-size tracks start end gap)
  (define span-tracks
    (for/list ([t (in-list tracks)]
               [i (in-naturals)]
               #:when (and (>= i start) (< i end)))
      t))
  (+ (for/sum ([t (in-list span-tracks)])
       (track-base-size t))
     (* gap (max 0 (sub1 (length span-tracks))))))

;; ============================================================
;; Helpers
;; ============================================================

(define (get-box-styles box)
  (match box
    [`(block ,_ ,styles ,_) styles]
    [`(inline ,_ ,styles ,_) styles]
    [`(inline-block ,_ ,styles ,_) styles]
    [`(flex ,_ ,styles ,_) styles]
    [`(grid ,_ ,styles ,_ ,_) styles]
    [`(table ,_ ,styles ,_) styles]
    [`(text ,_ ,styles ,_ ,_) styles]
    [`(replaced ,_ ,styles ,_ ,_) styles]
    [`(none ,_) '(style)]
    [_ '(style)]))

(define (set-view-pos view x y)
  (match view
    [`(view ,id ,_ ,_ ,w ,h ,children ,baseline)
     `(view ,id ,x ,y ,w ,h ,children ,baseline)]
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))

;; check if styles have an explicit (non-auto) width or height
(define (has-explicit-size? styles prop-name)
  (define v (get-style-prop styles prop-name 'auto))
  (and (not (eq? v 'auto)) v))

;; inject stretch width/height into a box's styles
;; this sets an explicit width/height so the block layout fills the cell
(define (inject-stretch-size box stretch-w w-val stretch-h h-val)
  (define (add-size-props styles sw w sh h)
    (match styles
      [`(style . ,props)
       (define new-props
         (let ([p1 (if sw
                       (cons `(width (px ,w))
                             (filter (lambda (p) (not (and (pair? p) (eq? (car p) 'width)))) props))
                       props)])
           (if sh
               (cons `(height (px ,h))
                     (filter (lambda (p) (not (and (pair? p) (eq? (car p) 'height)))) p1))
               p1)))
       `(style ,@new-props)]))
  (match box
    [`(block ,id ,styles ,children)
     `(block ,id ,(add-size-props styles stretch-w w-val stretch-h h-val) ,children)]
    [`(flex ,id ,styles ,children)
     `(flex ,id ,(add-size-props styles stretch-w w-val stretch-h h-val) ,children)]
    [`(grid ,id ,styles ,grid-def ,children)
     `(grid ,id ,(add-size-props styles stretch-w w-val stretch-h h-val) ,grid-def ,children)]
    [`(inline-block ,id ,styles ,children)
     `(inline-block ,id ,(add-size-props styles stretch-w w-val stretch-h h-val) ,children)]
    [`(replaced ,id ,styles ,iw ,ih)
     `(replaced ,id ,(add-size-props styles stretch-w w-val stretch-h h-val) ,iw ,ih)]
    [_ box]))

;; offset an absolute child view from padding-box to border-box coordinates
(define (offset-abs-view view dx dy)
  (match view
    [`(view ,id ,x ,y ,w ,h ,children ,baseline)
     `(view ,id ,(+ x dx) ,(+ y dy) ,w ,h ,children ,baseline)]
    [`(view ,id ,x ,y ,w ,h ,children)
     `(view ,id ,(+ x dx) ,(+ y dy) ,w ,h ,children)]
    [`(view-text ,id ,x ,y ,w ,h ,text)
     `(view-text ,id ,(+ x dx) ,(+ y dy) ,w ,h ,text)]
    [_ view]))
