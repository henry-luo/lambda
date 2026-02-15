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
     (define avail-h (avail-height->number (caddr avail)))
     (define bm (extract-box-model styles avail-w))

     ;; extract grid properties
     (define row-gap (resolve-gap (get-style-prop styles 'row-gap 0) avail-h))
     (define col-gap (resolve-gap (get-style-prop styles 'column-gap 0) avail-w))
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
     (resolve-track-sizes! col-tracks col-available items 'col dispatch-fn avail col-gap row-gap col-cross-avail content-w)

     ;; === Phase 4.5: Re-resolve column tracks for indefinite containers ===
     ;; CSS Grid spec: intrinsic sizing determines the container width, then the
     ;; grid is laid out with that definite width. The fr distribution may differ
     ;; because the "find size of an fr" algorithm (§11.7.1) with definite space
     ;; applies the max(sum,1) clamping differently than the indefinite flex fraction.
     (define col-indefinite-for-rerun? (or (infinite? col-available) (<= col-available 0)))
     (define intrinsic-col-total #f)
     (when (and col-indefinite-for-rerun? (ormap fr-track? col-tracks))
       (set! intrinsic-col-total (+ (tracks-total-size col-tracks) total-col-gaps))
       (define definite-col-avail (max 0 (- intrinsic-col-total total-col-gaps)))
       ;; reset column tracks for re-resolution with definite width
       (for ([t (in-list col-tracks)])
         (set-track-base-size! t 0)
         (set-track-growth-limit! t +inf.0))
       (resolve-track-sizes! col-tracks definite-col-avail items 'col dispatch-fn avail col-gap row-gap col-cross-avail intrinsic-col-total))

     ;; === Phase 5: Resolve row track sizes ===
     (define total-row-gaps (* row-gap (max 0 (sub1 (length row-tracks)))))
     (define row-available
       (if explicit-h
           (- explicit-h total-row-gaps)
           +inf.0))
     (resolve-track-sizes! row-tracks row-available items 'row dispatch-fn avail col-gap row-gap content-w content-w)

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
         (define effective-ai (if (eq? ai 'align-baseline) 'align-start ai))
         (define raw-stretch-w (and (eq? ji 'align-stretch) (not item-has-width)
                                    (not (and item-has-aspect-ratio item-has-height))))
         (define raw-stretch-h (and (eq? effective-ai 'align-stretch) (not item-has-height)
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
         (define actual-box
           (if (or stretch-w stretch-h)
               (inject-stretch-size (grid-item-box item) stretch-w stretch-cell-w stretch-h stretch-cell-h)
               (grid-item-box item)))
         (define avail-w-for-child
           (if (or stretch-w item-has-width)
               `(definite ,cell-w)
               'av-max-content))
         (define avail-h-for-child
           (if (or stretch-h item-has-height)
               `(definite ,cell-h)
               `(definite ,cell-h)))
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
       (define idx (resolve-line-value line-val num-explicit))
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

(define (resolve-track-sizes! tracks available items axis dispatch-fn avail col-gap row-gap [cross-avail +inf.0] [containing-width #f])
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
      [_ (void)]))

  ;; helper to extract fr value from a track (used in phases 2.5 and 4)
  (define (get-fr-value t)
    (match (track-track-size t)
      [`(fr ,n) n]
      [`(auto-fit-track (fr ,n)) n]
      [`(minmax ,_ (fr ,n)) n]
      [`(auto-fit-track (minmax ,_ (fr ,n))) n]
      [_ 0]))

  ;; phase 2: size items spanning single tracks
  (for ([item (in-list items)])
    (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
    (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
    (when (and (= (- end start) 1) (< start (length tracks)))
      (define t (list-ref tracks start))
      (define ts (track-track-size t))
      (when (or (eq? ts 'auto) (eq? ts 'min-content) (eq? ts 'max-content)
                (and (pair? ts) (eq? (car ts) 'minmax))
                (and (pair? ts) (eq? (car ts) 'fr))
                (and indefinite? (pair? ts) (eq? (car ts) '%)))
        (define item-size (measure-grid-item-min item axis dispatch-fn avail cross-avail containing-width))
        (when (> item-size (track-base-size t))
          (set-track-base-size! t
            (if (infinite? (track-growth-limit t))
                item-size
                (min item-size (track-growth-limit t))))))))

  ;; phase 2.5: distribute spanning items across multiple tracks
  ;; sort spanning items by span count (smaller spans first) for better distribution
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
      (define item-size (measure-grid-item-min item axis dispatch-fn avail cross-avail containing-width))
      (define current-sum (for/sum ([t (in-list span-tracks)]) (track-base-size t)))
      (define gap-total (* (max 0 (- (length span-tracks) 1))
                           (if (eq? axis 'col) col-gap row-gap)))
      (define extra (- item-size current-sum gap-total))
      (when (> extra 0)
        ;; distribute extra space among growable tracks (prefer non-fixed)
        (define growable
          (filter (lambda (t)
                    (or (eq? (track-track-size t) 'auto)
                        (eq? (track-track-size t) 'min-content)
                        (eq? (track-track-size t) 'max-content)
                        (and (pair? (track-track-size t)) (eq? (car (track-track-size t)) 'fr))
                        (and (pair? (track-track-size t)) (eq? (car (track-track-size t)) 'minmax))
                        (and indefinite? (pair? (track-track-size t)) (eq? (car (track-track-size t)) '%))))
                  span-tracks))
        ;; if no growable tracks, grow all of them
        (define targets (if (null? growable) span-tracks growable))
        ;; check if all growable targets are fr tracks: distribute proportionally by fr value
        (define all-fr? (andmap fr-track? targets))
        (define total-fr-val
          (if all-fr?
              (for/sum ([t (in-list targets)]) (get-fr-value t))
              0))
        (for ([t (in-list targets)])
          (define share
            (if (and all-fr? (> total-fr-val 0))
                ;; proportional distribution by fr value
                (* extra (/ (get-fr-value t) total-fr-val))
                ;; equal distribution for non-fr tracks
                (/ extra (length targets))))
          (set-track-base-size! t (+ (track-base-size t) share))))))

  ;; phase 3: grow non-fr tracks up to their growth limit
  ;; this must happen BEFORE fr distribution so fr uses remaining space
  (when (and (not (infinite? available))
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

  ;; "Find the size of an fr" sub-algorithm (CSS Grid §11.7.1)
  ;; Given a set of tracks and a space to fill, returns the hypothetical fr size.
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
          (let* ([effective-sum (max sum-fr 1)]
                 [hyp (/ space effective-sum)])
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
       ;; Per-item contributions: for spanning items crossing flexible tracks,
       ;; run "find the size of an fr" with those tracks and the item's max-content
       (define flex-fraction-from-items
         (for/fold ([best 0]) ([item (in-list items)])
           (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
           (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
           (if (and (> (- end start) 1) (< start (length tracks)))
               (let* ([item-tracks
                       (for/list ([i (in-range start (min end (length tracks)))])
                         (list-ref tracks i))]
                      [has-flex? (ormap fr-track? item-tracks)])
                 (if has-flex?
                     (let* ([item-size (measure-grid-item-min item axis dispatch-fn avail cross-avail containing-width)]
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
       ;; iterative freeze algorithm
       (let loop ([active-tracks (filter fr-track? tracks)]
                  [space fr-space])
         (define sum-fr (for/sum ([t (in-list active-tracks)]) (get-fr-value t)))
         (when (> sum-fr 0)
           ;; CSS Grid §11.7.1: if sum of flex factors < 1, use 1 as divisor
           (define effective-sum (max sum-fr 1))
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
    [`(% ,pct) (if (and (number? available) (not (infinite? available)))
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
  ;; add item margins to get the outer size for track contribution
  ;; per CSS spec, all percentage margins resolve against containing block inline size
  (define item-bm (extract-box-model (grid-item-styles item) containing-width))
  (define margin-sum
    (if (eq? axis 'col)
        (+ (box-model-margin-left item-bm) (box-model-margin-right item-bm))
        (+ (box-model-margin-top item-bm) (box-model-margin-bottom item-bm))))
  (+ border-box-size margin-sum))

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
(define (resolve-line-value v num-tracks)
  (match v
    [`(line ,n) (sub1 (if (> n 0) n (+ num-tracks 2 n)))]
    [_ #f]))

;; get span count from a grid-line value
(define (get-span-value v)
  (match v
    [`(span ,n) n]
    [_ 1]))

;; resolve placement for an item, returns (values start end) or #f for auto components
(define (resolve-item-placement-info rs re cs ce num-rows num-cols)
  (define r-start-raw (resolve-line-value rs num-rows))
  (define r-end-raw (resolve-line-value re num-rows))
  (define c-start-raw (resolve-line-value cs num-cols))
  (define c-end-raw (resolve-line-value ce num-cols))

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

  ;; first pass: determine max grid extent from definite placements
  (define max-r num-rows)
  (define max-c num-cols)

  ;; pre-read all item placement info (positions may be negative for items before explicit grid)
  (define item-infos
    (for/list ([child (in-list children)]
               [idx (in-naturals)])
      (define styles (get-box-styles child))
      (define item-bm (extract-box-model styles containing-width))
      (define rs (get-grid-line styles 'grid-row-start))
      (define re (get-grid-line styles 'grid-row-end))
      (define cs (get-grid-line styles 'grid-column-start))
      (define ce (get-grid-line styles 'grid-column-end))
      (define-values (row-start row-end col-start col-end r-span c-span)
        (resolve-item-placement-info rs re cs ce num-rows num-cols))
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
