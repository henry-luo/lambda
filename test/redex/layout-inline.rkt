#lang racket/base

;; layout-inline.rkt — Inline flow layout algorithm
;;
;; Implements CSS 2.2 §9.4.2 inline formatting context.
;; Handles line box creation, text wrapping, and vertical alignment.
;; Corresponds to Radiant's layout_inline.cpp.

(require racket/match
         racket/list
         "css-layout-lang.rkt"
         "layout-common.rkt")

(provide layout-inline
         layout-inline-children)

;; ============================================================
;; Line Box — accumulates inline content into lines
;; ============================================================

(struct line-box
  (x y width height
   max-ascender max-descender
   items)  ; list of positioned views
  #:transparent #:mutable)

(define (make-line-box start-x start-y)
  (line-box start-x start-y 0 0 0 0 '()))

;; ============================================================
;; Inline Layout — Main Entry Point
;; ============================================================

;; lay out an inline container.
;; inline content is flowed left-to-right, wrapping to new lines.
(define (layout-inline box avail dispatch-fn)
  (match box
    [`(inline ,id ,styles (,children ...))
     (define avail-w (or (avail-width->number (cadr avail)) +inf.0))
     (define bm (extract-box-model styles avail-w))
     (define max-content-w (- avail-w (horizontal-pb bm)))

     (define-values (child-views total-height)
       (layout-inline-children children max-content-w bm dispatch-fn))

     ;; inline boxes shrink-wrap to their content (CSS 2.2 §9.2.2)
     ;; actual content width = rightmost extent of children
     (define offset-x (+ (box-model-padding-left bm) (box-model-border-left bm)))
     (define actual-content-w
       (if (null? child-views) 0
           (apply max
             (for/list ([v (in-list child-views)])
               (- (+ (view-x v) (view-width v)) offset-x)))))
     (define border-box-w (compute-border-box-width bm actual-content-w))
     (define border-box-h (compute-border-box-height bm total-height))

     (make-view id 0 0 border-box-w border-box-h child-views)]

    [_ (error 'layout-inline "expected inline box, got: ~a" box)]))

;; ============================================================
;; Inline Children Layout — Line Breaking
;; ============================================================

;; lay out inline children into line boxes.
;; simple greedy line-breaking algorithm.
;;
;; returns (values child-views total-height)
(define (layout-inline-children children max-line-width parent-bm dispatch-fn)
  (define offset-x (+ (box-model-padding-left parent-bm) (box-model-border-left parent-bm)))
  (define offset-y (+ (box-model-padding-top parent-bm) (box-model-border-top parent-bm)))
  (define default-line-height 20)  ; simplified default line height

  (let loop ([remaining children]
             [current-x 0]
             [current-y 0]
             [line-height default-line-height]
             [views '()])
    (cond
      [(null? remaining)
       (values (reverse views) (+ current-y line-height))]
      [else
       (define child (car remaining))
       (define child-view
         (match child
           ;; text node: measure and possibly wrap
           [`(text ,id ,styles ,content ,measured-w)
            (define text-height default-line-height)
            ;; check if text fits on current line
            (cond
              [(and (> current-x 0) (> (+ current-x measured-w) max-line-width))
               ;; wrap to next line
               (set! current-x 0)
               (set! current-y (+ current-y line-height))
               (set! line-height default-line-height)]
              [else (void)])
            (define vw (make-text-view id
                                       (+ offset-x current-x)
                                       (+ offset-y current-y)
                                       measured-w text-height content))
            (set! current-x (+ current-x measured-w))
            (set! line-height (max line-height text-height))
            vw]

           ;; inline-block: lay out as block, then place inline
           [`(inline-block ,id ,styles ,children-inner)
            (define child-avail `(avail (definite ,max-line-width) indefinite))
            (define laid-out (dispatch-fn child child-avail))
            (define cw (view-width laid-out))
            (define ch (view-height laid-out))
            ;; wrap if needed
            (when (and (> current-x 0) (> (+ current-x cw) max-line-width))
              (set! current-x 0)
              (set! current-y (+ current-y line-height))
              (set! line-height default-line-height))
            (define positioned
              (set-view-position laid-out
                                (+ offset-x current-x)
                                (+ offset-y current-y)))
            (set! current-x (+ current-x cw))
            (set! line-height (max line-height ch))
            positioned]

           ;; nested inline
           [`(inline ,id ,styles ,inline-children)
            (define child-avail
              `(avail (definite ,(- max-line-width current-x)) indefinite))
            (define laid-out (dispatch-fn child child-avail))
            (define cw (view-width laid-out))
            (define ch (view-height laid-out))
            (when (and (> current-x 0) (> (+ current-x cw) max-line-width))
              (set! current-x 0)
              (set! current-y (+ current-y line-height))
              (set! line-height default-line-height))
            (define positioned
              (set-view-position laid-out
                                (+ offset-x current-x)
                                (+ offset-y current-y)))
            (set! current-x (+ current-x cw))
            (set! line-height (max line-height ch))
            positioned]

           ;; other box types dispatched normally
           [_ (define child-avail
                `(avail (definite ,max-line-width) indefinite))
              (define laid-out (dispatch-fn child child-avail))
              (define cw (view-width laid-out))
              (define ch (view-height laid-out))
              (when (and (> current-x 0) (> (+ current-x cw) max-line-width))
                (set! current-x 0)
                (set! current-y (+ current-y line-height))
                (set! line-height default-line-height))
              (define positioned
                (set-view-position laid-out
                                  (+ offset-x current-x)
                                  (+ offset-y current-y)))
              (set! current-x (+ current-x cw))
              (set! line-height (max line-height ch))
              positioned]))

       (loop (cdr remaining)
             current-x current-y line-height
             (cons child-view views))])))

;; ============================================================
;; Helper: Set view position
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
