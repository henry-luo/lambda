#lang racket/base

;; layout-block.rkt — Block flow layout algorithm
;;
;; Implements CSS 2.2 §10.3.3 (block width) and §10.6.3 (block height)
;; with vertical stacking of children and margin collapsing.
;; Corresponds to Radiant's layout_block.cpp.

(require racket/match
         racket/list
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

     ;; resolve content width (CSS 2.2 §10.3.3)
     ;; even if avail-w is #f (indefinite), explicit px widths still resolve
     (define content-w (resolve-block-width styles (or avail-w 0)))

     ;; resolve explicit height (or #f for auto)
     (define explicit-h (resolve-block-height styles avail-h avail-w))

     ;; lay out children within the content area
     (define child-avail
       `(avail (definite ,content-w) ,(if explicit-h `(definite ,explicit-h) 'indefinite)))

     (define-values (child-views content-height)
       (layout-block-children children child-avail bm dispatch-fn))

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
(define (layout-block-children children child-avail parent-bm dispatch-fn)
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
       (values (reverse views) current-y)]
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
          (define child-x (+ offset-x (box-model-margin-left child-bm)))
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
