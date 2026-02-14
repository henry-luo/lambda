#lang racket/base

;; layout-positioned.rkt — Positioned layout (absolute, fixed, relative, sticky)
;;
;; Implements CSS 2.2 §9.3 positioned elements.
;; Corresponds to Radiant's layout_positioned.cpp.

(require racket/match
         racket/function
         "css-layout-lang.rkt"
         "layout-common.rkt")

(provide layout-positioned
         apply-relative-offset)

;; ============================================================
;; Positioned Layout
;; ============================================================

;; resolve an absolutely positioned box relative to its containing block.
;; containing-block is (width . height).
(define (layout-positioned box containing-w containing-h dispatch-fn)
  (match box
    [`(,box-type ,id ,styles . ,rest)
     (define bm (extract-box-model styles))

     ;; resolve inset properties
     (define css-top (get-style-prop styles 'top 'auto))
     (define css-right (get-style-prop styles 'right 'auto))
     (define css-bottom (get-style-prop styles 'bottom 'auto))
     (define css-left (get-style-prop styles 'left 'auto))

     (define top-val (resolve-size-value css-top containing-h))
     (define right-val (resolve-size-value css-right containing-w))
     (define bottom-val (resolve-size-value css-bottom containing-h))
     (define left-val (resolve-size-value css-left containing-w))

     ;; resolve width
     (define css-width-val (get-style-prop styles 'width 'auto))
     (define resolved-w (resolve-size-value css-width-val containing-w))

     (define content-w
       (cond
         ;; explicit width
         [resolved-w (compute-content-width bm resolved-w)]
         ;; both left and right set → compute width
         [(and left-val right-val)
          (max 0 (- containing-w left-val right-val
                    (horizontal-pb bm) (horizontal-margin bm)))]
         ;; shrink-to-fit: use containing block width
         [else
          (max 0 (- containing-w (horizontal-pb bm) (horizontal-margin bm)))]))

     ;; resolve height
     (define css-height-val (get-style-prop styles 'height 'auto))
     (define resolved-h (resolve-size-value css-height-val containing-h))

     (define content-h
       (cond
         [resolved-h
          (if (eq? (box-model-box-sizing bm) 'border-box)
              (max 0 (- resolved-h (vertical-pb bm)))
              resolved-h)]
         [(and top-val bottom-val)
          (max 0 (- containing-h top-val bottom-val
                    (vertical-pb bm) (vertical-margin bm)))]
         [else #f]))  ; auto height → determined by content

     ;; lay out children to determine auto height
     (define avail `(avail (definite ,content-w)
                           ,(if content-h `(definite ,content-h) 'indefinite)))
     (define child-view (dispatch-fn box avail))
     (define actual-h (or content-h (view-height child-view)))

     ;; compute position
     (define x
       (cond
         [left-val (+ left-val (box-model-margin-left bm))]
         [right-val (- containing-w right-val
                      (compute-border-box-width bm content-w)
                      (box-model-margin-right bm))]
         [else (box-model-margin-left bm)]))

     (define y
       (cond
         [top-val (+ top-val (box-model-margin-top bm))]
         [bottom-val (- containing-h bottom-val
                       (compute-border-box-height bm actual-h)
                       (box-model-margin-bottom bm))]
         [else (box-model-margin-top bm)]))

     (set-view-pos child-view x y)]

    [_ (error 'layout-positioned "unexpected box: ~a" box)]))

;; ============================================================
;; Relative Positioning (CSS 2.2 §9.4.3)
;; ============================================================

;; apply relative offset to an already-laid-out view.
;; does not affect layout of siblings.
(define (apply-relative-offset view styles)
  (define pos (get-style-prop styles 'position 'static))
  (cond
    [(eq? pos 'relative)
     (define css-top (get-style-prop styles 'top 'auto))
     (define css-left (get-style-prop styles 'left 'auto))
     (define css-bottom (get-style-prop styles 'bottom 'auto))
     (define css-right (get-style-prop styles 'right 'auto))

     ;; top takes precedence over bottom; left over right
     (define dx
       (cond
         [(resolve-size-value css-left #f) => identity]
         [(resolve-size-value css-right #f) => (λ (v) (- v))]
         [else 0]))
     (define dy
       (cond
         [(resolve-size-value css-top #f) => identity]
         [(resolve-size-value css-bottom #f) => (λ (v) (- v))]
         [else 0]))

     (offset-view view dx dy)]
    [else view]))

;; offset a view's position by (dx, dy)
(define (offset-view view dx dy)
  (match view
    [`(view ,id ,x ,y ,w ,h ,children)
     `(view ,id ,(+ x dx) ,(+ y dy) ,w ,h ,children)]
    [`(view-text ,id ,x ,y ,w ,h ,text)
     `(view-text ,id ,(+ x dx) ,(+ y dy) ,w ,h ,text)]
    [_ view]))

;; ============================================================
;; Helpers
;; ============================================================

(define (set-view-pos view x y)
  (match view
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))
