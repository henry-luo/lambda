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
;; Flex Layout — Main Entry Point
;; ============================================================

(define (layout-flex box avail dispatch-fn)
  (match box
    [`(flex ,id ,styles (,children ...))
     (define avail-w (avail-width->number (cadr avail)))
     (define avail-h (avail-height->number (caddr avail)))
     (define bm (extract-box-model styles))

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
     (define content-w (resolve-block-width styles (or avail-w 0)))
     (define explicit-h (resolve-block-height styles avail-h))

     ;; resolve percentage gaps:
     ;; per CSS spec, percentage row-gap resolves against the container's block size (height),
     ;; and percentage column-gap resolves against the container's inline size (width).
     ;; if the reference size is indefinite, the percentage resolves to 0.
     (define (resolve-gap v ref-size)
       (match v
         [`(% ,pct)
          (if (and ref-size (not (infinite? ref-size)))
              (* (/ pct 100) ref-size)
              0)]
         [_ v]))
     (define row-gap (resolve-gap row-gap-raw explicit-h))
     (define col-gap (resolve-gap col-gap-raw (if avail-w content-w #f)))

     ;; main/cross axis available sizes
     (define main-avail (if is-row? content-w (or explicit-h +inf.0)))
     (define cross-avail (if is-row? (or explicit-h +inf.0) content-w))

     ;; gap in main/cross directions
     (define main-gap (if is-row? col-gap row-gap))
     (define cross-gap (if is-row? row-gap col-gap))

     ;; === Phase 1: Collect flex items ===
     (define items (collect-flex-items children styles is-row? main-avail dispatch-fn))

     ;; === Phase 2: Sort by order ===
     (define sorted-items
       (sort items < #:key flex-item-order))

     ;; reverse if needed
     (define ordered-items
       (if is-reversed? (reverse sorted-items) sorted-items))

     ;; === Phase 3: Determine hypothetical main sizes (already done in collection) ===

     ;; === Phase 4: Create flex lines ===
     (define lines
       (create-flex-lines ordered-items main-avail main-gap wrap-mode))

     ;; === Phase 5: Resolve flexible lengths ===
     ;; main-definite? = true when the container has a definite main-axis size
     ;; for row: explicit width OR definite available width from parent
     ;; for column: explicit height
     (define main-definite?
       (if is-row?
           (or (not (eq? (get-style-prop styles 'width 'auto) 'auto))
               (and avail-w #t))
           (and explicit-h #t)))
     (define resolved-lines
       (for/list ([line (in-list lines)])
         (resolve-flex-lengths line main-avail main-gap is-row? main-definite?)))

     ;; === Phase 6: Determine cross sizes ===
     (define cross-sized-lines
       (determine-cross-sizes resolved-lines cross-avail align-items
                              is-row? wrap-mode align-content dispatch-fn))

     ;; === Phase 7 & 8: Main and cross axis alignment, compute positions ===
     (define-values (child-views total-main total-cross)
       (position-flex-items cross-sized-lines
                           main-avail cross-avail
                           justify align-items align-content
                           main-gap cross-gap
                           is-row? is-reversed? wrap-mode bm dispatch-fn))

     ;; compute final container size
     ;; for auto-width row containers, use total-main to get intrinsic width
     (define has-explicit-width?
       (not (eq? (get-style-prop styles 'width 'auto) 'auto)))
     (define final-content-w
       (cond
         [is-row?
          (if has-explicit-width? content-w (max content-w total-main))]
         [else (max content-w total-cross)]))
     (define final-content-h
       (let ([raw-h (or explicit-h
                        (if is-row? total-cross total-main))])
         ;; apply min/max height even when height is auto
         (define min-h (resolve-min-height styles avail-h))
         (define max-h (resolve-max-height styles avail-h))
         (max min-h (min max-h raw-h))))

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
                  #:when (let ([s (get-box-styles child)])
                           (let ([pos (get-style-prop s 'position 'static)])
                             (or (eq? pos 'absolute) (eq? pos 'fixed)))))
         (define view (layout-positioned child containing-w containing-h dispatch-fn))
         ;; offset the view position from padding-box to border-box coordinates
         (define raw-view (offset-view* view abs-offset-x abs-offset-y))
         ;; check if insets are all auto (static position case)
         (define child-styles (get-box-styles child))
         (define css-top (get-style-prop child-styles 'top 'auto))
         (define css-right (get-style-prop child-styles 'right 'auto))
         (define css-bottom (get-style-prop child-styles 'bottom 'auto))
         (define css-left (get-style-prop child-styles 'left 'auto))
         (define child-w (view-width raw-view))
         (define child-h (view-height raw-view))

         ;; compute static position offsets for auto-inset axes
         ;; static position is in the padding-box coordinate space, then offset to border-box
         ;; content area starts at padding-left, padding-top within the padding box
         (define pad-left (box-model-padding-left bm))
         (define pad-top (box-model-padding-top bm))

         (define static-main-offset
           (cond
             [(and (eq? css-left 'auto) (eq? css-right 'auto) is-row?)
              ;; justify-content determines main-axis static position
              (case justify
                [(center) (+ pad-left (max 0 (/ (- final-content-w child-w) 2)))]
                [(flex-end end) (+ pad-left (max 0 (- final-content-w child-w)))]
                [(flex-start start) pad-left]
                [else pad-left])]
             [(and (eq? css-top 'auto) (eq? css-bottom 'auto) (not is-row?))
              (case justify
                [(center) (+ pad-top (max 0 (/ (- safe-content-h child-h) 2)))]
                [(flex-end end) (+ pad-top (max 0 (- safe-content-h child-h)))]
                [(flex-start start) pad-top]
                [else pad-top])]
             [else #f]))

         (define static-cross-offset
           (cond
             [(and (eq? css-top 'auto) (eq? css-bottom 'auto) is-row?)
              ;; align-items determines cross-axis static position
              (case align-items
                [(align-center) (+ pad-top (max 0 (/ (- safe-content-h child-h) 2)))]
                [(align-end) (+ pad-top (max 0 (- safe-content-h child-h)))]
                [(align-start align-stretch) pad-top]
                [else pad-top])]
             [(and (eq? css-left 'auto) (eq? css-right 'auto) (not is-row?))
              (case align-items
                [(align-center) (+ pad-left (max 0 (/ (- final-content-w child-w) 2)))]
                [(align-end) (+ pad-left (max 0 (- final-content-w child-w)))]
                [(align-start align-stretch) pad-left]
                [else pad-left])]
             [else #f]))

         ;; apply static position offsets
         ;; raw-view has coordinates in padding-box space (from layout-positioned)
         ;; offset by border to get border-box space
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
             raw-view)))

     (make-view id 0 0 border-box-w border-box-h (append child-views abs-children))]

    [_ (error 'layout-flex "expected flex box, got: ~a" box)]))

;; ============================================================
;; Phase 1: Collect Flex Items
;; ============================================================

(define (collect-flex-items children container-styles is-row? main-avail dispatch-fn)
  (for/list ([child (in-list children)]
             #:unless (match child [`(none ,_) #t] [_ #f])
             #:unless (let ([s (get-box-styles child)])
                        (let ([pos (get-style-prop s 'position 'static)])
                          (or (eq? pos 'absolute) (eq? pos 'fixed)))))
    (define styles (get-box-styles child))
    (define bm (extract-box-model styles))
    (define order (get-style-prop styles 'order 0))
    (define grow (get-style-prop styles 'flex-grow 0))
    (define shrink (get-style-prop styles 'flex-shrink 1))
    (define basis-val (get-style-prop styles 'flex-basis 'auto))

    ;; axis-aware padding/border/margin
    (define main-pb (if is-row? (horizontal-pb bm) (vertical-pb bm)))
    (define main-margin (if is-row? (horizontal-margin bm) (vertical-margin bm)))

    ;; resolve flex-basis
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
           [else
            ;; need to measure content
            (measure-flex-item-content child is-row? main-avail dispatch-fn)])]
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
    (define min-main (or min-main-raw 0))
    (define max-main (or max-main-raw +inf.0))
    (define hyp-main (max min-main (min max-main basis)))

    ;; outer hypothetical = hyp-main + padding/border + margin on main axis
    (flex-item child styles bm order grow shrink basis
              (+ hyp-main main-pb main-margin)
              min-main max-main
              hyp-main 0 #f)))

;; measure content size of a flex item when flex-basis is auto and width/height is auto
(define (measure-flex-item-content child is-row? main-avail dispatch-fn)
  (define measure-avail
    `(avail ,(if is-row? 'av-max-content `(definite ,main-avail))
            ,(if is-row? `(definite ,main-avail) 'av-max-content)))
  (define view (dispatch-fn child measure-avail))
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

  ;; when main-avail is indefinite, items stay at their hypothetical sizes
  ;; (flex-grow/shrink don't apply without a definite main size)
  (when (not main-definite?)
    (for ([item (in-list items)])
      (set-flex-item-main-size! item (flex-item-flex-basis item))))

  (when main-definite?
    (define available (- main-avail total-gaps))

    ;; sum of outer hypothetical main sizes (basis + padding/border + margin)
    ;; but for flex-grow/shrink we work with content-level basis
    (define total-outer-pb+margin
      (for/sum ([item (in-list items)])
        (define bm (flex-item-bm item))
        (+ (if is-row? (horizontal-pb bm) (vertical-pb bm))
           (if is-row? (horizontal-margin bm) (vertical-margin bm)))))

    (define total-basis
      (for/sum ([item (in-list items)])
        (flex-item-flex-basis item)))

    (define free-space (- available total-basis total-outer-pb+margin))

    ;; determine if we're growing or shrinking
    (define growing? (>= free-space 0))

    ;; distribute free space
    (define total-flex
      (for/sum ([item (in-list items)])
        (if growing?
            (flex-item-flex-grow item)
            (* (flex-item-flex-shrink item) (flex-item-flex-basis item)))))

    (for ([item (in-list items)])
      (define basis (flex-item-flex-basis item))
      (define flex-factor
        (if growing?
            (flex-item-flex-grow item)
            (* (flex-item-flex-shrink item) basis)))
      (define distributed
        (if (and (> total-flex 0) (not (= free-space 0)))
            (* free-space (/ flex-factor total-flex))
            0))
      (define target (+ basis distributed))
      ;; clamp by min/max
      (define clamped (max (flex-item-min-main item)
                          (min (flex-item-max-main item) target)))
      (set-flex-item-main-size! item clamped)))

  ;; recompute line free space after resolution
  (define new-used
    (+ total-gaps
       (for/sum ([item (in-list items)])
         (define bm (flex-item-bm item))
         (+ (flex-item-main-size item)
            (if is-row? (horizontal-pb bm) (vertical-pb bm))
            (if is-row? (horizontal-margin bm) (vertical-margin bm))))))

  (set-flex-line-main-size! line new-used)
  (set-flex-line-free-space! line (- main-avail new-used))
  line)

;; ============================================================
;; Phase 6: Determine Cross Sizes
;; ============================================================

(define (determine-cross-sizes lines cross-avail align-items is-row? wrap-mode align-content dispatch-fn)
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
      ;; for the cross axis: pass cross-avail as definite if available
      ;; this is needed for column direction where children need a definite width
      (define cross-avail-spec
        (if (and (number? cross-avail) (not (infinite? cross-avail)))
            `(definite ,cross-avail)
            'indefinite))
      (define child-avail
        (if is-row?
            `(avail (definite ,child-main) ,cross-avail-spec)
            `(avail ,cross-avail-spec (definite ,child-main))))
      (define child-view (dispatch-fn (flex-item-box item) child-avail))
      (set-flex-item-view! item child-view)
      (define cross (if is-row? (view-height child-view) (view-width child-view)))
      (set-flex-item-cross-size! item cross))

    ;; line cross size = max of item cross sizes (outer: cross + cross margin)
    (define natural-line-cross
      (for/fold ([max-cross 0])
                ([item (in-list items)])
        (define item-bm (flex-item-bm item))
        (define cross-margin (if is-row? (vertical-margin item-bm) (horizontal-margin item-bm)))
        (max max-cross (+ (flex-item-cross-size item) cross-margin))))

    ;; for single-line flex containers (nowrap or wrap+stretch), expand to cross-avail
    (define line-cross
      (if (and expand-single-line?
               (number? cross-avail)
               (not (infinite? cross-avail))
               (> cross-avail natural-line-cross))
          cross-avail
          natural-line-cross))

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
        (define stretched-cross (max 0 (- line-cross cross-margin)))
        (set-flex-item-cross-size! item stretched-cross)
        ;; re-layout the item with the stretched definite cross size
        (define main-size (flex-item-main-size item))
        (define main-pb (if is-row? (horizontal-pb item-bm) (vertical-pb item-bm)))
        (define child-main (+ main-size main-pb))
        (define child-avail
          (if is-row?
              `(avail (definite ,child-main) (definite ,stretched-cross))
              `(avail (definite ,stretched-cross) (definite ,child-main))))
        (define child-view (dispatch-fn (flex-item-box item) child-avail))
        (set-flex-item-view! item child-view)))

    (set-flex-line-cross-size! line line-cross)
    line))

;; ============================================================
;; Phase 7 & 8: Positioning
;; ============================================================

(define (position-flex-items lines main-avail cross-avail
                            justify align-items align-content
                            main-gap cross-gap
                            is-row? is-reversed? wrap-mode container-bm dispatch-fn)
  (define offset-x (+ (box-model-padding-left container-bm)
                      (box-model-border-left container-bm)))
  (define offset-y (+ (box-model-padding-top container-bm)
                      (box-model-border-top container-bm)))

  (define all-views '())
  (define cross-pos 0)
  (define total-cross 0)
  (define max-main-used 0)

  ;; wrap-reverse: reverse the order of lines for cross-axis positioning
  (define is-wrap-reverse? (eq? wrap-mode 'wrap-reverse))
  (define ordered-lines (if is-wrap-reverse? (reverse lines) lines))

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
          (define child-view (dispatch-fn (flex-item-box item) child-avail))
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
  (when (and (= num-lines 1)
             (number? cross-avail)
             (not (infinite? cross-avail))
             (or (not is-multi-line?)
                 (eq? align-content 'content-stretch)))
    (define line (car lines))
    (when (< (flex-line-cross-size line) cross-avail)
      (set-flex-line-cross-size! line cross-avail)))

  (set! cross-pos ac-start-offset)

  (for ([line (in-list ordered-lines)]
        [line-idx (in-naturals)])
    (when (> line-idx 0)
      (set! cross-pos (+ cross-pos cross-gap ac-line-spacing)))

    (define items (flex-line-items line))
    (define n (length items))
    (define free-space (flex-line-free-space line))
    (define line-cross (flex-line-cross-size line))

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
        [(flex-end end) (values (max 0 free-space) 0)]
        [(center) (values (max 0 (/ free-space 2)) 0)]
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

    (for ([item (in-list items)]
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
           (case self-align
             [(align-start) cross-margin-before]
             [(align-end) (max 0 (- line-cross cross-size cross-margin-after))]
             [(align-center) (max 0 (/ (- line-cross cross-size cross-margin-before cross-margin-after) 2))]
             [(align-stretch) cross-margin-before]
             [(align-baseline) cross-margin-before]
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
      ;; override the view with correct dimensions if needed
      (define positioned-view
        (if child-view
            (set-view-size (set-view-pos child-view x y) view-w view-h)
            (make-view (get-box-id (flex-item-box item)) x y view-w view-h '())))

      ;; apply relative positioning offset after flex positioning
      (define final-view (apply-relative-offset positioned-view item-styles))

      (set! all-views (cons final-view all-views))

      ;; advance main position
      (define item-main-outer
        (+ main-size main-pb effective-main-margin-before effective-main-margin-after))
      (set! main-pos (+ main-pos item-main-outer)))

    (set! max-main-used (max max-main-used main-pos))
    (set! cross-pos (+ cross-pos line-cross))
    (set! total-cross cross-pos))

  (values (reverse all-views)
          max-main-used
          total-cross))

;; ============================================================
;; Helpers
;; ============================================================

;; offset a view's position by (dx, dy)
(define (offset-view* view dx dy)
  (match view
    [`(view ,id ,x ,y ,w ,h ,children)
     `(view ,id ,(+ x dx) ,(+ y dy) ,w ,h ,children)]
    [`(view-text ,id ,x ,y ,w ,h ,text)
     `(view-text ,id ,(+ x dx) ,(+ y dy) ,w ,h ,text)]
    [_ view]))

;; count the number of auto margins on the main axis across all items in a line
(define (count-auto-main-margins items is-row?)
  (for/sum ([item (in-list items)])
    (define-values (mt mr mb ml) (get-raw-margins (flex-item-styles item)))
    (+ (if (eq? (if is-row? ml mt) 'auto) 1 0)
       (if (eq? (if is-row? mr mb) 'auto) 1 0))))

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
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))

(define (set-view-size view w h)
  (match view
    [`(view ,id ,x ,y ,_ ,_ ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,x ,y ,_ ,_ ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))

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
