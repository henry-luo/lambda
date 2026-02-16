#lang racket/base

;; layout-block.rkt — Block flow layout algorithm
;;
;; Implements CSS 2.2 §10.3.3 (block width) and §10.6.3 (block height)
;; with vertical stacking of children and margin collapsing.
;; Corresponds to Radiant's layout_block.cpp.

(require racket/match
         racket/list
         racket/math
         "css-layout-lang.rkt"
         "layout-common.rkt"
         "layout-positioned.rkt")

(provide layout-block
         layout-block-children)

;; ============================================================
;; Block Layout — Main Entry Point
;; ============================================================

;; lay out a block-level box given available space.
;; returns a View term: (view id x y width height (children...))
;;
;; block: a (block BoxId Styles Children) term
;; avail: an AvailableSpace term
;; returns: a View term
(define (layout-block box avail dispatch-fn)
  (match box
    [`(block ,id ,styles (,children ...))
     (define avail-w (avail-width->number (cadr avail)))
     (define avail-h (avail-height->number (caddr avail)))
     (define bm (extract-box-model styles avail-w))

     ;; detect intrinsic sizing mode (min-content / max-content)
     (define is-intrinsic?
       (or (eq? (cadr avail) 'av-min-content) (eq? (cadr avail) 'av-max-content)))
     (define is-auto-width?
       (eq? (get-style-prop styles 'width 'auto) 'auto))

     ;; resolve content width (CSS 2.2 §10.3.3)
     ;; even if avail-w is #f (indefinite), explicit px widths still resolve
     (define initial-content-w (resolve-block-width styles (or avail-w 0)))

     ;; resolve explicit height (or #f for auto)
     (define explicit-h (resolve-block-height styles avail-h avail-w))

     ;; for intrinsic sizing with auto width: shrink-wrap to content
     ;; lay out children at intrinsic mode, then use max child width as content-w
     (define content-w
       (if (and is-intrinsic? is-auto-width?)
           ;; first pass: lay out children in intrinsic mode to measure their widths
           (let ()
             (define measure-avail
               `(avail ,(cadr avail) ,(if explicit-h `(definite ,explicit-h) 'indefinite)))
             (define-values (measure-views _h)
               (layout-block-children children measure-avail bm dispatch-fn 'left))
             ;; find max child border-box width, subtract parent's padding+border
             ;; to get content-box width
             (define max-child-w
               (for/fold ([mx 0]) ([v (in-list measure-views)])
                 (define child-w (view-width v))
                 ;; account for child's margin-left offset
                 (define child-x (view-x v))
                 ;; child-x includes offset-x (parent padding+border) and child margin-left
                 ;; total outer width = child-x - offset-x + child-w + margin-right
                 ;; but simpler: the child's position + width determines needed container width
                 ;; subtract parent's padding+border offset
                 (define offset-x (+ (box-model-padding-left bm) (box-model-border-left bm)))
                 (define child-end (+ (- child-x offset-x) child-w))
                 (max mx child-end)))
             ;; apply min-width/max-width
             (define min-w-val (get-style-prop styles 'min-width 'auto))
             (define max-w-val (get-style-prop styles 'max-width 'none))
             (define min-w-raw (or (resolve-size-value min-w-val (or avail-w 0)) 0))
             (define max-w-raw (or (resolve-size-value max-w-val (or avail-w 0)) +inf.0))
             (define min-w
               (if (and (> min-w-raw 0) (eq? (box-model-box-sizing bm) 'border-box))
                   (max 0 (- min-w-raw (horizontal-pb bm)))
                   min-w-raw))
             (define max-w
               (if (and (not (infinite? max-w-raw)) (eq? (box-model-box-sizing bm) 'border-box))
                   (max 0 (- max-w-raw (horizontal-pb bm)))
                   max-w-raw))
             (max min-w (min max-w max-child-w)))
           initial-content-w))

     ;; lay out children within the content area
     (define child-avail
       `(avail (definite ,content-w) ,(if explicit-h `(definite ,explicit-h) 'indefinite)))

     ;; text-align: inherited property for centering text within block
     (define text-align (get-style-prop styles 'text-align 'left))

     (define-values (child-views content-height)
       (layout-block-children children child-avail bm dispatch-fn text-align))

     ;; final content height: explicit or determined by children
     ;; apply min-height / max-height even for auto (content-determined) height
     (define final-content-h
       (let ([raw-h (or explicit-h content-height)])
         (define min-h (resolve-min-height styles avail-h))
         (define max-h (resolve-max-height styles avail-h))
         (max min-h (min max-h raw-h))))

     ;; compute border-box dimensions
     (define border-box-w (compute-border-box-width bm content-w))
     (define border-box-h (compute-border-box-height bm final-content-h))

     ;; position is relative to parent's content area,
     ;; offset by this box's margin
     ;; (actual x,y positioning done by parent; we report 0,0 here;
     ;;  the parent adjusts based on margin + stacking)
     (make-view id 0 0 border-box-w border-box-h child-views)]

    ;; display:none → empty view
    [`(none ,id)
     (make-empty-view id)]

    [_ (error 'layout-block "expected block box, got: ~a" box)]))

;; ============================================================
;; Block Children Layout — Vertical Stacking
;; ============================================================

;; lay out a list of child boxes vertically within a block container.
;; implements margin collapsing between adjacent siblings.
;;
;; returns (values child-views total-content-height)
(define (layout-block-children children child-avail parent-bm dispatch-fn [text-align 'left])
  (define avail-w (avail-width->number (cadr child-avail)))
  (define avail-h (avail-height->number (caddr child-avail)))
  ;; padding offset for child positioning
  (define pad-left (box-model-padding-left parent-bm))
  (define pad-top (box-model-padding-top parent-bm))
  (define border-left (box-model-border-left parent-bm))
  (define border-top (box-model-border-top parent-bm))
  (define offset-x (+ pad-left border-left))
  (define offset-y (+ pad-top border-top))

  (let loop ([remaining children]
             [current-y 0]
             [prev-margin-bottom 0]
             [views '()])
    (cond
      [(null? remaining)
       ;; CSS 2.1 §8.3.1: the last child's bottom margin stays inside the parent
       ;; when the parent has non-zero bottom border-width or bottom padding.
       ;; Otherwise it collapses through to become the parent's bottom margin.
       (define parent-has-bottom-barrier?
         (or (> (box-model-padding-bottom parent-bm) 0)
             (> (box-model-border-bottom parent-bm) 0)))
       (define final-y
         (if parent-has-bottom-barrier?
             (+ current-y prev-margin-bottom)
             current-y))
       (values (reverse views) final-y)]
      [else
       (define child (car remaining))

       ;; skip display:none children entirely
       (cond
         [(match child [`(none ,_) #t] [_ #f])
          (loop (cdr remaining) current-y prev-margin-bottom views)]
         ;; skip absolute/fixed positioned children (they're laid out by containing block)
         [(let ([s (get-box-styles child)])
            (let ([pos (get-style-prop s 'position 'static)])
              (or (eq? pos 'absolute) (eq? pos 'fixed))))
          (loop (cdr remaining) current-y prev-margin-bottom views)]
         [else
          (define child-view (dispatch-fn child child-avail))

          ;; extract child's box model for margin handling
          (define child-styles (get-box-styles child))
          (define child-bm (extract-box-model child-styles avail-w))

          ;; collapse top margin with previous bottom margin
          (define collapsed-margin
            (collapse-margins prev-margin-bottom
                             (box-model-margin-top child-bm)))

          ;; position child
          (define child-x
            (cond
              ;; text-align applies to inline-level content (text views)
              [(and (match child-view [`(view-text . ,_) #t] [_ #f])
                    (eq? text-align 'center)
                    avail-w)
               (+ offset-x (max 0 (/ (- avail-w (view-width child-view)) 2)))]
              [(and (match child-view [`(view-text . ,_) #t] [_ #f])
                    (eq? text-align 'right)
                    avail-w)
               (+ offset-x (max 0 (- avail-w (view-width child-view))))]
              [else
               (+ offset-x (box-model-margin-left child-bm))]))
          (define child-y (+ offset-y current-y collapsed-margin))

          ;; update the view with computed position
          (define positioned-view (set-view-position child-view child-x child-y))

          ;; apply relative positioning offset after block positioning
          ;; pass containing block dimensions for percentage resolution
          (define final-view (apply-relative-offset positioned-view child-styles avail-w avail-h))

          ;; advance y by child's border-box height
          (define child-h (view-height child-view))
          (define new-y (+ current-y collapsed-margin child-h))

          (loop (cdr remaining)
                new-y
                (box-model-margin-bottom child-bm)
                (cons final-view views))])])))

;; ============================================================
;; Helper: Extract styles from any box type
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

;; ============================================================
;; Helper: Set view position (x, y)
;; ============================================================

(define (set-view-position view x y)
  (match view
    [`(view ,id ,_ ,_ ,w ,h ,children ,baseline)
     `(view ,id ,x ,y ,w ,h ,children ,baseline)]
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))
