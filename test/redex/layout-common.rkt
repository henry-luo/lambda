#lang racket/base

;; layout-common.rkt — Shared metafunctions for CSS layout
;;
;; Box model computation, length resolution, margin collapsing,
;; and utility functions used across all layout modes.

(require redex/reduction-semantics
         racket/match
         racket/list
         racket/math
         "css-layout-lang.rkt")

(provide (all-defined-out))

;; ============================================================
;; Named Constants
;; ============================================================

;; CSS 2.2 §15.7: initial value of 'font-size' is medium, mapped to 16px
(define CSS-DEFAULT-FONT-SIZE 16)

;; CSS 2.2 §8.3: user-agent default margin for <body>
(define UA-BODY-MARGIN 8)

;; ============================================================
;; Style Accessors — extract properties from a Styles term
;; ============================================================

;; look up a style property by its tag; return the value or a default
(define-metafunction CSS-Layout
  style-lookup : Styles StyleProp -> StyleProp or #f
  ;; base case: not found
  [(style-lookup (style) _) #f]
  ;; matching property found — we match on the tag name
  [(style-lookup (style StyleProp_match StyleProp_rest ...)
                 StyleProp_match)
   StyleProp_match]
  ;; skip non-matching
  [(style-lookup (style StyleProp_first StyleProp_rest ...)
                 StyleProp_query)
   (style-lookup (style StyleProp_rest ...) StyleProp_query)])

;; ============================================================
;; Racket-level style helpers (used in metafunctions)
;; ============================================================

;; extract a named property value from a Styles term
;; returns the default if not found
(define (get-style-prop styles prop-name default)
  (match styles
    [`(style . ,props)
     (let loop ([ps props])
       (cond
         [(null? ps) default]
         [(and (pair? (car ps)) (eq? (caar ps) prop-name))
          (cadar ps)]
         [else (loop (cdr ps))]))]))

;; extract edges (top right bottom left) for a property
(define (get-edges styles prop-name)
  (match styles
    [`(style . ,props)
     (let loop ([ps props])
       (cond
         [(null? ps) (values 0 0 0 0)]
         [(and (pair? (car ps)) (eq? (caar ps) prop-name))
          (match (cadar ps)
            [`(edges ,t ,r ,b ,l) (values t r b l)]
            [_ (values 0 0 0 0)])]
         [else (loop (cdr ps))]))]))

;; check if a margin edge is auto (returns raw margin values including 'auto)
(define (get-raw-margins styles)
  (match styles
    [`(style . ,props)
     (let loop ([ps props])
       (cond
         [(null? ps) (values 0 0 0 0)]
         [(and (pair? (car ps)) (eq? (caar ps) 'margin))
          (match (cadar ps)
            [`(edges ,t ,r ,b ,l) (values t r b l)]
            [_ (values 0 0 0 0)])]
         [else (loop (cdr ps))]))]))

;; get all style properties as an alist
(define (styles->alist styles)
  (match styles
    [`(style . ,props)
     (for/list ([p (in-list props)]
                #:when (pair? p))
       (cons (car p) (cdr p)))]))

;; ============================================================
;; Length Resolution
;; ============================================================

;; resolve a SizeValue to a pixel number given a containing block size
;; containing-size: the relevant dimension of the containing block (or #f if indefinite)
(define (resolve-size-value sv containing-size)
  (match sv
    ['auto #f]           ; auto → not resolved (caller decides)
    ['none #f]           ; none → no constraint
    [`(px ,n) n]
    [`(% ,pct)
     (if (and containing-size
              (not (infinite? containing-size)))
         (* (/ pct 100) containing-size)
         #f)]            ; percentage against indefinite/infinite → not resolved
    [`(em ,n) (* n CSS-DEFAULT-FONT-SIZE)]  ; simplified: assume 1em = 16px
    ['min-content 'min-content]
    ['max-content 'max-content]
    ['fit-content 'fit-content]
    [_ #f]))

;; resolve an AvailWidth to a number or #f
(define (avail-width->number aw)
  (match aw
    [`(definite ,n) n]
    [`(content-sized ,n) n]
    ['indefinite #f]
    ['av-min-content #f]
    ['av-max-content #f]))

;; resolve an AvailHeight to a number or #f
(define (avail-height->number ah)
  (match ah
    [`(definite ,n) n]
    ['indefinite #f]
    ['av-min-content #f]
    ['av-max-content #f]))

;; ============================================================
;; Box Model Computation
;; ============================================================

;; compute the content-box width from the border-box width,
;; subtracting padding and border
(define (border-box->content-width border-box-w padding-l padding-r border-l border-r)
  (max 0 (- border-box-w padding-l padding-r border-l border-r)))

;; compute the border-box width from the content-box width
(define (content->border-box-width content-w padding-l padding-r border-l border-r)
  (+ content-w padding-l padding-r border-l border-r))

;; full box model struct extracted from styles
(struct box-model
  (margin-top margin-right margin-bottom margin-left
   padding-top padding-right padding-bottom padding-left
   border-top border-right border-bottom border-left
   box-sizing)
  #:transparent)

;; resolve an edge value: handles plain numbers, (% N) percentages, and 'auto
;; per CSS spec, percentage margins AND paddings all resolve against the
;; containing block's inline-size (width in horizontal writing mode)
(define (resolve-edge-value v containing-width)
  (match v
    [`(% ,pct)
     (if (and containing-width (number? containing-width)
              (not (infinite? containing-width)))
         (* (/ pct 100) containing-width)
         0)]   ; percentage against indefinite → 0
    [(? number?) v]
    ['auto v]   ; keep 'auto for margin calculations
    [_ 0]))

;; extract box-model from styles, resolving percentage margins/paddings
;; against the containing block's inline-size (width).
;; containing-width: the containing block's width for resolving percentages,
;; or #f if indefinite.
(define (extract-box-model styles [containing-width #f])
  (define-values (mt mr mb ml) (get-edges styles 'margin))
  (define-values (pt pr pb pl) (get-edges styles 'padding))
  (define-values (bt br bb bl) (get-edges styles 'border-width))
  (define bs (get-style-prop styles 'box-sizing 'content-box))
  ;; resolve percentage margins and paddings against containing block width
  ;; per CSS spec: both horizontal AND vertical percentages resolve against width
  (define (resolve-margin v)
    (define resolved (resolve-edge-value v containing-width))
    (if (eq? resolved 'auto) 0 resolved))
  (define (resolve-padding v)
    (define resolved (resolve-edge-value v containing-width))
    (if (eq? resolved 'auto) 0 resolved))
  (box-model (resolve-margin mt) (resolve-margin mr)
             (resolve-margin mb) (resolve-margin ml)
             (resolve-padding pt) (resolve-padding pr)
             (resolve-padding pb) (resolve-padding pl)
             bt br bb bl bs))

;; compute content-area width given the resolved CSS width and box model
(define (compute-content-width bm css-width)
  (if (eq? (box-model-box-sizing bm) 'border-box)
      (border-box->content-width
       css-width
       (box-model-padding-left bm) (box-model-padding-right bm)
       (box-model-border-left bm) (box-model-border-right bm))
      css-width))

;; compute border-box width from content width and box model
(define (compute-border-box-width bm content-width)
  (content->border-box-width
   content-width
   (box-model-padding-left bm) (box-model-padding-right bm)
   (box-model-border-left bm) (box-model-border-right bm)))

;; compute border-box height from content height and box model
(define (compute-border-box-height bm content-height)
  (+ content-height
     (box-model-padding-top bm) (box-model-padding-bottom bm)
     (box-model-border-top bm) (box-model-border-bottom bm)))

;; total horizontal margin
(define (horizontal-margin bm)
  (+ (box-model-margin-left bm) (box-model-margin-right bm)))

;; total vertical margin
(define (vertical-margin bm)
  (+ (box-model-margin-top bm) (box-model-margin-bottom bm)))

;; total horizontal padding + border
(define (horizontal-pb bm)
  (+ (box-model-padding-left bm) (box-model-padding-right bm)
     (box-model-border-left bm) (box-model-border-right bm)))

;; total vertical padding + border
(define (vertical-pb bm)
  (+ (box-model-padding-top bm) (box-model-padding-bottom bm)
     (box-model-border-top bm) (box-model-border-bottom bm)))

;; ============================================================
;; Margin Collapsing (CSS 2.2 §8.3.1)
;; ============================================================

;; collapse two adjacent vertical margins
;; both positive: max
;; both negative: min (most negative)
;; one positive, one negative: sum
(define (collapse-margins m1 m2)
  (cond
    [(and (>= m1 0) (>= m2 0)) (max m1 m2)]
    [(and (< m1 0) (< m2 0)) (min m1 m2)]
    [else (+ m1 m2)]))

;; ============================================================
;; Width Resolution for Block-Level Boxes (CSS 2.2 §10.3.3)
;; ============================================================

;; resolve the width of a block-level box in normal flow.
;; the constraint: margin-left + border-left + padding-left + width +
;;                 padding-right + border-right + margin-right = containing-width
(define (resolve-block-width styles containing-width)
  (define bm (extract-box-model styles containing-width))
  (define css-width-val (get-style-prop styles 'width 'auto))
  (define resolved-w (resolve-size-value css-width-val containing-width))
  (define content-w
    (cond
      ;; explicit width
      [resolved-w (compute-content-width bm resolved-w)]
      ;; auto width: fill available space
      [else
       (max 0 (- containing-width (horizontal-pb bm) (horizontal-margin bm)))]))
  ;; apply min/max constraints (converted to content-box when border-box)
  (define min-w-val (get-style-prop styles 'min-width 'auto))
  (define max-w-val (get-style-prop styles 'max-width 'none))
  (define min-w-raw (or (resolve-size-value min-w-val containing-width) 0))
  (define max-w-raw (or (resolve-size-value max-w-val containing-width) +inf.0))
  (define min-w
    (if (and (> min-w-raw 0) (eq? (box-model-box-sizing bm) 'border-box))
        (max 0 (- min-w-raw (horizontal-pb bm)))
        min-w-raw))
  (define max-w
    (if (and (not (infinite? max-w-raw)) (eq? (box-model-box-sizing bm) 'border-box))
        (max 0 (- max-w-raw (horizontal-pb bm)))
        max-w-raw))
  (define clamped-w (max min-w (min max-w content-w)))
  clamped-w)

;; ============================================================
;; Height Resolution for Block-Level Boxes
;; ============================================================

;; resolve explicit height, or return #f for auto
;; containing-width: needed to correctly resolve percentage paddings (which resolve
;; against inline-size) when converting border-box height to content-box height.
(define (resolve-block-height styles containing-height [containing-width #f])
  (define css-h-val (get-style-prop styles 'height 'auto))
  (define resolved-h (resolve-size-value css-h-val containing-height))
  ;; resolve box model with containing-width so percentage paddings are correct
  (define bm (extract-box-model styles containing-width))
  (define content-h
    (cond
      [resolved-h
       (if (eq? (box-model-box-sizing bm) 'border-box)
           (max 0 (- resolved-h (vertical-pb bm)))
           resolved-h)]
      [else #f]))  ; auto → determined by content
  ;; apply min/max when explicit height is set
  ;; min/max must be converted to content-box when box-sizing:border-box
  (when content-h
    (define min-h-val (get-style-prop styles 'min-height 'auto))
    (define max-h-val (get-style-prop styles 'max-height 'none))
    (define min-h-raw (or (resolve-size-value min-h-val containing-height) 0))
    (define max-h-raw (or (resolve-size-value max-h-val containing-height) +inf.0))
    (define min-h
      (if (and (> min-h-raw 0) (eq? (box-model-box-sizing bm) 'border-box))
          (max 0 (- min-h-raw (vertical-pb bm)))
          min-h-raw))
    (define max-h
      (if (and (not (infinite? max-h-raw)) (eq? (box-model-box-sizing bm) 'border-box))
          (max 0 (- max-h-raw (vertical-pb bm)))
          max-h-raw))
    (set! content-h (max min-h (min max-h content-h))))
  content-h)

;; resolve min-height constraint for a box (returns content-box value or 0)
(define (resolve-min-height styles containing-height)
  (define min-h-val (get-style-prop styles 'min-height 'auto))
  (define resolved (resolve-size-value min-h-val containing-height))
  (if resolved
      (let ([bm (extract-box-model styles)])
        (if (eq? (box-model-box-sizing bm) 'border-box)
            (max 0 (- resolved (vertical-pb bm)))
            resolved))
      0))

;; resolve max-height constraint for a box (returns content-box value or +inf.0)
(define (resolve-max-height styles containing-height)
  (define max-h-val (get-style-prop styles 'max-height 'none))
  (define resolved (resolve-size-value max-h-val containing-height))
  (if resolved
      (let ([bm (extract-box-model styles)])
        (if (eq? (box-model-box-sizing bm) 'border-box)
            (max 0 (- resolved (vertical-pb bm)))
            resolved))
      +inf.0))

;; ============================================================
;; View Construction Helpers
;; ============================================================

;; create a view node from layout results
;; optional baseline: if provided, stored as 8th element for flex baseline export
(define (make-view id x y width height children [baseline #f])
  (if baseline
      `(view ,id ,x ,y ,width ,height ,children ,baseline)
      `(view ,id ,x ,y ,width ,height ,children)))

;; create a text view node
(define (make-text-view id x y width height text)
  `(view-text ,id ,x ,y ,width ,height ,text))

;; empty view (display:none)
(define (make-empty-view id)
  `(view ,id 0 0 0 0 ()))

;; extract view dimensions
(define (view-x v) (list-ref v 2))
(define (view-y v) (list-ref v 3))
(define (view-width v) (list-ref v 4))
(define (view-height v) (list-ref v 5))
(define (view-children v) (list-ref v 6))
(define (view-id v) (list-ref v 1))
;; extract stored baseline (returns #f if not stored)
(define (view-baseline v)
  (if (> (length v) 7) (list-ref v 7) #f))

;; ============================================================
;; View Manipulation Helpers
;; ============================================================

;; set the x/y position of a view node, preserving all other fields
(define (set-view-pos view x y)
  (match view
    [`(view ,id ,_ ,_ ,w ,h ,children ,baseline)
     `(view ,id ,x ,y ,w ,h ,children ,baseline)]
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))

;; set the width/height of a view node, preserving all other fields
(define (set-view-size view w h)
  (match view
    [`(view ,id ,x ,y ,_ ,_ ,children ,baseline)
     `(view ,id ,x ,y ,w ,h ,children ,baseline)]
    [`(view ,id ,x ,y ,_ ,_ ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,x ,y ,_ ,_ ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))

;; offset a view's position by (dx, dy)
(define (offset-view view dx dy)
  (match view
    [`(view ,id ,x ,y ,w ,h ,children ,baseline)
     `(view ,id ,(+ x dx) ,(+ y dy) ,w ,h ,children ,baseline)]
    [`(view ,id ,x ,y ,w ,h ,children)
     `(view ,id ,(+ x dx) ,(+ y dy) ,w ,h ,children)]
    [`(view-text ,id ,x ,y ,w ,h ,text)
     `(view-text ,id ,(+ x dx) ,(+ y dy) ,w ,h ,text)]
    [_ view]))

;; ============================================================
;; Box Style Extraction
;; ============================================================

;; extract the styles term from any box type
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

;; ============================================================
;; Baseline Computation
;; ============================================================

;; compute the first baseline of a view tree.
;; if the view has a stored baseline, use that; otherwise, recurse into
;; the first child.
(define (compute-view-baseline view)
  (define stored (view-baseline view))
  (if stored
      stored
      (let ([h (view-height view)]
            [children (view-children view)])
        (if (or (null? children) (not (pair? children)))
            h
            (let ([first-child (car children)])
              (+ (view-y first-child) (compute-view-baseline first-child)))))))

;; ============================================================
;; Gap Resolution
;; ============================================================

;; resolve a gap value (column-gap or row-gap) against a reference size.
;; supports numeric values and percentage values `(% pct)`.
(define (resolve-gap gap-val avail)
  (cond
    [(number? gap-val) gap-val]
    [(and (pair? gap-val) (eq? (car gap-val) '%))
     (if (and avail (number? avail) (not (infinite? avail)))
         (* (/ (cadr gap-val) 100) avail)
         0)]
    [else 0]))
