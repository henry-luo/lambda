#lang racket/base

;; layout-flex.rkt — Flexbox layout algorithm
;;
;; Implements CSS Flexbox Level 1 (https://www.w3.org/TR/css-flexbox-1/)
;; as a multi-phase algorithm modeled on Radiant's layout_flex_multipass.cpp.
;;
;; The 9 phases of flex layout:
;;   1. Collect flex items
;;   2. Sort by CSS order property
;;   3. Determine main/cross axis sizes
;;   4. Create flex lines (handle wrapping)
;;   5. Resolve flexible lengths (grow/shrink)
;;   6. Determine cross sizes
;;   7. Main axis alignment (justify-content)
;;   8. Cross axis alignment (align-items/align-self)
;;   9. Compute final positions

(require racket/match
         racket/list
         racket/math
         "css-layout-lang.rkt"
         "layout-common.rkt"
         "layout-positioned.rkt")

(provide layout-flex)

;; ============================================================
;; Flex Item — internal representation during layout
;; ============================================================

(struct flex-item
  (box                ; original Box term
   styles             ; resolved styles
   bm                 ; box-model
   order              ; CSS order property
   flex-grow          ; flex-grow factor
   flex-shrink        ; flex-shrink factor
   flex-basis         ; resolved flex-basis (px)
   hypothetical-main  ; hypothetical main size
   min-main           ; minimum main size
   max-main           ; maximum main size
   main-size          ; final main size (set during resolution)
   cross-size         ; final cross size
   view               ; laid out view (set after sizing)
   child-index        ; original index among children for output ordering
   baseline           ; computed baseline (distance from top of content box)
   )
  #:transparent #:mutable)

;; ============================================================
;; Flex Line — group of items on one line
;; ============================================================

(struct flex-line
  (items              ; list of flex-item
   main-size          ; total main size of items
   cross-size         ; cross size of this line
   free-space         ; remaining space after sizing
   )
  #:transparent #:mutable)

;; ============================================================
;; Baseline Computation — imported from layout-common.rkt
;; ============================================================

;; ============================================================
;; Flex Layout — Main Entry Point
;; ============================================================

(define (layout-flex box avail dispatch-fn)
  (match box
    [`(flex ,id ,styles (,children ...))
     (define avail-w (avail-width->number (cadr avail)))
     (define avail-h (avail-height->number (caddr avail)))
     ;; detect intrinsic sizing mode (av-max-content or av-min-content)
     (define is-intrinsic-width-sizing?
       (or (eq? (cadr avail) 'av-max-content) (eq? (cadr avail) 'av-min-content)))
     (define bm (extract-box-model styles avail-w))

     ;; extract flex container properties
     (define direction (get-style-prop styles 'flex-direction 'row))
     (define wrap-mode (get-style-prop styles 'flex-wrap 'nowrap))
     (define justify (get-style-prop styles 'justify-content 'flex-start))
     (define align-items (get-style-prop styles 'align-items 'align-stretch))
     (define align-content (get-style-prop styles 'align-content 'content-stretch))
     (define row-gap-raw (get-style-prop styles 'row-gap 0))
     (define col-gap-raw (get-style-prop styles 'column-gap 0))

     ;; determine main axis
     (define is-row? (or (eq? direction 'row) (eq? direction 'row-reverse)))
     (define is-reversed? (or (eq? direction 'row-reverse) (eq? direction 'column-reverse)))

     ;; resolve container's content size
     ;; even if avail-w is #f (indefinite), explicit px widths still resolve
     ;; per CSS spec: percentage widths against indefinite containing blocks behave as auto
     (define css-width-raw (get-style-prop styles 'width 'auto))
     (define pct-width-indefinite?
       (and (not avail-w) (match css-width-raw [`(% ,_) #t] [_ #f])))
     (define effective-styles-for-width
       (if pct-width-indefinite?
           ;; strip percentage width → treat as auto
           (match styles
             [`(style . ,props)
              `(style ,@(filter (lambda (p) (not (and (pair? p) (eq? (car p) 'width)))) props))]
             [_ styles])
           styles))
     (define content-w (resolve-block-width effective-styles-for-width (or avail-w 0)))
     (define explicit-h (resolve-block-height styles avail-h avail-w))

     ;; resolve percentage gaps:
     ;; per CSS spec, percentage row-gap resolves against the container's block size (height),
     ;; and percentage column-gap resolves against the container's inline size (width).
     ;; if the reference size is indefinite, the percentage resolves to 0.
     (define row-gap (resolve-gap row-gap-raw explicit-h))
     (define col-gap (resolve-gap col-gap-raw (if avail-w content-w #f)))

     ;; main/cross axis available sizes
     ;; for row: when the container has auto width and avail-w is indefinite,
     ;; the behavior depends on intrinsic sizing mode:
     ;; - av-max-content: use +inf.0 so items get natural (flex-basis) sizes
     ;; - av-min-content: use 0 so items shrink maximally to their auto-minimum
     ;; - pure indefinite: use +inf.0 (same as max-content)
     ;; content-w = 0 when avail-w is indefinite (from resolve-block-width with auto/0).
     ;; percentage width with indefinite containing block also behaves as auto.
     (define is-min-content-mode? (eq? (cadr avail) 'av-min-content))
     (define effective-width-auto?
       (or (eq? (get-style-prop styles 'width 'auto) 'auto)
           pct-width-indefinite?))
     (define main-avail
       (if is-row?
           (cond
             [(and effective-width-auto? (not avail-w) is-min-content-mode?) 0]
             [(and effective-width-auto? (not avail-w)) +inf.0]
             [else content-w])
           (or explicit-h
               ;; for column wrap: use max-height as wrapping boundary when height is auto
               ;; per CSS Flexbox §9.3, items wrap at the container's inner main size,
               ;; and max-height constrains the main axis for wrapping purposes
               (let ([max-h (resolve-max-height styles avail-h)])
                 (if (and (not (eq? wrap-mode 'nowrap)) (< max-h +inf.0))
                     max-h
                     +inf.0)))))
     ;; for cross-avail: use explicit size, or min-size as a floor for positioning/alignment
     ;; this ensures that min-height (for row) or min-width (for column) is respected
     ;; when computing single-line expansion and cross-axis alignment offsets
     (define cross-avail
       (if is-row?
           (or explicit-h
               (let ([min-h (resolve-min-height styles avail-h)])
                 (if (> min-h 0) min-h +inf.0)))
           ;; column flex: cross axis = width
           ;; when the container has definite available width, content-w correctly resolves.
           ;; when avail-w is indefinite (intrinsic sizing), content-w resolves to 0.
           ;; for av-max-content / indefinite, use +inf.0 so items expand to natural widths;
           ;; for av-min-content, keep content-w (0) to force items to minimum widths.
           (if (and effective-width-auto? (not avail-w)
                    (not (eq? (cadr avail) 'av-min-content)))
               +inf.0
               content-w)))
     ;; track whether cross-avail comes from a definite source:
     ;; for row: explicit height is set
     ;; for column: container has explicit width (avail-w is definite)
     (define cross-definite?
       (if is-row?
           (and explicit-h #t)
           (and avail-w
                (not (eq? (get-style-prop styles 'width 'auto) 'auto)))))

     ;; gap in main/cross directions
     (define main-gap (if is-row? col-gap row-gap))
     (define cross-gap (if is-row? row-gap col-gap))

     ;; === Phase 1: Collect flex items ===
     ;; for percentage margin/padding resolution, items resolve against the
     ;; container's inline-size (content-w for row, or cross/main avail)
     (define items (collect-flex-items children styles is-row? main-avail cross-avail dispatch-fn
                                      content-w))

     ;; === Phase 2: Sort by order ===
     (define sorted-items
       (sort items < #:key flex-item-order))

     ;; CSS Flexbox §9.3: Items are assigned to flex lines in order-modified
     ;; document order (sorted by `order`), NOT reversed by flex-direction.
     ;; The direction reversal only affects main-axis placement within lines.
     (define ordered-items sorted-items)

     ;; === Phase 3: Determine hypothetical main sizes (already done in collection) ===

     ;; === Phase 4: Create flex lines ===
     (define lines
       (create-flex-lines ordered-items main-avail main-gap wrap-mode))

     ;; === Phase 5: Resolve flexible lengths ===
     ;; main-definite? = true when the container has a definite main-axis size
     ;; for row: explicit width OR definite available width from parent
     ;; for column: explicit height
     ;; percentage width with indefinite containing block is NOT definite
     ;; ALSO true for min-content mode: main-avail=0 acts as a definite constraint
     ;; to force items to shrink maximally to their content-based minimums
     (define main-definite?
       (if is-row?
           (or (and (not effective-width-auto?)
                    (not (eq? (get-style-prop styles 'width 'auto) 'auto)))
               (and avail-w #t)
               is-min-content-mode?)
           (and explicit-h #t)))
     (define resolved-lines
       (for/list ([line (in-list lines)])
         (resolve-flex-lengths line main-avail main-gap is-row? main-definite?)))

     ;; === Phase 5b: Re-resolve when min/max-main constrains indefinite main axis ===
     ;; Per CSS Flexbox spec, when an auto main-size is clamped by min/max constraints,
     ;; the clamped size becomes definite and items must be re-resolved.
     (when (and (not main-definite?) (infinite? main-avail))
       (define min-main-content
         (if is-row?
             (let ([mn-val (get-style-prop styles 'min-width 'auto)])
               (let ([mn (resolve-size-value mn-val (or avail-w 0))])
                 (if mn
                     (if (eq? (box-model-box-sizing bm) 'border-box)
                         (max 0 (- mn (horizontal-pb bm)))
                         mn)
                     0)))
             (resolve-min-height styles avail-h)))
       (define max-main-content
         (if is-row?
             (let ([mx-val (get-style-prop styles 'max-width 'none)])
               (let ([mx (resolve-size-value mx-val (or avail-w 0))])
                 (if mx
                     (if (eq? (box-model-box-sizing bm) 'border-box)
                         (max 0 (- mx (horizontal-pb bm)))
                         mx)
                     +inf.0)))
             (resolve-max-height styles avail-h)))
       ;; compute total main-axis size across all lines
       (define current-total
         (for/fold ([s 0]) ([line (in-list resolved-lines)])
           (+ s (flex-line-main-size line))))
       (define clamped-main (max min-main-content (min max-main-content current-total)))
       (when (and (> (abs (- clamped-main current-total)) 0.01)
                  (not (infinite? clamped-main)))
         ;; content exceeds max or falls below min → re-resolve with constrained size
         (set! resolved-lines
           (for/list ([line (in-list lines)])
             (resolve-flex-lengths line clamped-main main-gap is-row? #t)))))

     ;; === Phase 6: Determine cross sizes ===
     ;; compute max-cross from container's max-height/max-width (content-box)
     (define max-cross
       (if is-row?
           (resolve-max-height styles avail-h)
           (let ([mx-val (get-style-prop styles 'max-width 'none)])
             (or (resolve-size-value mx-val (or avail-w 0)) +inf.0))))
     (define cross-sized-lines
       (determine-cross-sizes resolved-lines cross-avail align-items
                              is-row? wrap-mode align-content dispatch-fn
                              cross-definite? max-cross main-avail))

     ;; === Phase 7 & 8: Main and cross axis alignment, compute positions ===
     ;; compute effective main-avail for justify-content positioning
     ;; when main is indefinite, use content size expanded by min/max constraints
     ;; so that min-height (for column) or min-width (for row) is respected for centering
     (define effective-main-avail
       (if (infinite? main-avail)
           (let* ([content-main
                   (for/fold ([mx 0]) ([line (in-list cross-sized-lines)])
                     (max mx (for/fold ([s 0]) ([item (in-list (flex-line-items line))])
                               (+ s (flex-item-main-size item)
                                  (let ([bm (flex-item-bm item)])
                                    (if is-row? (+ (horizontal-pb bm) (horizontal-margin bm))
                                        (+ (vertical-pb bm) (vertical-margin bm))))))))]
                  [min-main (if is-row?
                                (or (resolve-size-value (get-style-prop styles 'min-width 'auto) (or avail-w 0)) 0)
                                (resolve-min-height styles avail-h))]
                  [max-main (if is-row?
                                (or (resolve-size-value (get-style-prop styles 'max-width 'none) (or avail-w 0)) +inf.0)
                                (resolve-max-height styles avail-h))])
             (max min-main (min max-main content-main)))
           main-avail))
     ;; when effective-main-avail differs from original (i.e., min/max expanded the container),
     ;; update each line's free-space so justify-content can distribute the extra space
     (when (and (not (= effective-main-avail main-avail))
                (not (infinite? effective-main-avail)))
       (for ([line (in-list cross-sized-lines)])
         (define used (flex-line-main-size line))
         (set-flex-line-free-space! line (- effective-main-avail used))))
     (define-values (child-views total-main total-cross first-line-baseline)
       (position-flex-items cross-sized-lines
                           effective-main-avail cross-avail
                           justify align-items align-content
                           main-gap cross-gap
                           is-row? is-reversed? wrap-mode bm dispatch-fn
                           cross-definite?))

     ;; compute final container size
     ;; for auto-width row containers, use total-main to get intrinsic width
     ;; percentage width with indefinite containing block → treated as auto
     (define has-explicit-width?
       (and (not pct-width-indefinite?)
            (not (eq? (get-style-prop styles 'width 'auto) 'auto))))
     (define has-explicit-height?
       (not (eq? (get-style-prop styles 'height 'auto) 'auto)))
     (define aspect-ratio-val (get-style-prop styles 'aspect-ratio #f))
     ;; for intrinsic row sizing (CSS Flexbox §9.9.1):
     ;; compute each item's max-content contribution to the container's intrinsic width.
     ;; For min-content mode: items have already been shrunk to their minimums by
     ;; resolve-flex-lengths (main-avail=0, main-definite?=#t), so use total-main instead.
     ;; Only items with EXPLICIT flex-basis (not auto) need special handling, because
     ;; their flex-base may differ from their natural (max-content) size.
     ;; For flex-basis: auto items, the hypothetical outer already equals their content size.
     ;; Contribution rules for explicit-basis items in max-content mode:
     ;; - grow>0: max(hypothetical-outer, max-content-outer)
     ;;   → the container must be wide enough for growable items to reach their natural size
     ;; - grow=0, shrink>0: min(hypothetical-outer, max-content-outer)
     ;;   → shrinkable items compress to their natural size (can be smaller than flex-base)
     ;; - grow=0, shrink=0: hypothetical-outer (items stay at flex-base)
     ;; For items with explicit main-axis size (width), use the resolved size directly
     ;; with min/max clamping (avoids re-dispatching which may give incorrect results).
     (define intrinsic-flex-main
       (if (and is-row? (not has-explicit-width?) is-intrinsic-width-sizing?)
           (let ([is-max-content? (eq? (cadr avail) 'av-max-content)])
             (if (not is-max-content?)
                 ;; min-content mode: compute per CSS Flexbox §9.9.1
                 ;; Single-line: sum of items' min-content contributions
                 ;; Multi-line: largest individual min-content contribution
                 ;; min-content contribution = max(outer-min-content, outer-preferred)
                 ;; clamped by flex-base as max (when not growable) and min/max properties
                 (let ()
                   ;; compute per-item min-content contribution
                   (define (item-min-content-contribution item)
                     (define item-bm (flex-item-bm item))
                     (define item-styles (flex-item-styles item))
                     (define main-margin (if is-row? (horizontal-margin item-bm)
                                             (vertical-margin item-bm)))
                     (define main-pb (if is-row? (horizontal-pb item-bm)
                                         (vertical-pb item-bm)))
                     ;; outer min-content size: measure at av-min-content + margin
                     (define child-box (flex-item-box item))
                     (define mc-avail `(avail av-min-content indefinite))
                     (define mc-view (dispatch-fn child-box mc-avail))
                     (define outer-min-content (+ (if is-row? (view-width mc-view) (view-height mc-view))
                                                  main-margin))
                     ;; outer preferred size (if item has explicit main size)
                     (define main-size-prop (if is-row? 'width 'height))
                     (define main-size-val (get-style-prop item-styles main-size-prop 'auto))
                     (define has-item-explicit-main? (not (eq? main-size-val 'auto)))
                     (define outer-preferred
                       (if has-item-explicit-main?
                           (let* ([resolved (or (resolve-size-value main-size-val +inf.0) 0)]
                                  [content (if (eq? (box-model-box-sizing item-bm) 'border-box)
                                               (max 0 (- resolved main-pb))
                                               resolved)]
                                  [bb (+ content main-pb)])
                             (+ bb main-margin))
                           #f))
                     ;; contribution = max(outer-min-content, outer-preferred) if preferred exists
                     (define base-contribution
                       (if outer-preferred
                           (max outer-min-content outer-preferred)
                           outer-min-content))
                     ;; clamp by flex-base as max when not growable (grow=0)
                     (define grow (flex-item-flex-grow item))
                     (define basis-outer (flex-item-hypothetical-main item))
                     (define clamped-contribution
                       (if (= grow 0)
                           (min base-contribution basis-outer)
                           base-contribution))
                     ;; apply min/max main size constraints
                     (define min-prop (if is-row? 'min-width 'min-height))
                     (define max-prop (if is-row? 'max-width 'max-height))
                     (define min-raw (or (resolve-size-value
                                          (get-style-prop item-styles min-prop 'auto) +inf.0) 0))
                     (define max-raw (or (resolve-size-value
                                          (get-style-prop item-styles max-prop 'none) +inf.0) +inf.0))
                     (define min-c-content
                       (if (and (> min-raw 0) (eq? (box-model-box-sizing item-bm) 'border-box))
                           (max 0 (- min-raw main-pb)) min-raw))
                     (define max-c-content
                       (if (and (not (infinite? max-raw)) (eq? (box-model-box-sizing item-bm) 'border-box))
                           (max 0 (- max-raw main-pb)) max-raw))
                     (define min-outer (+ min-c-content main-pb main-margin))
                     (define max-outer (+ max-c-content main-pb main-margin))
                     (max min-outer (min max-outer clamped-contribution)))
                   ;; gather all items across all lines
                   (define all-items
                     (apply append (map flex-line-items cross-sized-lines)))
                   (define is-multi-line-container?
                     (not (eq? wrap-mode 'nowrap)))
                   (if is-multi-line-container?
                       ;; multi-line: largest individual min-content contribution
                       (apply max 0 (map item-min-content-contribution all-items))
                       ;; single-line: sum of min-content contributions + gaps
                       (+ (for/sum ([item (in-list all-items)])
                            (item-min-content-contribution item))
                          (* main-gap (max 0 (sub1 (length all-items)))))))
                 ;; max-content mode
                 (for/sum ([line (in-list cross-sized-lines)])
               (define items-in-line (flex-line-items line))
               (+ (for/sum ([item (in-list items-in-line)])
                    (define grow (flex-item-flex-grow item))
                    (define shrink (flex-item-flex-shrink item))
                    (define hyp-outer (flex-item-hypothetical-main item))
                    ;; only adjust items with explicit flex-basis in max-content mode
                    (define item-styles (flex-item-styles item))
                    (define basis-val (get-style-prop item-styles 'flex-basis 'auto))
                    (define has-explicit-basis? (not (eq? basis-val 'auto)))
                    (if (and is-max-content? has-explicit-basis?
                             (or (> grow 0) (> shrink 0)))
                        ;; compute max-content outer for this item
                        (let* ([item-bm (flex-item-bm item)]
                               [main-margin (if is-row? (horizontal-margin item-bm)
                                                (vertical-margin item-bm))]
                               [main-pb (if is-row? (horizontal-pb item-bm)
                                            (vertical-pb item-bm))]
                               [main-size-prop (if is-row? 'width 'height)]
                               [main-size-val (get-style-prop item-styles main-size-prop 'auto)]
                               [has-item-explicit-main? (not (eq? main-size-val 'auto))]
                               [max-content-outer
                                (if has-item-explicit-main?
                                    ;; explicit main size: max-content = resolved size with
                                    ;; min/max clamping, converted to border-box + margin
                                    (let* ([resolved (or (resolve-size-value main-size-val +inf.0) 0)]
                                           [content (if (eq? (box-model-box-sizing item-bm) 'border-box)
                                                        (max 0 (- resolved main-pb))
                                                        resolved)]
                                           ;; apply min/max constraints
                                           [min-prop (if is-row? 'min-width 'min-height)]
                                           [max-prop (if is-row? 'max-width 'max-height)]
                                           [min-raw (or (resolve-size-value
                                                         (get-style-prop item-styles min-prop 'auto) +inf.0) 0)]
                                           [max-raw (or (resolve-size-value
                                                         (get-style-prop item-styles max-prop 'none) +inf.0) +inf.0)]
                                           [min-c (if (and (> min-raw 0)
                                                          (eq? (box-model-box-sizing item-bm) 'border-box))
                                                      (max 0 (- min-raw main-pb)) min-raw)]
                                           [max-c (if (and (not (infinite? max-raw))
                                                          (eq? (box-model-box-sizing item-bm) 'border-box))
                                                      (max 0 (- max-raw main-pb)) max-raw)]
                                           [clamped (max min-c (min max-c content))]
                                           [bb (+ clamped main-pb)])
                                      (+ bb main-margin))
                                    ;; auto main size: measure by dispatching with av-max-content
                                    (let* ([child-box (flex-item-box item)]
                                           [mc-avail `(avail av-max-content indefinite)]
                                           [mc-view (dispatch-fn child-box mc-avail)]
                                           [max-content-bb (view-width mc-view)])
                                      (+ max-content-bb main-margin)))])
                          (cond
                            [(> grow 0) (max hyp-outer max-content-outer)]
                            [(> shrink 0) (min hyp-outer max-content-outer)]
                            [else hyp-outer]))
                        ;; flex-basis:auto: use hypothetical outer
                        hyp-outer))
                  (* main-gap (max 0 (sub1 (length items-in-line))))))))
           #f))
     (define final-content-w
       (let ([raw-w (cond
                      [is-row?
                       (if has-explicit-width? content-w
                           (if intrinsic-flex-main
                               (max content-w intrinsic-flex-main)
                               (max content-w total-main)))]
                      ;; column direction: cross axis = width.  When the container
                      ;; has an explicit width, honour it (CSS Flexbox §9.2).
                      [else (if has-explicit-width? content-w
                                (max content-w total-cross))])])
         ;; aspect-ratio: if width is auto and height is explicit (resolved), derive width
         (if (and (not has-explicit-width?)
                  has-explicit-height?
                  explicit-h (number? explicit-h)
                  aspect-ratio-val (number? aspect-ratio-val) (> aspect-ratio-val 0))
             ;; aspect-ratio = width / height → width = height * ratio
             (let* ([vert-pb (+ (box-model-padding-top bm) (box-model-padding-bottom bm)
                                (box-model-border-top bm) (box-model-border-bottom bm))]
                    [horiz-pb (+ (box-model-padding-left bm) (box-model-padding-right bm)
                                 (box-model-border-left bm) (box-model-border-right bm))]
                    [bb-h (+ explicit-h vert-pb)]
                    [bb-w (* bb-h aspect-ratio-val)])
               (max 0 (- bb-w horiz-pb)))
             raw-w)))
     ;; apply min-width/max-width when width is auto (not already handled by resolve-block-width)
     (when (not has-explicit-width?)
       (define min-w-val (get-style-prop styles 'min-width 'auto))
       (define max-w-val (get-style-prop styles 'max-width 'none))
       (define min-w-raw (or (resolve-size-value min-w-val (or avail-w 0)) 0))
       (define max-w-raw (or (resolve-size-value max-w-val (or avail-w 0)) +inf.0))
       ;; convert to content-box when border-box
       (define min-w
         (if (and (> min-w-raw 0) (eq? (box-model-box-sizing bm) 'border-box))
             (max 0 (- min-w-raw (horizontal-pb bm)))
             min-w-raw))
       (define max-w
         (if (and (not (infinite? max-w-raw)) (eq? (box-model-box-sizing bm) 'border-box))
             (max 0 (- max-w-raw (horizontal-pb bm)))
             max-w-raw))
       (set! final-content-w (max min-w (min max-w final-content-w))))
     (define final-content-h
       (let ([raw-h (or explicit-h
                        (if is-row? total-cross total-main))])
         ;; aspect-ratio: if height is auto and width is explicit, derive height
         (define aspect-ratio (get-style-prop styles 'aspect-ratio #f))
         (define ar-h
           (if (and (not explicit-h)
                    has-explicit-width?
                    aspect-ratio (number? aspect-ratio) (> aspect-ratio 0))
               ;; aspect-ratio = width / height → height = width / ratio
               ;; use border-box width for calculation, then subtract vertical pb
               (let* ([bb-w (compute-border-box-width bm content-w)]
                      [bb-h (/ bb-w aspect-ratio)]
                      [vert-pb (+ (box-model-padding-top bm) (box-model-padding-bottom bm)
                                  (box-model-border-top bm) (box-model-border-bottom bm))])
                 (max 0 (- bb-h vert-pb)))
               raw-h))
         ;; apply min/max height even when height is auto
         (define min-h (resolve-min-height styles avail-h))
         (define max-h (resolve-max-height styles avail-h))
         (max min-h (min max-h ar-h))))

     ;; guard against +inf.0 in final sizes
     (define safe-content-h
       (if (and (number? final-content-h) (infinite? final-content-h))
           0
           final-content-h))

     (define border-box-w (compute-border-box-width bm final-content-w))
     (define border-box-h (compute-border-box-height bm safe-content-h))

     ;; lay out absolute/fixed children against the flex container
     ;; per CSS spec, the containing block for abspos is the PADDING BOX
     ;; containing block size = content + padding (no border)
     (define containing-w (+ final-content-w (box-model-padding-left bm) (box-model-padding-right bm)))
     (define containing-h (+ safe-content-h (box-model-padding-top bm) (box-model-padding-bottom bm)))
     ;; offset from border-box origin to padding-box origin
     (define abs-offset-x (box-model-border-left bm))
     (define abs-offset-y (box-model-border-top bm))
     (define abs-children
       (for/list ([child (in-list children)]
                  [dom-idx (in-naturals)]
                  #:when (let ([s (get-box-styles child)])
                           (let ([pos (get-style-prop s 'position 'static)])
                             (or (eq? pos 'absolute) (eq? pos 'fixed)))))
         (define view (layout-positioned child containing-w containing-h dispatch-fn))
         ;; offset the view position from padding-box to border-box coordinates
         (define raw-view (offset-view view abs-offset-x abs-offset-y))
         ;; check if insets are all auto (static position case)
         (define child-styles (get-box-styles child))
         (define css-top (get-style-prop child-styles 'top 'auto))
         (define css-right (get-style-prop child-styles 'right 'auto))
         (define css-bottom (get-style-prop child-styles 'bottom 'auto))
         (define css-left (get-style-prop child-styles 'left 'auto))
         (define child-w (view-width raw-view))
         (define child-h (view-height raw-view))

         ;; child's own margins for static position
         (define child-bm (extract-box-model child-styles content-w))
         (define child-ml (box-model-margin-left child-bm))
         (define child-mr (box-model-margin-right child-bm))
         (define child-mt (box-model-margin-top child-bm))
         (define child-mb (box-model-margin-bottom child-bm))
         ;; outer dimensions (border-box + margin)
         (define child-outer-w (+ child-w child-ml child-mr))
         (define child-outer-h (+ child-h child-mt child-mb))

         ;; compute static position offsets for auto-inset axes
         ;; static position is in the padding-box coordinate space, then offset to border-box
         ;; content area starts at padding-left, padding-top within the padding box
         ;; margin-start is added to position the child's margin edge at the static position
         (define pad-left (box-model-padding-left bm))
         (define pad-top (box-model-padding-top bm))

         (define static-main-offset
           (cond
             [(and (eq? css-left 'auto) (eq? css-right 'auto) is-row?)
              ;; justify-content determines main-axis static position
              (case justify
                [(center) (+ pad-left child-ml (max 0 (/ (- final-content-w child-outer-w) 2)))]
                [(flex-end end) (+ pad-left child-ml (max 0 (- final-content-w child-outer-w)))]
                [(flex-start start) (+ pad-left child-ml)]
                [else (+ pad-left child-ml)])]
             [(and (eq? css-top 'auto) (eq? css-bottom 'auto) (not is-row?))
              (case justify
                [(center) (+ pad-top child-mt (max 0 (/ (- safe-content-h child-outer-h) 2)))]
                [(flex-end end) (+ pad-top child-mt (max 0 (- safe-content-h child-outer-h)))]
                [(flex-start start) (+ pad-top child-mt)]
                [else (+ pad-top child-mt)])]
             [else #f]))

         (define static-cross-offset
           ;; resolve effective alignment: align-self overrides align-items
           (let* ([child-self-align
                   (get-style-prop child-styles 'align-self 'self-auto)]
                  [effective-align
                   (if (eq? child-self-align 'self-auto)
                       align-items
                       (case child-self-align
                         [(self-start) 'align-start]
                         [(self-end) 'align-end]
                         [(self-center) 'align-center]
                         [(self-stretch) 'align-stretch]
                         [else align-items]))]
                  ;; wrap-reverse inverts cross-start/cross-end
                  [wr-align (if (eq? wrap-mode 'wrap-reverse)
                                (case effective-align
                                  [(align-start align-stretch) 'align-end]
                                  [(align-end) 'align-start]
                                  [else effective-align])
                                effective-align)])
           (cond
             [(and (eq? css-top 'auto) (eq? css-bottom 'auto) is-row?)
              ;; alignment determines cross-axis static position
              (case wr-align
                [(align-center) (+ pad-top child-mt (max 0 (/ (- safe-content-h child-outer-h) 2)))]
                [(align-end) (+ pad-top child-mt (max 0 (- safe-content-h child-outer-h)))]
                [(align-start align-stretch) (+ pad-top child-mt)]
                [else (+ pad-top child-mt)])]
             [(and (eq? css-left 'auto) (eq? css-right 'auto) (not is-row?))
              (case wr-align
                [(align-center) (+ pad-left child-ml (max 0 (/ (- final-content-w child-outer-w) 2)))]
                [(align-end) (+ pad-left child-ml (max 0 (- final-content-w child-outer-w)))]
                [(align-start align-stretch) (+ pad-left child-ml)]
                [else (+ pad-left child-ml)])]
             [else #f])))

         ;; apply static position offsets
         ;; raw-view has coordinates in padding-box space (from layout-positioned)
         ;; offset by border to get border-box space
         (define final-abs-view
         (if (or static-main-offset static-cross-offset)
             (let* ([vx (view-x raw-view)]
                    [vy (view-y raw-view)]
                    [new-x (if is-row?
                               (if static-main-offset
                                   (+ abs-offset-x static-main-offset)
                                   (if static-cross-offset
                                       (+ abs-offset-x static-cross-offset)
                                       vx))
                               (if static-cross-offset
                                   (+ abs-offset-x static-cross-offset)
                                   (if static-main-offset
                                       vx
                                       vx)))]
                    [new-y (if is-row?
                               (if static-cross-offset
                                   (+ abs-offset-y static-cross-offset)
                                   (if static-main-offset
                                       vy
                                       vy))
                               (if static-main-offset
                                   (+ abs-offset-y static-main-offset)
                                   (if static-cross-offset
                                       vy
                                       vy)))])
               (set-view-pos raw-view
                             (if (and is-row? (eq? css-left 'auto) (eq? css-right 'auto))
                                 new-x
                                 (if (and (not is-row?) (eq? css-left 'auto) (eq? css-right 'auto))
                                     new-x
                                     vx))
                             (if (and is-row? (eq? css-top 'auto) (eq? css-bottom 'auto))
                                 new-y
                                 (if (and (not is-row?) (eq? css-top 'auto) (eq? css-bottom 'auto))
                                     new-y
                                     vy))))
             raw-view))
        ;; return (dom-index . view) pair for DOM-order interleaving
        (cons dom-idx final-abs-view)))

     ;; merge flex child-views and abs-children by DOM source order
     ;; child-views is a list of (child-index . view) pairs (from position-flex-items)
     ;; abs-children is a list of (dom-index . view) pairs
     (define all-indexed-views (append child-views abs-children))
     (define sorted-all-views
       (map cdr (sort all-indexed-views < #:key car)))
     (make-view id 0 0 border-box-w border-box-h sorted-all-views first-line-baseline)]

    [_ (error 'layout-flex "expected flex box, got: ~a" box)]))

;; ============================================================
;; Phase 1: Collect Flex Items
;; ============================================================

(define (collect-flex-items children container-styles is-row? main-avail cross-avail dispatch-fn
                             [containing-width #f])
  (for/list ([child (in-list children)]
             [child-idx (in-naturals)]
             #:unless (match child [`(none ,_) #t] [_ #f])
             #:unless (let ([s (get-box-styles child)])
                        (let ([pos (get-style-prop s 'position 'static)])
                          (or (eq? pos 'absolute) (eq? pos 'fixed)))))
    (define styles (get-box-styles child))
    (define bm (extract-box-model styles containing-width))
    (define order (get-style-prop styles 'order 0))
    (define grow (get-style-prop styles 'flex-grow 0))
    (define shrink (get-style-prop styles 'flex-shrink 1))
    (define basis-val (get-style-prop styles 'flex-basis 'auto))

    ;; axis-aware padding/border/margin
    (define main-pb (if is-row? (horizontal-pb bm) (vertical-pb bm)))
    (define main-margin (if is-row? (horizontal-margin bm) (vertical-margin bm)))

    ;; resolve flex-basis
    (define aspect-ratio (get-style-prop styles 'aspect-ratio #f))
    (define basis
      (cond
        [(eq? basis-val 'auto)
         ;; auto basis: use width/height or content size
         (define size-prop (if is-row? 'width 'height))
         (define size-val (get-style-prop styles size-prop 'auto))
         (define resolved (resolve-size-value size-val main-avail))
         (cond
           [resolved
            ;; if box-sizing: border-box, convert to content size
            (if (eq? (box-model-box-sizing bm) 'border-box)
                (max 0 (- resolved main-pb))
                resolved)]
           ;; aspect-ratio: derive main from cross dimension
           [(and aspect-ratio (number? aspect-ratio) (> aspect-ratio 0))
            (define cross-prop (if is-row? 'height 'width))
            ;; for row flex: resolve percentage cross (height) against container's cross size
            ;; for column flex: resolve percentage cross (width) against main-avail
            (define cross-ref (if is-row? cross-avail main-avail))
            (define cross-val (resolve-size-value (get-style-prop styles cross-prop 'auto) cross-ref))
            (if cross-val
                ;; cross is known, derive main via ratio
                ;; aspect-ratio = width/height
                ;; for row: main = width = height * ratio = cross-val * ratio
                ;; for column: main = height = width / ratio = cross-val / ratio
                (let ([derived-main-border (if is-row?
                                               (* cross-val aspect-ratio)
                                               (/ cross-val aspect-ratio))])
                  (max 0 (- derived-main-border main-pb)))
                ;; cross also auto → check if min-cross can seed AR derivation
                (let* ([cross-pb (if is-row? (vertical-pb bm) (horizontal-pb bm))]
                       [min-cross-prop (if is-row? 'min-height 'min-width)]
                       [min-cross-val (resolve-size-value (get-style-prop styles min-cross-prop 'auto) cross-ref)]
                       [min-cross (if min-cross-val
                                      (if (eq? (box-model-box-sizing bm) 'border-box)
                                          (max 0 (- min-cross-val cross-pb))
                                          min-cross-val)
                                      #f)])
                  (if (and min-cross (> min-cross 0))
                      ;; min-cross seeds AR: derive main from min-cross
                      (let ([derived-main-border (if is-row?
                                                     (* (+ min-cross cross-pb) aspect-ratio)
                                                     (/ (+ min-cross cross-pb) aspect-ratio))])
                        (max 0 (- derived-main-border main-pb)))
                      ;; no min-cross → check max-cross for AR constraint
                      ;; per CSS: when aspect-ratio + max-cross, derive basis from max-cross
                      (let* ([max-cross-prop (if is-row? 'max-height 'max-width)]
                             [max-cross-val (resolve-size-value (get-style-prop styles max-cross-prop 'none) cross-ref)]
                             [max-cross (if max-cross-val
                                            (if (eq? (box-model-box-sizing bm) 'border-box)
                                                max-cross-val  ;; border-box includes pb
                                                (+ max-cross-val cross-pb))
                                            #f)])
                        (if (and max-cross (< max-cross +inf.0))
                            ;; derive basis from max-cross via aspect-ratio
                            ;; then clamp by content measurement (basis shouldn't exceed content)
                            (let* ([derived-main-border (if is-row?
                                                            (* max-cross aspect-ratio)
                                                            (/ max-cross aspect-ratio))]
                                   [derived-main (max 0 (- derived-main-border main-pb))]
                                   ;; also measure content to get the max-content basis
                                   [measured (measure-flex-item-content child is-row? main-avail cross-avail dispatch-fn)]
                                   [content-main (max 0 (- measured main-pb))])
                              ;; use the smaller: the AR-transferred size or the content size
                              (min derived-main content-main))
                            ;; no max-cross → check if max-main can constrain via AR
                            ;; When max-main is set, the AR-derived cross at max-main may be
                            ;; smaller than the container cross, affecting text wrapping.
                            ;; Measure content at the AR-derived cross width for accuracy.
                            (let* ([max-main-prop (if is-row? 'max-width 'max-height)]
                                   [max-main-val (resolve-size-value (get-style-prop styles max-main-prop 'none) main-avail)]
                                   [max-main-content (if (and max-main-val (not (infinite? max-main-val)))
                                                        (if (eq? (box-model-box-sizing bm) 'border-box)
                                                            (max 0 (- max-main-val main-pb))
                                                            max-main-val)
                                                        #f)]
                                   ;; derive the cross width from max-main via AR
                                   [ar-cross-at-max (if max-main-content
                                                        (if is-row?
                                                            (/ (+ max-main-content main-pb) aspect-ratio)
                                                            (* (+ max-main-content main-pb) aspect-ratio))
                                                        #f)]
                                   ;; use the AR-derived cross as the measurement width
                                   [eff-cross (if ar-cross-at-max
                                                  (min (or cross-avail +inf.0) ar-cross-at-max)
                                                  cross-avail)]
                                   [measured (measure-flex-item-content child is-row? main-avail eff-cross dispatch-fn)])
                              (max 0 (- measured main-pb))))))))]
           [else
            ;; need to measure content — result is border-box, convert to content-box
            (define measured (measure-flex-item-content child is-row? main-avail cross-avail dispatch-fn))
            (max 0 (- measured main-pb))])]
        [else
         (define resolved-basis (or (resolve-size-value basis-val main-avail) 0))
         ;; if box-sizing: border-box, convert to content size
         (if (eq? (box-model-box-sizing bm) 'border-box)
             (max 0 (- resolved-basis main-pb))
             resolved-basis)]))

    ;; compute hypothetical main size (basis clamped by min/max)
    (define min-prop (if is-row? 'min-width 'min-height))
    (define max-prop (if is-row? 'max-width 'max-height))
    (define min-main-raw (resolve-size-value (get-style-prop styles min-prop 'auto) main-avail))
    (define max-main-raw (resolve-size-value (get-style-prop styles max-prop 'none) main-avail))
    ;; convert min/max for border-box items (they specify border-box size, not content size)
    (define min-main
      (cond
        [min-main-raw
         ;; explicit min-width/min-height was specified
         (let ([raw min-main-raw])
           (if (and (> raw 0) (eq? (box-model-box-sizing bm) 'border-box))
               (max 0 (- raw main-pb))
               raw))]
        [else
         ;; min-width/min-height is auto → automatic minimum size (CSS Flexbox §4.5)
         ;; Per spec: automatic minimum is 0 when:
         ;; - overflow is not visible (hidden, scroll, auto, etc.)
         ;; - item has a definite main-axis size (specified size suggestion applies)
         ;; Otherwise use content-based minimum (measure the item).
         (define overflow-prop (get-style-prop styles 'overflow 'visible))
         (define overflow-x (get-style-prop styles 'overflow-x overflow-prop))
         (define overflow-y (get-style-prop styles 'overflow-y overflow-prop))
         (define main-overflow (if is-row? overflow-x overflow-y))
         (define size-prop (if is-row? 'width 'height))
         (define has-explicit-main?
           (not (eq? (get-style-prop styles size-prop 'auto) 'auto)))
         (cond
           ;; overflow not visible → auto min = 0
           [(not (eq? main-overflow 'visible)) 0]
           ;; item has definite main size → auto min = min(specified, content-based)
           ;; per CSS Flexbox §4.5: "the automatic minimum size is the smaller of
           ;; the item's content size suggestion and its specified size suggestion"
           ;; content size suggestion = min-content with explicit main size stripped
           [has-explicit-main?
            (define measured (measure-flex-item-content-min child is-row? main-avail cross-avail dispatch-fn))
            (define content-min (max 0 (- measured main-pb)))
            (min basis content-min)]
           [else
            ;; content-based minimum: measure item's min-content contribution
            ;; per CSS Flexbox §4.5: automatic minimum = min-content size
            ;; result is border-box, convert to content-box
            (define measured (measure-flex-item-content-min child is-row? main-avail cross-avail dispatch-fn))
            (max 0 (- measured main-pb))])]))
    (define max-main
      (let ([raw (or max-main-raw +inf.0)])
        (if (and (not (infinite? raw)) (eq? (box-model-box-sizing bm) 'border-box))
            (max 0 (- raw main-pb))
            raw)))
    ;; per CSS Flexbox §4.5: automatic minimum size is clamped by max-main if definite
    ;; this applies when min-main was computed from content (min-main-raw is #f = auto)
    (define clamped-min-main
      (if (and (not min-main-raw) (< max-main +inf.0))
          (min min-main max-main)
          min-main))
    (define hyp-main (max clamped-min-main (min max-main basis)))

    ;; outer hypothetical = hyp-main + padding/border + margin on main axis
    (flex-item child styles bm order grow shrink basis
              (+ hyp-main main-pb main-margin)
              clamped-min-main max-main
              hyp-main 0 #f child-idx 0)))

;; measure content size of a flex item when flex-basis is auto and width/height is auto
;; We must strip min/max constraints on the main axis before measuring,
;; because those are applied separately in the flex algorithm (§9.7).
;; Otherwise the measurement inflates the basis to include min-width/min-height.
(define (measure-flex-item-content child is-row? main-avail cross-avail dispatch-fn)
  ;; override min/max on the measured axis so they don't affect intrinsic sizing
  (define child-styles (get-box-styles child))
  ;; prepend overrides — get-style-prop finds first match
  (define stripped-styles
    (match child-styles
      [`(style . ,props)
       `(style ,(list (if is-row? 'min-width 'min-height) 'auto)
               ,(list (if is-row? 'max-width 'max-height) 'none)
               ,@props)]))
  ;; rebuild the box with stripped styles
  (define stripped-child
    (match child
      [`(,type ,id ,_styles . ,rest) `(,type ,id ,stripped-styles ,@rest)]
      [_ child]))
  ;; for row flex: main=width (max-content), cross=height (definite main-avail is parent height)
  ;; for column flex: main=height (max-content), cross=width (use cross-avail = container width)
  (define cross-spec
    (cond
      [(not is-row?)
       ;; column flex: cross axis is width → use cross-avail (container width) for wrapping
       (if (and (number? cross-avail) (not (infinite? cross-avail)))
           `(definite ,cross-avail)
           'av-max-content)]
      [else
       ;; row flex: cross axis is height → use main-avail as height context
       `(definite ,main-avail)]))
  (define measure-avail
    `(avail ,(if is-row? 'av-max-content cross-spec)
            ,(if is-row? cross-spec 'av-max-content)))
  (define view (dispatch-fn stripped-child measure-avail))
  (if is-row? (view-width view) (view-height view)))

;; measure min-content size of a flex item with explicit main size stripped
;; used for "content size suggestion" in CSS Flexbox §4.5 when item has a specified main size
(define (measure-flex-item-content-min child is-row? main-avail cross-avail dispatch-fn)
  (define child-styles (get-box-styles child))
  (define main-prop (if is-row? 'width 'height))
  ;; strip explicit main size, min/max so measurement reflects pure content minimum
  (define stripped-styles
    (match child-styles
      [`(style . ,props)
       `(style ,(list main-prop 'auto)
               ,(list (if is-row? 'min-width 'min-height) 'auto)
               ,(list (if is-row? 'max-width 'max-height) 'none)
               ,@props)]))
  (define stripped-child
    (match child
      [`(,type ,id ,_styles . ,rest) `(,type ,id ,stripped-styles ,@rest)]
      [_ child]))
  ;; for column flex: cross axis is width → pass cross-avail (container width) for wrapping
  (define cross-spec
    (cond
      [(not is-row?)
       (if (and (number? cross-avail) (not (infinite? cross-avail)))
           `(definite ,cross-avail)
           'av-min-content)]
      [else `(definite ,main-avail)]))
  ;; measure at min-content on main axis
  (define measure-avail
    `(avail ,(if is-row? 'av-min-content cross-spec)
            ,(if is-row? cross-spec 'av-min-content)))
  (define view (dispatch-fn stripped-child measure-avail))
  (if is-row? (view-width view) (view-height view)))

;; ============================================================
;; Phase 4: Create Flex Lines
;; ============================================================

(define (create-flex-lines items main-avail main-gap wrap-mode)
  (cond
    ;; nowrap: all items on one line
    [(eq? wrap-mode 'nowrap)
     (list (flex-line items
                      (items-total-main items main-gap)
                      0 0))]
    ;; wrap: break lines when exceeding main-avail
    [else
     (let loop ([remaining items]
                [current-line '()]
                [current-main 0]
                [lines '()])
       (cond
         [(null? remaining)
          (reverse
           (if (null? current-line)
               lines
               (cons (flex-line (reverse current-line) current-main 0 0)
                     lines)))]
         [else
          (define item (car remaining))
          (define item-main (flex-item-hypothetical-main item))
          (define gap (if (null? current-line) 0 main-gap))
          (define new-main (+ current-main gap item-main))
          (cond
            ;; fits on current line (or line is empty)
            [(or (null? current-line) (<= new-main main-avail))
             (loop (cdr remaining)
                   (cons item current-line)
                   new-main
                   lines)]
            ;; start new line
            [else
             (loop remaining
                   '()
                   0
                   (cons (flex-line (reverse current-line) current-main 0 0)
                         lines))])]))]))

(define (items-total-main items main-gap)
  (define n (length items))
  (+ (for/sum ([item (in-list items)])
       (flex-item-hypothetical-main item))
     (* main-gap (max 0 (sub1 n)))))

;; ============================================================
;; Phase 5: Resolve Flexible Lengths (CSS Flexbox §9.7)
;; ============================================================

(define (resolve-flex-lengths line main-avail main-gap is-row? main-definite?)
  (define items (flex-line-items line))
  (define n (length items))
  (define total-gaps (* main-gap (max 0 (sub1 n))))

  ;; helper: outer padding+border+margin for an item on the main axis
  (define (item-outer-pb+m item)
    (define bm (flex-item-bm item))
    (+ (if is-row? (horizontal-pb bm) (vertical-pb bm))
       (if is-row? (horizontal-margin bm) (vertical-margin bm))))

  ;; when main-avail is indefinite, items stay at their hypothetical sizes
  ;; (flex-grow/shrink don't apply without a definite main size)
  ;; but min/max constraints still apply
  (when (not main-definite?)
    (for ([item (in-list items)])
      (define basis (flex-item-flex-basis item))
      (define clamped (max (flex-item-min-main item)
                          (min (flex-item-max-main item) basis)))
      (set-flex-item-main-size! item clamped)))

  (when main-definite?
    (define available (- main-avail total-gaps))

    ;; CSS Flexbox §9.7: Resolving Flexible Lengths
    ;; Uses a freeze-and-redistribute loop:
    ;; 1. Determine growing vs shrinking
    ;; 2. Freeze inflexible items
    ;; 3. Loop: distribute free space, freeze items that violate min/max, repeat

    ;; track frozen state per item (vector for O(1) access)
    (define frozen (make-vector n #f))

    ;; Step 1: determine if line is growing or shrinking
    (define total-hyp-outer
      (for/sum ([item (in-list items)])
        (flex-item-hypothetical-main item)))
    (define growing? (>= (- available total-hyp-outer) 0))

    ;; helper: content-level hypothetical main size
    (define (hyp-main item)
      (- (flex-item-hypothetical-main item) (item-outer-pb+m item)))

    ;; Step 2: freeze inflexible items at their hypothetical main size
    (for ([item (in-list items)]
          [i (in-naturals)])
      (define flex-factor (if growing? (flex-item-flex-grow item) (flex-item-flex-shrink item)))
      (cond
        [(= flex-factor 0)
         (set-flex-item-main-size! item (hyp-main item))
         (vector-set! frozen i #t)]
        [(and growing? (> (flex-item-flex-basis item) (hyp-main item)))
         (set-flex-item-main-size! item (hyp-main item))
         (vector-set! frozen i #t)]
        [(and (not growing?) (< (flex-item-flex-basis item) (hyp-main item)))
         (set-flex-item-main-size! item (hyp-main item))
         (vector-set! frozen i #t)]
        [else (void)]))

    ;; Step 3: Loop — distribute free space, freeze violators, repeat
    (let loop ()
      ;; check if any unfrozen items remain
      (define has-unfrozen?
        (for/or ([i (in-range n)])
          (not (vector-ref frozen i))))
      (when has-unfrozen?
        ;; calculate remaining free space
        (define free-space
          (- available
             (for/sum ([item (in-list items)]
                       [i (in-naturals)])
               (if (vector-ref frozen i)
                   (+ (flex-item-main-size item) (item-outer-pb+m item))
                   (+ (flex-item-flex-basis item) (item-outer-pb+m item))))))

        ;; calculate total flex factor of unfrozen items
        (define total-flex
          (for/sum ([item (in-list items)]
                    [i (in-naturals)])
            (if (vector-ref frozen i) 0
                (if growing?
                    (flex-item-flex-grow item)
                    (* (flex-item-flex-shrink item) (flex-item-flex-basis item))))))

        ;; distribute free space to unfrozen items
        ;; CSS spec: when total flex < 1, items receive (factor × free-space)
        (define use-fraction? (and (> total-flex 0) (< total-flex 1)))
        (for ([item (in-list items)]
              [i (in-naturals)])
          (unless (vector-ref frozen i)
            (define basis (flex-item-flex-basis item))
            (define flex-factor
              (if growing?
                  (flex-item-flex-grow item)
                  (* (flex-item-flex-shrink item) basis)))
            (define distributed
              (cond
                [(or (= total-flex 0) (= free-space 0)) 0]
                [use-fraction? (* free-space flex-factor)]
                [else (* free-space (/ flex-factor total-flex))]))
            (define target (+ basis distributed))
            (set-flex-item-main-size! item target)))

        ;; fix min/max violations and accumulate total adjustment
        (define total-violation 0)
        (for ([item (in-list items)]
              [i (in-naturals)])
          (unless (vector-ref frozen i)
            (define target (flex-item-main-size item))
            (define clamped (max (flex-item-min-main item)
                                (min (flex-item-max-main item) target)))
            (set! total-violation (+ total-violation (- clamped target)))
            (set-flex-item-main-size! item clamped)))

        ;; freeze items based on violation direction
        (cond
          [(= total-violation 0)
           ;; no violations — freeze all unfrozen items
           (for ([i (in-range n)])
             (vector-set! frozen i #t))]
          [(> total-violation 0)
           ;; min violations dominate — freeze items that hit their min
           (for ([item (in-list items)]
                 [i (in-naturals)])
             (unless (vector-ref frozen i)
               (when (<= (flex-item-main-size item) (flex-item-min-main item))
                 (vector-set! frozen i #t))))]
          [else
           ;; max violations dominate — freeze items that hit their max
           (for ([item (in-list items)]
                 [i (in-naturals)])
             (unless (vector-ref frozen i)
               (when (>= (flex-item-main-size item) (flex-item-max-main item))
                 (vector-set! frozen i #t))))])

        ;; continue loop if we froze some but not all items
        (unless (= total-violation 0)
          (loop)))))

  ;; recompute line free space after resolution
  (define new-used
    (+ total-gaps
       (for/sum ([item (in-list items)])
         (+ (flex-item-main-size item) (item-outer-pb+m item)))))

  (set-flex-line-main-size! line new-used)
  ;; guard: when main-avail is infinite, free-space must be 0
  ;; (prevents +inf.0 from propagating into justify-content positioning)
  (set-flex-line-free-space! line
    (if (and (number? main-avail) (not (infinite? main-avail)))
        (- main-avail new-used)
        0))
  line)

;; ============================================================
;; Helper: Override percentage main-axis size for child dispatch
;; ============================================================

;; when a flex item has a percentage main-axis size (e.g. height:50% in column),
;; override it with the flex-resolved definite value before dispatching.
;; this prevents re-resolution of the percentage against the item's own avail,
;; which would produce incorrect (halved) sizes for nested children.
(define (override-pct-main-size item child-main-border-box is-row?)
  (define main-prop (if is-row? 'width 'height))
  (define main-val (get-style-prop (flex-item-styles item) main-prop 'auto))
  (if (and (pair? main-val) (eq? (car main-val) '%))
      ;; replace percentage with definite px value
      (let* ([new-styles
              (match (flex-item-styles item)
                [`(style . ,props)
                 `(style ,@(map (lambda (p)
                                  (if (and (pair? p) (eq? (car p) main-prop))
                                      `(,main-prop (px ,child-main-border-box))
                                      p))
                                props))])]
             [new-box
              (match (flex-item-box item)
                [`(,type ,id ,_ . ,rest) `(,type ,id ,new-styles ,@rest)]
                [_ (flex-item-box item)])])
        new-box)
      (flex-item-box item)))

;; inject a definite cross-axis size into a box's styles for stretch dispatch.
;; per CSS Flexbox spec §9.4.8: "the flex item's cross size is treated as definite"
;; after stretch. This ensures nested flex containers see the stretched size as an
;; explicit (definite) cross dimension, enabling their children to flex-grow/stretch.
(define (override-cross-size-in-box box cross-size is-row?)
  (define cross-prop (if is-row? 'height 'width))
  (define item-styles (get-box-styles box))
  (define cross-val (get-style-prop item-styles cross-prop 'auto))
  ;; only inject if the cross-axis size is auto (not already set)
  (if (eq? cross-val 'auto)
      (let* ([new-styles
              (match item-styles
                [`(style . ,props)
                 `(style (,cross-prop (px ,cross-size)) ,@props)])]
             [new-box
              (match box
                [`(,type ,id ,_ . ,rest) `(,type ,id ,new-styles ,@rest)]
                [_ box])])
        new-box)
      box))

;; ============================================================
;; Phase 6: Determine Cross Sizes
;; ============================================================

(define (determine-cross-sizes lines cross-avail align-items is-row? wrap-mode align-content dispatch-fn
                              [cross-definite? #f] [max-cross +inf.0] [main-avail +inf.0])
  (define single-line? (= (length lines) 1))
  ;; for single-line containers, expand line cross to cross-avail ONLY if:
  ;; - nowrap (align-content doesn't apply)
  ;; - wrapping with align-content: stretch
  (define expand-single-line?
    (and single-line?
         (or (eq? wrap-mode 'nowrap)
             (eq? align-content 'content-stretch))))

  (for/list ([line (in-list lines)])
    (define items (flex-line-items line))

    ;; first pass: lay out each item to determine its natural cross size
    (for ([item (in-list items)])
      (define main-size (flex-item-main-size item))
      (define item-bm (flex-item-bm item))
      (define main-pb (if is-row? (horizontal-pb item-bm) (vertical-pb item-bm)))
      ;; pass main-size as the border-box main to the child
      (define child-main (+ main-size main-pb))
      ;; determine item's effective alignment for cross-axis sizing
      (define item-styles (flex-item-styles item))
      (define self-align (get-style-prop item-styles 'align-self 'self-auto))
      (define item-effective-align
        (if (eq? self-align 'self-auto) align-items
            (case self-align
              [(self-stretch) 'align-stretch]
              [(self-start) 'align-start]
              [(self-end) 'align-end]
              [(self-center) 'align-center]
              [(self-baseline) 'align-baseline]
              [else align-items])))
      ;; for the cross axis: pass cross-avail as definite if available
      ;; for single-line containers, use full cross-avail (for stretch items)
      ;; for multi-line wrapping containers, use indefinite so items get
      ;; their natural cross size (align-content:stretch will re-stretch later)
      ;; for non-stretch items (center/start/end/baseline), use indefinite
      ;; so they shrink-to-fit to content (CSS §9.4 step 8)
      (define is-multi-line-wrapping? (not (eq? wrap-mode 'nowrap)))
      (define cross-size-prop (if is-row? 'height 'width))
      (define cross-size-val (get-style-prop item-styles cross-size-prop 'auto))
      (define has-item-explicit-cross?
        (not (eq? cross-size-val 'auto)))
      ;; detect percentage cross sizes — they resolve to auto when cross-avail is indefinite
      (define is-pct-cross? (match cross-size-val [`(% ,_) #t] [_ #f]))
      ;; also detect percentage min/max on cross axis (need definite cross-avail to resolve)
      (define min-cross-prop (if is-row? 'min-height 'min-width))
      (define max-cross-prop (if is-row? 'max-height 'max-width))
      (define has-pct-min-max-cross?
        (or (match (get-style-prop item-styles min-cross-prop 'auto) [`(% ,_) #t] [_ #f])
            (match (get-style-prop item-styles max-cross-prop 'none) [`(% ,_) #t] [_ #f])))
      (define cross-avail-spec
        (cond
          [(and is-multi-line-wrapping? (> (length lines) 1))
           ;; multi-line: don't constrain items to full container cross
           'indefinite]
          ;; non-stretch items without explicit cross size should shrink-to-fit
          ;; UNLESS they have percentage min/max that needs a definite reference
          ;; EXCEPTION: wrapping flex containers need definite cross-avail for line-breaking
          [(and (not (eq? item-effective-align 'align-stretch))
                (not has-item-explicit-cross?)
                (not has-pct-min-max-cross?))
           ;; check if this item is a wrapping flex container
           (define child-box (flex-item-box item))
           (define child-styles (get-box-styles child-box))
           (define child-wrap (get-style-prop child-styles 'flex-wrap 'nowrap))
           (define is-wrapping-child? (not (eq? child-wrap 'nowrap)))
           (if (and is-wrapping-child?
                    (number? cross-avail)
                    (not (infinite? cross-avail)))
               `(definite ,cross-avail)
               'indefinite)]
          ;; percentage cross sizes with indefinite cross-avail (0 from auto width) →
          ;; treat as indefinite so the percentage resolves to auto
          [(and is-pct-cross? (or (not (number? cross-avail))
                                   (infinite? cross-avail)
                                   (= cross-avail 0)))
           'indefinite]
          [(and (number? cross-avail) (not (infinite? cross-avail)))
           `(definite ,cross-avail)]
          [else 'indefinite]))
      ;; aspect-ratio cross constraint: when item has AR + auto cross + indefinite cross-avail,
      ;; derive a definite cross from the resolved main via AR so the child layout is constrained.
      ;; This is needed for cases like column flex + align-items:start + max-height + aspect-ratio,
      ;; where the AR-derived width must constrain the child's content layout.
      (define ar-cross-avail-spec
        (let ([ar (get-style-prop item-styles 'aspect-ratio #f)])
          (if (and ar (number? ar) (> ar 0)
                   (not has-item-explicit-cross?)
                   (eq? cross-avail-spec 'indefinite))
              ;; derive cross (border-box) from child-main (border-box) via AR
              ;; aspect-ratio = width/height
              ;; for row: cross=height, child-main=width → cross = width/ratio
              ;; for column: cross=width, child-main=height → cross = height*ratio
              (let ([derived-cross (if is-row?
                                      (/ child-main ar)
                                      (* child-main ar))])
                `(definite ,derived-cross))
              cross-avail-spec)))
      (define child-avail
        (if is-row?
            `(avail (definite ,child-main) ,ar-cross-avail-spec)
            `(avail ,ar-cross-avail-spec (definite ,child-main))))
      ;; override percentage main-axis sizes to prevent re-resolution
      (define dispatch-box0 (override-pct-main-size item child-main is-row?))
      ;; always inject flex-resolved main-size as definite so nested flex containers
      ;; distribute space correctly (CSS Flexbox §9.4)
      ;; this is critical when flex-grow/shrink changes the item's size from its CSS value
      (define main-prop (if is-row? 'width 'height))
      (define dispatch-box
        (let* ([item-styles (get-box-styles dispatch-box0)]
               [new-styles
                (match item-styles
                  [`(style . ,props)
                   `(style (,main-prop (px ,child-main))
                           ,@(filter (lambda (p) (not (and (pair? p) (eq? (car p) main-prop)))) props))])]
               [new-box
                (match dispatch-box0
                  [`(,type ,id ,_ . ,rest) `(,type ,id ,new-styles ,@rest)]
                  [_ dispatch-box0])])
          new-box))
      (define child-view (dispatch-fn dispatch-box child-avail))
      (set-flex-item-view! item child-view)
      (define raw-cross (if is-row? (view-height child-view) (view-width child-view)))
      ;; aspect-ratio: if the item has auto cross dimension and aspect-ratio,
      ;; derive cross from the resolved main size rather than using the dispatched size.
      ;; However, skip this if there's a max-cross constraint (max-width for column,
      ;; max-height for row) — in that case, let the post-cross AR derivation handle
      ;; it by applying max-cross and re-deriving main from the constrained cross.
      (define aspect-ratio (get-style-prop item-styles 'aspect-ratio #f))
      (define cross-dim-prop (if is-row? 'height 'width))
      (define has-explicit-cross-dim?
        (not (eq? (get-style-prop item-styles cross-dim-prop 'auto) 'auto)))
      (define max-cross-dim-prop (if is-row? 'max-height 'max-width))
      (define has-max-cross-constraint?
        (not (eq? (get-style-prop item-styles max-cross-dim-prop 'none) 'none)))
      (define cross
        (if (and aspect-ratio (number? aspect-ratio) (> aspect-ratio 0)
                 (not has-explicit-cross-dim?)
                 (not has-max-cross-constraint?))  ;; no max-cross → derive from main
            (let ([main-border (+ main-size (if is-row? (horizontal-pb (flex-item-bm item)) (vertical-pb (flex-item-bm item))))])
              ;; aspect-ratio = width/height
              ;; for row: cross = height = width / ratio = main-border / ratio
              ;; for column: cross = width = height * ratio = main-border * ratio
              (define derived (if is-row?
                                  (/ main-border aspect-ratio)
                                  (* main-border aspect-ratio)))
              ;; subtract cross pb to get content-box cross
              (define cross-pb (if is-row? (vertical-pb (flex-item-bm item)) (horizontal-pb (flex-item-bm item))))
              (max 0 (- derived cross-pb)))
            raw-cross))
      (set-flex-item-cross-size! item cross)

      ;; aspect-ratio: for non-stretch items with aspect-ratio and no explicit main size,
      ;; derive main from the resolved cross size (cross may be constrained by max-width/min-width)
      ;; per CSS Flexbox §9.4: use cross size to derive main when aspect-ratio applies
      (when (and aspect-ratio (number? aspect-ratio) (> aspect-ratio 0)
                 (not (eq? item-effective-align 'align-stretch)))
        (define main-size-prop (if is-row? 'width 'height))
        (define has-item-explicit-main?
          (not (eq? (get-style-prop item-styles main-size-prop 'auto) 'auto)))
        (when (not has-item-explicit-main?)
          (define item-bm (flex-item-bm item))
          (define cross-pb (if is-row? (vertical-pb item-bm) (horizontal-pb item-bm)))
          (define main-pb-ar (if is-row? (horizontal-pb item-bm) (vertical-pb item-bm)))
          (define cross-border (+ cross cross-pb))
          ;; apply max-cross constraint to cross-border
          (define max-cross-prop (if is-row? 'max-height 'max-width))
          (define max-cross-val (resolve-size-value (get-style-prop item-styles max-cross-prop 'none) cross-avail))
          (define eff-cross-border
            (if max-cross-val (min cross-border max-cross-val) cross-border))
          (define derived-main-border (if is-row?
                                          (* eff-cross-border aspect-ratio)
                                          (/ eff-cross-border aspect-ratio)))
          (define derived-main (max 0 (- derived-main-border main-pb-ar)))
          (set-flex-item-main-size! item derived-main)
          ;; also update cross to reflect the capped value (max-cross may have reduced it)
          (when (and max-cross-val (< eff-cross-border cross-border))
            (set-flex-item-cross-size! item (max 0 (- eff-cross-border cross-pb))))))

      ;; compute baseline for baseline alignment (only meaningful for row direction)
      (when is-row?
        (set-flex-item-baseline! item (compute-view-baseline child-view))))

    ;; line cross size = max of item cross sizes (outer: cross + cross margin)
    ;; for baseline-aligned items, need to account for baseline shift
    (define-values (natural-line-cross max-baseline)
      (let ()
        ;; first compute max-cross from all items normally
        (define max-cross
          (for/fold ([mx 0])
                    ([item (in-list items)])
            (define item-bm (flex-item-bm item))
            (define cross-margin (if is-row? (vertical-margin item-bm) (horizontal-margin item-bm)))
            (max mx (+ (flex-item-cross-size item) cross-margin))))
        ;; compute baseline-specific metrics for row direction
        (if is-row?
            (let* ([baseline-items
                    (filter (lambda (item)
                              (define s (get-style-prop (flex-item-styles item) 'align-self 'self-auto))
                              (define eff (if (eq? s 'self-auto) align-items
                                             (case s
                                               [(self-baseline) 'align-baseline]
                                               [(self-stretch) 'align-stretch]
                                               [(self-start) 'align-start]
                                               [(self-end) 'align-end]
                                               [(self-center) 'align-center]
                                               [else align-items])))
                              (eq? eff 'align-baseline))
                            items)]
                   [max-bl (if (null? baseline-items) 0
                               (apply max (map (lambda (item)
                                                 (define item-bm (flex-item-bm item))
                                                 (define mt (box-model-margin-top item-bm))
                                                 (+ mt (flex-item-baseline item)))
                                               baseline-items)))]
                   [max-below (if (null? baseline-items) 0
                                  (apply max (map (lambda (item)
                                                    (define item-bm (flex-item-bm item))
                                                    (define mt (box-model-margin-top item-bm))
                                                    (define mb (box-model-margin-bottom item-bm))
                                                    (- (+ mt (flex-item-cross-size item) mb) (+ mt (flex-item-baseline item))))
                                                  baseline-items)))]
                   [baseline-cross (+ max-bl max-below)])
              (values (max max-cross baseline-cross) max-bl))
            (values max-cross 0))))

    ;; for single-line flex containers (nowrap or wrap+stretch),
    ;; the line cross equals the container's definite cross size (CSS Flexbox §9.4 step 8)
    ;; for nowrap with definite cross: unconditionally set line-cross to cross-avail
    ;; (CSS spec: "If the flex container is single-line and has a definite cross size,
    ;;  the cross size of the flex line is the flex container's inner cross size.")
    ;; for wrap with single line: only expand (never clamp), since content can overflow
    (define line-cross
      (cond
        ;; nowrap + definite cross → unconditional (spec §9.4 step 8)
        [(and expand-single-line?
              (eq? wrap-mode 'nowrap)
              cross-definite?
              (number? cross-avail)
              (not (infinite? cross-avail)))
         cross-avail]
        ;; nowrap + max-cross constrains → clamp line cross by max-cross
        ;; per CSS Flexbox §9.4: when cross size is indefinite but max-cross is definite,
        ;; the line cross is the natural line cross clamped by max-cross
        [(and expand-single-line?
              (eq? wrap-mode 'nowrap)
              (not cross-definite?)
              (< max-cross +inf.0))
         (min natural-line-cross max-cross)]
        ;; wrap single-line + content-stretch → expand to fill, but don't clamp
        [(and expand-single-line?
              (number? cross-avail)
              (not (infinite? cross-avail))
              (> cross-avail natural-line-cross))
         cross-avail]
        [else natural-line-cross]))

    ;; stretch items if needed, and re-layout with definite cross size
    (for ([item (in-list items)])
      (define item-styles (flex-item-styles item))
      (define self-align (get-style-prop item-styles 'align-self 'self-auto))
      (define effective-align
        (if (eq? self-align 'self-auto) align-items
            (case self-align
              [(self-stretch) 'align-stretch]
              [(self-start) 'align-start]
              [(self-end) 'align-end]
              [(self-center) 'align-center]
              [(self-baseline) 'align-baseline]
              [else align-items])))

      ;; check if the item has explicit cross-axis size
      (define cross-size-prop (if is-row? 'height 'width))
      (define has-explicit-cross?
        (not (eq? (get-style-prop item-styles cross-size-prop 'auto) 'auto)))

      (when (and (eq? effective-align 'align-stretch)
                 (not has-explicit-cross?))
        ;; stretch cross size to line cross (minus item's cross margin)
        (define item-bm (flex-item-bm item))
        (define cross-margin (if is-row? (vertical-margin item-bm) (horizontal-margin item-bm)))
        (define raw-stretched (max 0 (- line-cross cross-margin)))
        ;; clamp by min/max on cross axis
        (define min-cross-prop (if is-row? 'min-height 'min-width))
        (define max-cross-prop (if is-row? 'max-height 'max-width))
        (define min-cross-val (resolve-size-value (get-style-prop item-styles min-cross-prop 'auto) cross-avail))
        (define max-cross-val (resolve-size-value (get-style-prop item-styles max-cross-prop 'none) cross-avail))
        (define min-cross (or min-cross-val 0))
        (define max-cross-lim (or max-cross-val +inf.0))
        (define stretched-cross (max min-cross (min max-cross-lim raw-stretched)))
        (set-flex-item-cross-size! item stretched-cross)

        ;; aspect-ratio re-derivation: if item has aspect-ratio and no explicit main-axis size,
        ;; derive main from the now-definite stretched cross size
        (define aspect-ratio (get-style-prop item-styles 'aspect-ratio #f))
        (define main-size-prop (if is-row? 'width 'height))
        (define has-explicit-main?
          (not (eq? (get-style-prop item-styles main-size-prop 'auto) 'auto)))
        (define main-size0 (flex-item-main-size item))
        (define main-size
          (if (and aspect-ratio (number? aspect-ratio) (> aspect-ratio 0)
                   (not has-explicit-main?))
              ;; aspect-ratio = width/height
              ;; for row: main = width, cross = height → main-border = cross-border * ratio
              ;; for column: main = height, cross = width → main-border = cross-border / ratio
              (let* ([cross-pb (if is-row? (vertical-pb item-bm) (horizontal-pb item-bm))]
                     [main-pb (if is-row? (horizontal-pb item-bm) (vertical-pb item-bm))]
                     [cross-border (+ stretched-cross cross-pb)]
                     [derived-main-border (if is-row?
                                              (* cross-border aspect-ratio)
                                              (/ cross-border aspect-ratio))]
                     [derived-main (max 0 (- derived-main-border main-pb))]
                     ;; clamp by min/max on main axis
                     ;; when the item has an explicit min-main > 0, also clamp by container's
                     ;; main-avail (the flex algorithm would have resolved this constraint)
                     [main-cap (if (and (> (flex-item-min-main item) 0)
                                        (number? main-avail) (not (infinite? main-avail)))
                                   main-avail +inf.0)]
                     ;; per CSS Flexbox §4.5: for items with aspect-ratio, the auto minimum
                     ;; in the ratio-dependent axis is min(content-size, transferred-size).
                     ;; the transferred size is derived from the now-resolved cross size.
                     [effective-min (min (flex-item-min-main item) derived-main)]
                     [clamped (max effective-min
                                   (min (flex-item-max-main item) (min main-cap derived-main)))])
                (set-flex-item-main-size! item clamped)
                clamped)
              main-size0))

        ;; re-layout the item with the stretched definite cross size
        (define main-pb (if is-row? (horizontal-pb item-bm) (vertical-pb item-bm)))
        (define child-main (+ main-size main-pb))
        (define child-avail
          (if is-row?
              `(avail (definite ,child-main) (definite ,stretched-cross))
              `(avail (definite ,stretched-cross) (definite ,child-main))))
        (define stretch-dispatch-box0 (override-pct-main-size item child-main is-row?))
        ;; inject stretched cross-size as definite for nested flex containers
        (define stretch-dispatch-box1 (override-cross-size-in-box stretch-dispatch-box0 stretched-cross is-row?))
        ;; always inject flex-resolved main-size so nested containers distribute correctly
        (define main-prop (if is-row? 'width 'height))
        (define stretch-dispatch-box
          (let* ([stb-styles (get-box-styles stretch-dispatch-box1)]
                 [new-styles
                  (match stb-styles
                    [`(style . ,props)
                     `(style (,main-prop (px ,child-main))
                             ,@(filter (lambda (p) (not (and (pair? p) (eq? (car p) main-prop)))) props))])]
                 [new-box
                  (match stretch-dispatch-box1
                    [`(,type ,id ,_ . ,rest) `(,type ,id ,new-styles ,@rest)]
                    [_ stretch-dispatch-box1])])
            new-box))
        (define child-view (dispatch-fn stretch-dispatch-box child-avail))
        (set-flex-item-view! item child-view)))

    (set-flex-line-cross-size! line line-cross)
    line))

;; ============================================================
;; Phase 7 & 8: Positioning
;; ============================================================

(define (position-flex-items lines main-avail cross-avail
                            justify align-items align-content
                            main-gap cross-gap
                            is-row? is-reversed? wrap-mode container-bm dispatch-fn
                            cross-definite?)
  (define offset-x (+ (box-model-padding-left container-bm)
                      (box-model-border-left container-bm)))
  (define offset-y (+ (box-model-padding-top container-bm)
                      (box-model-border-top container-bm)))

  (define all-views '())
  (define cross-pos 0)
  (define total-cross 0)
  (define max-main-used 0)

  ;; wrap-reverse: CSS Flexbox §9.4 — swaps cross-start and cross-end.
  ;; Strategy: lay out lines in DOM order, then mirror all cross-axis
  ;; positions. align-content distribution also needs its start/end swapped
  ;; so that flex-start → flex-end before mirroring (same net effect).
  (define is-wrap-reverse? (eq? wrap-mode 'wrap-reverse))
  (define ordered-lines lines)

  ;; align-content distribution across lines
  (define num-lines (length lines))
  (define total-lines-cross
    (+ (for/sum ([line (in-list lines)]) (flex-line-cross-size line))
       (* cross-gap (max 0 (sub1 num-lines)))))
  (define cross-free
    (if (and (number? cross-avail) (not (infinite? cross-avail)))
        (- cross-avail total-lines-cross)
        0))

  ;; align-content: stretch — distribute free cross-space among lines
  ;; must be done BEFORE computing ac-start-offset/ac-line-spacing
  (when (and (eq? align-content 'content-stretch)
             (> num-lines 0)
             (number? cross-avail)
             (not (infinite? cross-avail))
             (> cross-free 0))
    (define extra-per-line (/ cross-free num-lines))
    (for ([line (in-list lines)])
      (set-flex-line-cross-size! line
        (+ (flex-line-cross-size line) extra-per-line)))
    ;; re-stretch items whose align-self is stretch and have no explicit cross size
    (for ([line (in-list lines)])
      (define line-cross (flex-line-cross-size line))
      (for ([item (in-list (flex-line-items line))])
        (define item-styles (flex-item-styles item))
        (define self-align (get-style-prop item-styles 'align-self 'self-auto))
        (define effective-align
          (if (eq? self-align 'self-auto) align-items
              (case self-align
                [(self-stretch) 'align-stretch]
                [(self-start) 'align-start]
                [(self-end) 'align-end]
                [(self-center) 'align-center]
                [(self-baseline) 'align-baseline]
                [else align-items])))
        (define cross-size-prop (if is-row? 'height 'width))
        (define has-explicit-cross?
          (not (eq? (get-style-prop item-styles cross-size-prop 'auto) 'auto)))
        (when (and (eq? effective-align 'align-stretch)
                   (not has-explicit-cross?))
          (define item-bm (flex-item-bm item))
          (define cross-margin (if is-row? (vertical-margin item-bm) (horizontal-margin item-bm)))
          (define raw-stretched (max 0 (- line-cross cross-margin)))
          ;; clamp by min/max on cross axis
          (define min-cross-prop (if is-row? 'min-height 'min-width))
          (define max-cross-prop (if is-row? 'max-height 'max-width))
          (define min-cross-val (resolve-size-value (get-style-prop item-styles min-cross-prop 'auto) cross-avail))
          (define max-cross-val (resolve-size-value (get-style-prop item-styles max-cross-prop 'none) cross-avail))
          (define min-cross (or min-cross-val 0))
          (define max-cross (or max-cross-val +inf.0))
          (define stretched-cross (max min-cross (min max-cross raw-stretched)))
          (set-flex-item-cross-size! item stretched-cross)
          ;; re-layout with new stretched cross size
          (define main-size (flex-item-main-size item))
          (define main-pb (if is-row? (horizontal-pb item-bm) (vertical-pb item-bm)))
          (define child-main (+ main-size main-pb))
          (define child-avail
            (if is-row?
                `(avail (definite ,child-main) (definite ,stretched-cross))
                `(avail (definite ,stretched-cross) (definite ,child-main))))
          (define restretch-dispatch-box0 (override-pct-main-size item child-main is-row?))
          ;; inject stretched cross-size as definite for nested flex containers
          (define restretch-dispatch-box1 (override-cross-size-in-box restretch-dispatch-box0 stretched-cross is-row?))
          ;; always inject flex-resolved main-size so nested containers distribute correctly
          (define main-prop (if is-row? 'width 'height))
          (define restretch-dispatch-box
            (let* ([rstb-styles (get-box-styles restretch-dispatch-box1)]
                   [new-styles
                    (match rstb-styles
                      [`(style . ,props)
                       `(style (,main-prop (px ,child-main))
                               ,@(filter (lambda (p) (not (and (pair? p) (eq? (car p) main-prop)))) props))])]
                   [new-box
                    (match restretch-dispatch-box1
                      [`(,type ,id ,_ . ,rest) `(,type ,id ,new-styles ,@rest)]
                      [_ restretch-dispatch-box1])])
              new-box))
          (define child-view (dispatch-fn restretch-dispatch-box child-avail))
          (set-flex-item-view! item child-view)))))

  ;; recompute total-lines-cross after stretch distribution
  (define total-lines-cross-after
    (+ (for/sum ([line (in-list lines)]) (flex-line-cross-size line))
       (* cross-gap (max 0 (sub1 num-lines)))))
  (define cross-free-after
    (if (and (number? cross-avail) (not (infinite? cross-avail)))
        (- cross-avail total-lines-cross-after)
        0))

  ;; is this a multi-line container? (flex-wrap != nowrap, even if only 1 actual line)
  (define is-multi-line? (not (eq? wrap-mode 'nowrap)))

  (define-values (ac-start-offset ac-line-spacing)
    (cond
      ;; single-line nowrap containers: align-content doesn't apply
      [(and (= num-lines 1) (not is-multi-line?))
       (values 0 0)]
      ;; single-line wrapping containers: align-content DOES apply
      ;; (important for space-around with single line, etc.)
      [else
       (case align-content
         [(content-start content-stretch) (values 0 0)]
         [(content-end) (values cross-free-after 0)]
         [(content-center) (values (/ cross-free-after 2) 0)]
         [(content-space-between)
          (if (> num-lines 1)
              (values 0 (/ (max 0 cross-free-after) (sub1 num-lines)))
              (values 0 0))]
         [(content-space-around)
          (if (> num-lines 0)
              (let ([s (/ (max 0 cross-free-after) num-lines)])
                (values (/ s 2) s))
              (values 0 0))]
         [(content-space-evenly)
          (if (> num-lines 0)
              (let ([s (/ (max 0 cross-free-after) (add1 num-lines))])
                (values s s))
              (values 0 0))]
         [else (values 0 0)])]))

  ;; for single line, expand to cross-avail when:
  ;; - nowrap: always (align-content doesn't apply)
  ;; - wrapping with align-content: stretch: also expand (already handled above for multi-line)
  ;; When cross is definite, unconditionally set to cross-avail (CSS spec: single line = container cross)
  ;; When cross is NOT definite, only expand (never shrink) to avoid breaking measurement contexts
  (when (and (= num-lines 1)
             (number? cross-avail)
             (not (infinite? cross-avail))
             (or (not is-multi-line?)
                 (eq? align-content 'content-stretch)))
    (define line (car lines))
    (if cross-definite?
        (set-flex-line-cross-size! line cross-avail)
        (when (< (flex-line-cross-size line) cross-avail)
          (set-flex-line-cross-size! line cross-avail))))

  (set! cross-pos ac-start-offset)

  ;; compute the first flex line's baseline for the container's exported baseline
  ;; CSS Flexbox §9.4: container baseline comes from first line's baseline-aligned items
  (define first-line-baseline
    (if (and is-row? (not (null? lines)))
        (let* ([first-line (car lines)]
               [first-items (flex-line-items first-line)]
               [bl-items
                (filter (lambda (item)
                          (define s (get-style-prop (flex-item-styles item) 'align-self 'self-auto))
                          (define eff (if (eq? s 'self-auto) align-items
                                         (case s
                                           [(self-baseline) 'align-baseline]
                                           [else 'other])))
                          (eq? eff 'align-baseline))
                        first-items)])
          (if (null? bl-items)
              #f  ;; no baseline items → use default first-child baseline
              ;; the shared baseline = max over (margin-top + item-baseline)
              ;; this is measured from the top of the line's content edge
              (let ([max-bl (apply max (map (lambda (item)
                                              (define item-bm (flex-item-bm item))
                                              (define mt (box-model-margin-top item-bm))
                                              (+ mt (flex-item-baseline item)))
                                            bl-items))])
                ;; add the line's cross offset (ac-start-offset) plus container padding/border
                (+ offset-y ac-start-offset max-bl))))
        #f))

  (for ([line (in-list ordered-lines)]
        [line-idx (in-naturals)])
    (when (> line-idx 0)
      (set! cross-pos (+ cross-pos cross-gap ac-line-spacing)))

    (define items (flex-line-items line))
    ;; CSS Flexbox §9.4: for reversed flex-direction (row-reverse / column-reverse),
    ;; items within each line are placed from main-end to main-start.
    ;; Reverse the per-line item order so the iteration lays them out visually reversed.
    (define positioned-items (if is-reversed? (reverse items) items))
    (define n (length items))
    (define free-space (flex-line-free-space line))
    (define line-cross (flex-line-cross-size line))

    ;; compute max baseline for baseline-aligned items in this line (row only)
    (define line-max-baseline
      (if is-row?
          (let ([bl-items
                 (filter (lambda (item)
                           (define s (get-style-prop (flex-item-styles item) 'align-self 'self-auto))
                           (define eff (if (eq? s 'self-auto) align-items
                                          (case s
                                            [(self-baseline) 'align-baseline]
                                            [else 'other])))
                           (eq? eff 'align-baseline))
                         items)])
            (if (null? bl-items) 0
                (apply max (map (lambda (item)
                                  (define item-bm (flex-item-bm item))
                                  (define mt (box-model-margin-top item-bm))
                                  (+ mt (flex-item-baseline item)))
                                bl-items))))
          0))

    ;; main axis positioning (justify-content)
    ;; for reversed directions, swap flex-start/flex-end semantics
    ;; but NOT start/end (which are writing-mode relative, not flex-relative)
    (define effective-justify
      (if is-reversed?
          (case justify
            [(flex-start) 'flex-end]
            [(flex-end) 'flex-start]
            [else justify])
          justify))

    (define-values (start-offset item-spacing)
      (case effective-justify
        [(flex-start start) (values 0 0)]
        ;; flex-end/end: allow negative offset when items overflow
        [(flex-end end) (values free-space 0)]
        ;; center: allow negative offset when items overflow
        [(center) (values (/ free-space 2) 0)]
        [(space-between)
         (if (> n 1)
             (values 0 (/ (max 0 free-space) (sub1 n)))
             (values 0 0))]
        [(space-around)
         (if (> n 0)
             (let ([s (/ (max 0 free-space) n)])
               (values (/ s 2) s))
             (values 0 0))]
        [(space-evenly)
         (if (> n 0)
             (let ([s (/ (max 0 free-space) (add1 n))])
               (values s s))
             (values 0 0))]
        [else (values 0 0)]))

    (define main-pos start-offset)

    (for ([item (in-list positioned-items)]
          [item-idx (in-naturals)])
      (when (> item-idx 0)
        (set! main-pos (+ main-pos main-gap item-spacing)))

      (define item-bm (flex-item-bm item))
      (define item-styles (flex-item-styles item))
      (define main-size (flex-item-main-size item))
      (define cross-size (flex-item-cross-size item))

      ;; axis-aware margin/pb
      (define main-margin-before (if is-row? (box-model-margin-left item-bm) (box-model-margin-top item-bm)))
      (define main-margin-after (if is-row? (box-model-margin-right item-bm) (box-model-margin-bottom item-bm)))
      (define main-pb (if is-row? (horizontal-pb item-bm) (vertical-pb item-bm)))
      (define cross-margin-before (if is-row? (box-model-margin-top item-bm) (box-model-margin-left item-bm)))
      (define cross-margin-after (if is-row? (box-model-margin-bottom item-bm) (box-model-margin-right item-bm)))

      ;; check for auto margins (flex auto margins absorb free space)
      (define-values (raw-mt raw-mr raw-mb raw-ml) (get-raw-margins item-styles))
      (define main-before-auto? (if is-row? (eq? raw-ml 'auto) (eq? raw-mt 'auto)))
      (define main-after-auto? (if is-row? (eq? raw-mr 'auto) (eq? raw-mb 'auto)))
      (define cross-before-auto? (if is-row? (eq? raw-mt 'auto) (eq? raw-ml 'auto)))
      (define cross-after-auto? (if is-row? (eq? raw-mb 'auto) (eq? raw-mr 'auto)))

      ;; compute auto margin values on main axis
      ;; auto margins absorb remaining free space per item
      (define item-outer-main (+ main-size main-pb main-margin-before main-margin-after))
      (define item-free (max 0 (- (if (> n 0) (/ (max 0 free-space) 1) 0) 0)))
      (define auto-main-count (+ (if main-before-auto? 1 0) (if main-after-auto? 1 0)))
      (define effective-main-margin-before
        (if (and main-before-auto? (> free-space 0))
            ;; distribute free space to auto margins on main axis
            ;; free-space already accounts for items; each item's auto margins split remaining
            (/ (max 0 free-space) (max 1 (count-auto-main-margins items is-row?)))
            main-margin-before))
      (define effective-main-margin-after
        (if (and main-after-auto? (> free-space 0))
            (/ (max 0 free-space) (max 1 (count-auto-main-margins items is-row?)))
            main-margin-after))

      ;; cross axis alignment for this item
      (define self-align
        (let ([sa (get-style-prop item-styles 'align-self 'self-auto)])
          (if (eq? sa 'self-auto)
              align-items
              (case sa
                [(self-start) 'align-start]
                [(self-end) 'align-end]
                [(self-center) 'align-center]
                [(self-baseline) 'align-baseline]
                [(self-stretch) 'align-stretch]
                [else align-items]))))

      (define cross-offset
        (cond
          ;; auto cross margins override align-self
          [(or cross-before-auto? cross-after-auto?)
           (define cross-free (max 0 (- line-cross cross-size)))
           (cond
             [(and cross-before-auto? cross-after-auto?)
              ;; both auto → center
              (/ cross-free 2)]
             [cross-before-auto?
              ;; only before auto → push to end
              cross-free]
             [else
              ;; only after auto → stay at start
              0])]
          [else
           ;; For wrap-reverse, within-line alignment is NOT inverted here;
           ;; the post-hoc cross-axis mirror (CSS Flexbox §9.4) handles
           ;; the swap of cross-start/cross-end for ALL positions at once.
           (case self-align
             [(align-start) cross-margin-before]
             [(align-end) (- line-cross cross-size cross-margin-after)]
             [(align-center) (+ cross-margin-before (/ (- line-cross cross-size cross-margin-before cross-margin-after) 2))]
             [(align-stretch) cross-margin-before]
             [(align-baseline)
              ;; baseline alignment: shift so item's baseline aligns with line's max baseline
              ;; cross-offset positions the border box, so add margin-before
              (if is-row?
                  (let* ([item-bm (flex-item-bm item)]
                         [mt (box-model-margin-top item-bm)]
                         [item-outer-baseline (+ mt (flex-item-baseline item))])
                    (+ cross-margin-before (- line-max-baseline item-outer-baseline)))
                  ;; for column direction, baseline falls back to flex-start
                  cross-margin-before)]
             [else cross-margin-before])]))

      ;; compute x,y based on axis direction
      (define-values (x y)
        (if is-row?
            (values (+ offset-x main-pos effective-main-margin-before)
                    (+ offset-y cross-pos cross-offset))
            (values (+ offset-x cross-pos cross-offset)
                    (+ offset-y main-pos effective-main-margin-before))))

      ;; create the child view with proper sizes
      ;; use the stretched cross size for the view dimensions
      (define view-w
        (if is-row?
            (+ main-size main-pb)
            cross-size))
      (define view-h
        (if is-row?
            cross-size
            (+ main-size main-pb)))

      ;; get the child view (already laid out in Phase 6)
      (define child-view (flex-item-view item))
      ;; override the view with correct dimensions if needed.
      ;; text views (anonymous flex items wrapping text content) preserve their
      ;; intrinsic content dimensions — CSS text nodes report their bounding box
      ;; at natural text size, even when the anonymous flex item stretches.
      (define positioned-view
        (if child-view
            (let ([pos-view (set-view-pos child-view x y)])
              (match pos-view
                [`(view-text . ,_) pos-view]
                [_ (set-view-size pos-view view-w view-h)]))
            (make-view (get-box-id (flex-item-box item)) x y view-w view-h '())))

      ;; apply relative positioning offset after flex positioning
      ;; pass containing block dimensions for percentage offsets
      (define rel-cb-w (if is-row? main-avail line-cross))
      (define rel-cb-h (if is-row? line-cross main-avail))
      (define final-view (apply-relative-offset positioned-view item-styles rel-cb-w rel-cb-h))

      (set! all-views (cons (cons (flex-item-child-index item) final-view) all-views))

      ;; advance main position
      (define item-main-outer
        (+ main-size main-pb effective-main-margin-before effective-main-margin-after))
      (set! main-pos (+ main-pos item-main-outer)))

    (set! max-main-used (max max-main-used main-pos))
    (set! cross-pos (+ cross-pos line-cross))
    (set! total-cross cross-pos))

  ;; return indexed pairs (child-index . view) for DOM-order merging with abs children
  ;; the caller will handle final sorting
  ;; also return first-line-baseline for container baseline export
  ;;
  ;; CSS Flexbox §9.4: wrap-reverse mirrors cross-axis positions.
  ;; CSS Flexbox §9.4: wrap-reverse mirrors cross-axis positions.
  ;; After laying out lines as if normal wrap, reflect all item cross positions
  ;; so that the first line in DOM order sits at the cross-end edge.
  ;; Use cross-avail when definite, otherwise use total-cross (container's
  ;; intrinsic cross size = sum of all line cross sizes).
  (define mirror-dim
    (cond
      [(and (number? cross-avail) (not (infinite? cross-avail))) cross-avail]
      [(> total-cross 0) total-cross]
      [else #f]))
  (define final-views
    (if (and is-wrap-reverse? mirror-dim (> mirror-dim 0))
        ;; mirror: new_cross = mirror-dim - (old_cross - offset) - item_dimension
        ;; For row: mirror y. For column: mirror x.
        (for/list ([pair (in-list all-views)])
          (define idx (car pair))
          (define v (cdr pair))
          (define vx (view-x v))
          (define vy (view-y v))
          (define vw (view-width v))
          (define vh (view-height v))
          (if is-row?
              ;; mirror y: new-y = offset-y + (mirror-dim - (vy - offset-y) - vh)
              (let ([new-y (+ offset-y (- mirror-dim (- vy offset-y) vh))])
                (cons idx (set-view-pos v vx new-y)))
              ;; mirror x: new-x = offset-x + (mirror-dim - (vx - offset-x) - vw)
              (let ([new-x (+ offset-x (- mirror-dim (- vx offset-x) vw))])
                (cons idx (set-view-pos v new-x vy)))))
        all-views))

  (values final-views
          max-main-used
          total-cross
          first-line-baseline))

;; offset-view, get-box-styles, set-view-pos, set-view-size,
;; compute-view-baseline, resolve-gap — imported from layout-common.rkt

;; count the number of auto margins on the main axis across all items in a line
(define (count-auto-main-margins items is-row?)
  (for/sum ([item (in-list items)])
    (define-values (mt mr mb ml) (get-raw-margins (flex-item-styles item)))
    (+ (if (eq? (if is-row? ml mt) 'auto) 1 0)
       (if (eq? (if is-row? mr mb) 'auto) 1 0))))

(define (get-box-id box)
  (match box
    [`(block ,id ,_ ,_) id]
    [`(inline ,id ,_ ,_) id]
    [`(inline-block ,id ,_ ,_) id]
    [`(flex ,id ,_ ,_) id]
    [`(grid ,id ,_ ,_ ,_) id]
    [`(table ,id ,_ ,_) id]
    [`(text ,id ,_ ,_ ,_) id]
    [`(replaced ,id ,_ ,_ ,_) id]
    [`(none ,id) id]
    [_ "unknown"]))
