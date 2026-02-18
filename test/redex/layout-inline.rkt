#lang racket/base

;; layout-inline.rkt — Inline flow layout algorithm
;;
;; Implements CSS 2.2 §9.4.2 inline formatting context.
;; Handles line box creation, text wrapping, and vertical alignment.
;; Corresponds to Radiant's layout_inline.cpp.

(require racket/match
         racket/list
         "css-layout-lang.rkt"
         "layout-common.rkt"
         "font-metrics.rkt")

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

     (define-values (child-views total-height _line-widths)
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
;; avail-h: available height for percentage resolution (or #f for indefinite)
;;
;; returns (values child-views total-height line-widths)
(define (layout-inline-children children max-line-width parent-bm dispatch-fn [avail-h #f])
  (define offset-x (+ (box-model-padding-left parent-bm) (box-model-border-left parent-bm)))
  (define offset-y (+ (box-model-padding-top parent-bm) (box-model-border-top parent-bm)))

  ;; compute text height from styles: use explicit line-height or font metrics
  (define (text-height-from-styles styles)
    (define fs (get-style-prop styles 'font-size 16))
    (define lh (get-style-prop styles 'line-height #f))
    (cond
      [(and lh (number? lh)) lh]
      ;; Ahem font: line-height defaults to font-size
      [(let ([ft (get-style-prop styles 'font-type #f)])
         (or (not ft) (eq? ft 'ahem)))
       fs]
      ;; proportional: use JSON-loaded font metrics ratio
      [else
       (let ([fm (get-style-prop styles 'font-metrics 'times)])
         (* (font-line-height-ratio fm) fs))]))

  ;; track cursor-based width of each completed line (mutable accumulator)
  (define completed-line-widths '())

  (let loop ([remaining children]
             [current-x 0]
             [current-y 0]
             [line-height 0]
             [views '()])
    (cond
      [(null? remaining)
       ;; current-x is the cursor-based width of the last (current) line
       (values (reverse views) (+ current-y line-height)
               (reverse (cons current-x completed-line-widths)))]
      [else
       (define child (car remaining))

       ;; CSS 2.2 §9.3.2: check for forced line break (<br> element)
       ;; <br> elements carry __forced-break in their styles
       (define is-forced-break?
         (match child
           [`(inline ,_ ,styles ,_)
            (get-style-prop styles '__forced-break #f)]
           [_ #f]))

       (cond
         [is-forced-break?
          ;; CSS 2.2 §9.3.2: <br> forced line break
          ;; Position the <br> at the current cursor on the current line,
          ;; give it one line-height, then advance to the next line.
          (define br-id
            (match child [`(inline ,id ,_ ,_) id] [_ 'br]))
          ;; <br> has height equal to one line of text in the current context
          (define br-height
            (if (> line-height 0)
                line-height
                ;; default proportional line height: ~17.71 (16px × 1.107)
                17.71))
          (define br-view
            `(view ,br-id ,(+ offset-x current-x) ,(+ offset-y current-y) 0 ,br-height ()))
          (define new-line-height (max line-height br-height))
          (set! completed-line-widths (cons current-x completed-line-widths))
          (loop (cdr remaining)
                0
                (+ current-y new-line-height)
                0
                (cons br-view views))]

         [else
       (define child-view
         (match child
           ;; text node: dispatch through layout system for proper metrics
           [`(text ,id ,styles ,content ,measured-w)
            ;; lay out text via dispatch to get correct font-measured dimensions
            (define text-avail `(avail (definite ,max-line-width) indefinite))
            (define laid-out (dispatch-fn child text-avail))
            (define cw (view-width laid-out))
            (define ch (view-height laid-out))
            ;; CSS 2.2 §10.6.1: text view height = font-size (em-box), but the
            ;; line box contribution includes half-leading above and below.
            ;; view-y stores half-leading; line contribution = ch + 2*half-leading
            (define text-half-leading (view-y laid-out))
            (define line-contribution (+ ch (* 2 text-half-leading)))
            ;; check if text fits on current line
            (cond
              [(and (> current-x 0) (> (+ current-x cw) max-line-width))
               ;; wrap to next line
               (set! completed-line-widths (cons current-x completed-line-widths))
               (set! current-x 0)
               (set! current-y (+ current-y line-height))
               (set! line-height 0)]
              [else (void)])
            (define positioned
              (set-view-position laid-out
                                (+ offset-x current-x)
                                (+ offset-y current-y)))
            (set! current-x (+ current-x cw))
            (set! line-height (max line-height line-contribution))
            positioned]

           ;; inline-block: lay out as block, then place inline
           ;; CSS 2.2 §10.3.9: inline-block boxes have margins that affect inline placement
           [`(inline-block ,id ,styles ,children-inner)
            (define child-avail
              `(avail (definite ,max-line-width)
                      ,(if avail-h `(definite ,avail-h) 'indefinite)))
            (define laid-out (dispatch-fn child child-avail))
            (define cw (view-width laid-out))
            (define ch (view-height laid-out))
            ;; extract margins for inline-block positioning
            (define ib-bm (extract-box-model styles max-line-width))
            (define ml (box-model-margin-left ib-bm))
            (define mr (box-model-margin-right ib-bm))
            (define mt (box-model-margin-top ib-bm))
            (define mb (box-model-margin-bottom ib-bm))
            ;; margin-box width for line wrapping check
            (define margin-box-w (+ ml cw mr))
            ;; wrap if needed
            (when (and (> current-x 0) (> (+ current-x margin-box-w) max-line-width))
              (set! completed-line-widths (cons current-x completed-line-widths))
              (set! current-x 0)
              (set! current-y (+ current-y line-height))
              (set! line-height 0))
            (define positioned
              (set-view-position laid-out
                                (+ offset-x current-x ml)
                                (+ offset-y current-y mt)))
            (set! current-x (+ current-x margin-box-w))
            (set! line-height (max line-height (+ mt ch mb)))
            positioned]

           ;; nested inline
           [`(inline ,id ,styles ,inline-children)
            (define child-avail
              `(avail (definite ,(- max-line-width current-x)) indefinite))
            (define laid-out (dispatch-fn child child-avail))
            (define cw (view-width laid-out))
            (define ch (view-height laid-out))
            (when (and (> current-x 0) (> (+ current-x cw) max-line-width))
              (set! completed-line-widths (cons current-x completed-line-widths))
              (set! current-x 0)
              (set! current-y (+ current-y line-height))
              (set! line-height 0))
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
                (set! completed-line-widths (cons current-x completed-line-widths))
                (set! current-x 0)
                (set! current-y (+ current-y line-height))
                (set! line-height 0))
              (define positioned
                (set-view-position laid-out
                                  (+ offset-x current-x)
                                  (+ offset-y current-y)))
              (set! current-x (+ current-x cw))
              (set! line-height (max line-height ch))
              positioned]))

       (loop (cdr remaining)
             current-x current-y line-height
             (cons child-view views))])])))  ;; close else, cond, loop-case
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
