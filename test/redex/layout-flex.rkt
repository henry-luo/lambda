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
         "css-layout-lang.rkt"
         "layout-common.rkt")

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
     (define row-gap (get-style-prop styles 'row-gap 0))
     (define col-gap (get-style-prop styles 'column-gap 0))

     ;; determine main axis
     (define is-row? (or (eq? direction 'row) (eq? direction 'row-reverse)))
     (define is-reversed? (or (eq? direction 'row-reverse) (eq? direction 'column-reverse)))

     ;; resolve container's content size
     (define content-w (if avail-w (resolve-block-width styles avail-w) 0))
     (define explicit-h (resolve-block-height styles avail-h))

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
     (define resolved-lines
       (for/list ([line (in-list lines)])
         (resolve-flex-lengths line main-avail main-gap)))

     ;; === Phase 6: Determine cross sizes ===
     (define cross-sized-lines
       (determine-cross-sizes resolved-lines cross-avail align-items
                              is-row? dispatch-fn))

     ;; === Phase 7 & 8: Main and cross axis alignment, compute positions ===
     (define-values (child-views total-main total-cross)
       (position-flex-items cross-sized-lines
                           main-avail cross-avail
                           justify align-items align-content
                           main-gap cross-gap
                           is-row? bm dispatch-fn))

     ;; compute final container size
     (define final-content-w (if is-row? content-w (max content-w total-cross)))
     (define final-content-h
       (or explicit-h
           (if is-row? total-cross total-main)))

     (define border-box-w (compute-border-box-width bm final-content-w))
     (define border-box-h (compute-border-box-height bm final-content-h))

     (make-view id 0 0 border-box-w border-box-h child-views)]

    [_ (error 'layout-flex "expected flex box, got: ~a" box)]))

;; ============================================================
;; Phase 1: Collect Flex Items
;; ============================================================

(define (collect-flex-items children container-styles is-row? main-avail dispatch-fn)
  (for/list ([child (in-list children)])
    (define styles (get-box-styles child))
    (define bm (extract-box-model styles))
    (define order (get-style-prop styles 'order 0))
    (define grow (get-style-prop styles 'flex-grow 0))
    (define shrink (get-style-prop styles 'flex-shrink 1))
    (define basis-val (get-style-prop styles 'flex-basis 'auto))

    ;; resolve flex-basis
    (define basis
      (cond
        [(eq? basis-val 'auto)
         ;; auto basis: use width/height or content size
         (define size-prop (if is-row? 'width 'height))
         (define size-val (get-style-prop styles size-prop 'auto))
         (define resolved (resolve-size-value size-val main-avail))
         (or resolved
             ;; need to measure content
             (measure-flex-item-content child is-row? main-avail dispatch-fn))]
        [else
         (or (resolve-size-value basis-val main-avail) 0)]))

    ;; compute hypothetical main size (basis clamped by min/max)
    (define min-prop (if is-row? 'min-width 'min-height))
    (define max-prop (if is-row? 'max-width 'max-height))
    (define min-main (or (resolve-size-value (get-style-prop styles min-prop 'auto) main-avail) 0))
    (define max-main (or (resolve-size-value (get-style-prop styles max-prop 'none) main-avail) +inf.0))
    (define hyp-main (max min-main (min max-main basis)))

    ;; outer hypothetical = hyp-main + margin + padding + border on main axis
    (define main-pb
      (if is-row? (horizontal-pb bm) (vertical-pb bm)))
    (define main-margin
      (if is-row? (horizontal-margin bm) (vertical-margin bm)))

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

(define (resolve-flex-lengths line main-avail main-gap)
  (define items (flex-line-items line))
  (define n (length items))
  (define total-gaps (* main-gap (max 0 (sub1 n))))
  (define available (- main-avail total-gaps))

  ;; sum of hypothetical main sizes (content only, no outer)
  (define total-basis
    (for/sum ([item (in-list items)])
      (+ (flex-item-flex-basis item)
         (if #t  ; always add padding/border
             (let ([bm (flex-item-bm item)])
               (horizontal-pb bm))
             0))))

  (define free-space (- available total-basis))

  ;; determine if we're growing or shrinking
  (define growing? (> free-space 0))

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
    (set-flex-item-main-size! item clamped))

  ;; recompute line main size and free space
  (define new-total
    (+ total-gaps
       (for/sum ([item (in-list items)])
         (+ (flex-item-main-size item)
            (horizontal-pb (flex-item-bm item))))))

  (set-flex-line-main-size! line new-total)
  (set-flex-line-free-space! line (- main-avail new-total))
  line)

;; ============================================================
;; Phase 6: Determine Cross Sizes
;; ============================================================

(define (determine-cross-sizes lines cross-avail align-items is-row? dispatch-fn)
  (for/list ([line (in-list lines)])
    (define items (flex-line-items line))

    ;; lay out each item to determine its cross size
    (for ([item (in-list items)])
      (define main-size (flex-item-main-size item))
      (define child-avail
        (if is-row?
            `(avail (definite ,main-size) indefinite)
            `(avail indefinite (definite ,main-size))))
      (define child-view (dispatch-fn (flex-item-box item) child-avail))
      (set-flex-item-view! item child-view)
      (define cross (if is-row? (view-height child-view) (view-width child-view)))
      (set-flex-item-cross-size! item cross))

    ;; line cross size = max of item cross sizes
    (define line-cross
      (for/fold ([max-cross 0])
                ([item (in-list items)])
        (max max-cross (flex-item-cross-size item))))

    ;; stretch items if align-items is stretch
    (when (eq? align-items 'align-stretch)
      (for ([item (in-list items)])
        (define self-align (get-style-prop (flex-item-styles item) 'align-self 'self-auto))
        (when (or (eq? self-align 'self-auto) (eq? self-align 'self-stretch))
          (set-flex-item-cross-size! item line-cross))))

    (set-flex-line-cross-size! line line-cross)
    line))

;; ============================================================
;; Phase 7 & 8: Positioning
;; ============================================================

(define (position-flex-items lines main-avail cross-avail
                            justify align-items align-content
                            main-gap cross-gap
                            is-row? container-bm dispatch-fn)
  (define offset-x (+ (box-model-padding-left container-bm)
                      (box-model-border-left container-bm)))
  (define offset-y (+ (box-model-padding-top container-bm)
                      (box-model-border-top container-bm)))

  (define all-views '())
  (define cross-pos 0)
  (define total-cross 0)

  (for ([line (in-list lines)]
        [line-idx (in-naturals)])
    (when (> line-idx 0)
      (set! cross-pos (+ cross-pos cross-gap)))

    (define items (flex-line-items line))
    (define n (length items))
    (define free-space (flex-line-free-space line))
    (define line-cross (flex-line-cross-size line))

    ;; main axis positioning (justify-content)
    (define-values (start-offset item-spacing)
      (case justify
        [(flex-start) (values 0 0)]
        [(flex-end) (values (max 0 free-space) 0)]
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
      (define main-size (flex-item-main-size item))
      (define cross-size (flex-item-cross-size item))

      ;; cross axis alignment for this item
      (define self-align
        (let ([sa (get-style-prop (flex-item-styles item) 'align-self 'self-auto)])
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
        (case self-align
          [(align-start) 0]
          [(align-end) (max 0 (- line-cross cross-size))]
          [(align-center) (max 0 (/ (- line-cross cross-size) 2))]
          [(align-stretch) 0]
          [else 0]))

      ;; compute x,y based on axis direction
      (define-values (x y)
        (if is-row?
            (values (+ offset-x main-pos (box-model-margin-left item-bm))
                    (+ offset-y cross-pos cross-offset (box-model-margin-top item-bm)))
            (values (+ offset-x cross-pos cross-offset (box-model-margin-left item-bm))
                    (+ offset-y main-pos (box-model-margin-top item-bm)))))

      ;; get or create the child view
      (define child-view
        (or (flex-item-view item)
            (let ([child-avail
                   (if is-row?
                       `(avail (definite ,main-size) (definite ,cross-size))
                       `(avail (definite ,cross-size) (definite ,main-size)))])
              (dispatch-fn (flex-item-box item) child-avail))))

      (define positioned (set-view-pos child-view x y))
      (set! all-views (cons positioned all-views))

      ;; advance main position
      (define item-main-outer
        (+ main-size (horizontal-pb item-bm) (horizontal-margin item-bm)))
      (set! main-pos (+ main-pos item-main-outer)))

    (set! cross-pos (+ cross-pos line-cross))
    (set! total-cross cross-pos))

  (values (reverse all-views)
          main-avail
          total-cross))

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
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))
