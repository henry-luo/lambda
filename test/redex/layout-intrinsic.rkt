#lang racket/base

;; layout-intrinsic.rkt — Intrinsic sizing (min-content / max-content)
;;
;; Implements CSS Sizing Level 3 intrinsic size determination.
;; Corresponds to Radiant's intrinsic_sizing.cpp.
;;
;; min-content width: the smallest width that fits content without overflow
;; max-content width: the width the content would take with no wrapping

(require racket/match
         "css-layout-lang.rkt"
         "layout-common.rkt")

(provide min-content-width
         max-content-width)

;; ============================================================
;; Min-Content Width
;; ============================================================

;; compute the min-content width of a box.
;; for block containers: the maximum min-content of children
;; for inline content: width of the widest word (or atomic inline)
;; for flex/grid: depends on items
(define (min-content-width box dispatch-fn)
  (match box
    ;; block container: max of children min-content widths
    [`(block ,id ,styles (,children ...))
     (define bm (extract-box-model styles))
     (define css-w (get-style-prop styles 'width 'auto))
     (cond
       ;; explicit width → that's the min-content
       [(and (not (eq? css-w 'auto)) (resolve-size-value css-w #f))
        => (λ (w) (+ w (horizontal-pb bm)))]
       ;; auto → recurse
       [else
        (define child-min
          (for/fold ([max-w 0])
                    ([child (in-list children)])
            (max max-w (min-content-width child dispatch-fn))))
        (+ child-min (horizontal-pb bm))])]

    ;; inline: sum of min-content of children (wraps at every opportunity)
    [`(inline ,id ,styles (,children ...))
     (define bm (extract-box-model styles))
     (define child-min
       (for/fold ([max-w 0])
                 ([child (in-list children)])
         (max max-w (min-content-width child dispatch-fn))))
     (+ child-min (horizontal-pb bm))]

    ;; text: width of the longest "word" — simplified to full width
    ;; (proper implementation would break at word boundaries)
    [`(text ,id ,styles ,content ,measured-w)
     measured-w]

    ;; replaced element: intrinsic width
    [`(replaced ,id ,styles ,w ,h)
     (define bm (extract-box-model styles))
     (+ w (horizontal-pb bm))]

    ;; flex: sum or max of items depending on direction
    [`(flex ,id ,styles (,children ...))
     (define bm (extract-box-model styles))
     (define direction (get-style-prop styles 'flex-direction 'row))
     (define is-row? (or (eq? direction 'row) (eq? direction 'row-reverse)))
     (define child-sizes
       (map (λ (c) (min-content-width c dispatch-fn)) children))
     (define content-min
       (if is-row?
           ;; row: smallest possible is largest single item (wrapping assumed)
           (apply max 0 child-sizes)
           ;; column: largest item width
           (apply max 0 child-sizes)))
     (+ content-min (horizontal-pb bm))]

    ;; grid: complex — simplified to max of items
    [`(grid ,id ,styles ,_ (,children ...))
     (define bm (extract-box-model styles))
     (define child-min
       (for/fold ([max-w 0])
                 ([child (in-list children)])
         (max max-w (min-content-width child dispatch-fn))))
     (+ child-min (horizontal-pb bm))]

    ;; none
    [`(none ,_) 0]

    ;; inline-block
    [`(inline-block ,id ,styles (,children ...))
     (min-content-width `(block ,id ,styles (,@children)) dispatch-fn)]

    [_ 0]))

;; ============================================================
;; Max-Content Width
;; ============================================================

;; compute the max-content width of a box.
;; for block containers: the maximum max-content of children
;; for inline content: total width of all content without wrapping
(define (max-content-width box dispatch-fn)
  (match box
    ;; block container
    [`(block ,id ,styles (,children ...))
     (define bm (extract-box-model styles))
     (define css-w (get-style-prop styles 'width 'auto))
     (cond
       [(and (not (eq? css-w 'auto)) (resolve-size-value css-w #f))
        => (λ (w) (+ w (horizontal-pb bm)))]
       [else
        (define child-max
          (for/fold ([max-w 0])
                    ([child (in-list children)])
            (max max-w (max-content-width child dispatch-fn))))
        (+ child-max (horizontal-pb bm))])]

    ;; inline: sum of all inline content widths (no wrapping)
    [`(inline ,id ,styles (,children ...))
     (define bm (extract-box-model styles))
     (define child-sum
       (for/sum ([child (in-list children)])
         (max-content-width child dispatch-fn)))
     (+ child-sum (horizontal-pb bm))]

    ;; text: full measured width
    [`(text ,id ,styles ,content ,measured-w)
     measured-w]

    ;; replaced element
    [`(replaced ,id ,styles ,w ,h)
     (define bm (extract-box-model styles))
     (+ w (horizontal-pb bm))]

    ;; flex
    [`(flex ,id ,styles (,children ...))
     (define bm (extract-box-model styles))
     (define direction (get-style-prop styles 'flex-direction 'row))
     (define is-row? (or (eq? direction 'row) (eq? direction 'row-reverse)))
     (define col-gap (get-style-prop styles 'column-gap 0))
     (define child-sizes
       (map (λ (c) (max-content-width c dispatch-fn)) children))
     (define content-max
       (if is-row?
           ;; row: sum of all items + gaps
           (+ (apply + child-sizes)
              (* col-gap (max 0 (sub1 (length child-sizes)))))
           ;; column: widest item
           (apply max 0 child-sizes)))
     (+ content-max (horizontal-pb bm))]

    ;; grid
    [`(grid ,id ,styles ,_ (,children ...))
     (define bm (extract-box-model styles))
     (define child-max
       (for/sum ([child (in-list children)])
         (max-content-width child dispatch-fn)))
     (+ child-max (horizontal-pb bm))]

    ;; none
    [`(none ,_) 0]

    ;; inline-block
    [`(inline-block ,id ,styles (,children ...))
     (max-content-width `(block ,id ,styles (,@children)) dispatch-fn)]

    [_ 0]))

;; ============================================================
;; Helper
;; ============================================================

(define (get-style-prop styles prop-name default)
  (match styles
    [`(style . ,props)
     (let loop ([ps props])
       (cond
         [(null? ps) default]
         [(and (pair? (car ps)) (eq? (caar ps) prop-name))
          (cadar ps)]
         [else (loop (cdr ps))]))]))

(define (extract-box-model styles)
  (define-values (mt mr mb ml) (get-edges styles 'margin))
  (define-values (pt pr pb pl) (get-edges styles 'padding))
  (define-values (bt br bb bl) (get-edges styles 'border-width))
  (define bs (get-style-prop styles 'box-sizing 'content-box))
  (box-model mt mr mb ml pt pr pb pl bt br bb bl bs))

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

(define (horizontal-pb bm)
  (+ (box-model-padding-left bm) (box-model-padding-right bm)
     (box-model-border-left bm) (box-model-border-right bm)))
