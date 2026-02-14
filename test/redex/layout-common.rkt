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
    [`(em ,n) (* n 16)]  ; simplified: assume 1em = 16px
    ['min-content 'min-content]
    ['max-content 'max-content]
    ['fit-content 'fit-content]
    [_ #f]))

;; resolve an AvailWidth to a number or #f
(define (avail-width->number aw)
  (match aw
    [`(definite ,n) n]
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

;; extract box-model from styles
(define (extract-box-model styles)
  (define-values (mt mr mb ml) (get-edges styles 'margin))
  (define-values (pt pr pb pl) (get-edges styles 'padding))
  (define-values (bt br bb bl) (get-edges styles 'border-width))
  (define bs (get-style-prop styles 'box-sizing 'content-box))
  ;; convert 'auto margins to 0 for general box-model calculations
  (define (safe-margin v) (if (eq? v 'auto) 0 v))
  (box-model (safe-margin mt) (safe-margin mr) (safe-margin mb) (safe-margin ml)
             pt pr pb pl bt br bb bl bs))

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
  (define bm (extract-box-model styles))
  (define css-width-val (get-style-prop styles 'width 'auto))
  (define resolved-w (resolve-size-value css-width-val containing-width))
  (define content-w
    (cond
      ;; explicit width
      [resolved-w (compute-content-width bm resolved-w)]
      ;; auto width: fill available space
      [else
       (max 0 (- containing-width (horizontal-pb bm) (horizontal-margin bm)))]))
  ;; apply min/max constraints
  (define min-w-val (get-style-prop styles 'min-width 'auto))
  (define max-w-val (get-style-prop styles 'max-width 'none))
  (define min-w (or (resolve-size-value min-w-val containing-width) 0))
  (define max-w (or (resolve-size-value max-w-val containing-width) +inf.0))
  (define clamped-w (max min-w (min max-w content-w)))
  clamped-w)

;; ============================================================
;; Height Resolution for Block-Level Boxes
;; ============================================================

;; resolve explicit height, or return #f for auto
(define (resolve-block-height styles containing-height)
  (define css-h-val (get-style-prop styles 'height 'auto))
  (define resolved-h (resolve-size-value css-h-val containing-height))
  (define bm (extract-box-model styles))
  (define content-h
    (cond
      [resolved-h
       (if (eq? (box-model-box-sizing bm) 'border-box)
           (max 0 (- resolved-h (vertical-pb bm)))
           resolved-h)]
      [else #f]))  ; auto → determined by content
  ;; apply min/max when explicit height is set
  (when content-h
    (define min-h-val (get-style-prop styles 'min-height 'auto))
    (define max-h-val (get-style-prop styles 'max-height 'none))
    (define min-h (or (resolve-size-value min-h-val containing-height) 0))
    (define max-h (or (resolve-size-value max-h-val containing-height) +inf.0))
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
(define (make-view id x y width height children)
  `(view ,id ,x ,y ,width ,height ,children))

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
