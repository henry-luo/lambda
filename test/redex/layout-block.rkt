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
             ;; CSS 2.2 §16.1: text-indent contributes to intrinsic width
             ;; only add it when this block directly contains text children (first line)
             (define text-indent-val
               (let ([ti (get-style-prop styles 'text-indent 0)])
                 (cond
                   [(number? ti) ti]
                   [(and (pair? ti) (eq? (car ti) 'px)) (cadr ti)]
                   [else 0])))
             (define has-direct-text?
               (for/or ([c (in-list children)])
                 (match c [`(text . ,_) #t] [_ #f])))
             (define content-w-with-indent
               (if (and has-direct-text? (> text-indent-val 0))
                   (+ max-child-w text-indent-val)
                   max-child-w))
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
             (max min-w (min max-w content-w-with-indent)))
           initial-content-w))

     ;; lay out children within the content area
     (define child-avail
       `(avail (definite ,content-w) ,(if explicit-h `(definite ,explicit-h) 'indefinite)))

     ;; text-align: inherited property for centering text within block
     (define text-align (get-style-prop styles 'text-align 'left))

     (define-values (child-views content-height)
       (layout-block-children children child-avail bm dispatch-fn text-align styles))

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
(define (layout-block-children children child-avail parent-bm dispatch-fn [text-align 'left] [parent-styles #f])
  (define avail-w (avail-width->number (cadr child-avail)))
  (define avail-h (avail-height->number (caddr child-avail)))
  ;; padding offset for child positioning
  (define pad-left (box-model-padding-left parent-bm))
  (define pad-top (box-model-padding-top parent-bm))
  (define border-left (box-model-border-left parent-bm))
  (define border-top (box-model-border-top parent-bm))
  (define offset-x (+ pad-left border-left))
  (define offset-y (+ pad-top border-top))

  ;; resolve text-indent from parent styles (CSS 2.2 §16.1)
  ;; text-indent applies to the first line of a block container
  (define text-indent-raw
    (if parent-styles
        (get-style-prop parent-styles 'text-indent 0)
        0))
  (define text-indent
    (cond
      [(number? text-indent-raw) text-indent-raw]
      [(and (pair? text-indent-raw) (eq? (car text-indent-raw) 'px))
       (cadr text-indent-raw)]
      [(and (pair? text-indent-raw) (eq? (car text-indent-raw) '%))
       ;; percentage resolves against containing block width
       (if (and avail-w (number? avail-w))
           (* (/ (cadr text-indent-raw) 100) avail-w)
           0)]
      [else 0]))
  ;; track whether we've applied text-indent to the first text child
  (define text-indent-applied? #f)

  ;; CSS 2.2 §9.5: float context — track active floats
  ;; each float: (side x-start y-start width height)
  ;; side: 'left or 'right
  (define float-lefts '())   ;; list of (x y w h) for left floats

  ;; helper: compute left float intrusion at a given y position
  (define (float-left-intrusion-at y h)
    (for/fold ([max-right 0]) ([f (in-list float-lefts)])
      (match f
        [(list fx fy fw fh)
         ;; float occupies y range [fy, fy+fh)
         ;; child occupies y range [y, y+h)
         (if (and (< y (+ fy fh)) (< fy (+ y h)))
             (max max-right (+ fx fw))
             max-right)])))

  ;; CSS ::before pseudo-element block height
  ;; if parent has __before-block-height style, add it as initial y offset
  (define before-block-h
    (if parent-styles
        (let ([bh (get-style-prop parent-styles '__before-block-height 0)])
          (if (number? bh) bh 0))
        0))

  ;; CSS 2.2 §9.2.1.1: block-in-inline strut heights
  ;; when an inline element is converted to block because it contains block children,
  ;; add strut line-height at the beginning and/or end for empty anonymous block portions
  (define before-strut-h
    (if parent-styles
        (let ([sh (get-style-prop parent-styles '__before-strut-height 0)])
          (if (number? sh) sh 0))
        0))
  (define after-strut-h
    (if parent-styles
        (let ([sh (get-style-prop parent-styles '__after-strut-height 0)])
          (if (number? sh) sh 0))
        0))

  ;; combine before offsets
  (define initial-y (+ before-block-h before-strut-h))

  (let loop ([remaining children]
             [current-y initial-y]
             [prev-margin-bottom 0]
             [views '()]
             [is-first-child? #t])
    (cond
      [(null? remaining)
       ;; CSS 2.1 §8.3.1: the last child's bottom margin stays inside the parent
       ;; when the parent has non-zero bottom border-width or bottom padding.
       ;; Otherwise it collapses through to become the parent's bottom margin.
       (define parent-has-bottom-barrier?
         (or (> (box-model-padding-bottom parent-bm) 0)
             (> (box-model-border-bottom parent-bm) 0)))
       (define final-y
         (+ (if parent-has-bottom-barrier?
                (+ current-y prev-margin-bottom)
                current-y)
            after-strut-h))
       (values (reverse views) final-y)]
      [else
       (define child (car remaining))

       ;; skip display:none children entirely
       (cond
         [(match child [`(none ,_) #t] [_ #f])
          (loop (cdr remaining) current-y prev-margin-bottom views is-first-child?)]
         ;; skip absolute/fixed positioned children (they're laid out by containing block)
         [(let ([s (get-box-styles child)])
            (let ([pos (get-style-prop s 'position 'static)])
              (or (eq? pos 'absolute) (eq? pos 'fixed))))
          (loop (cdr remaining) current-y prev-margin-bottom views is-first-child?)]

         ;; CSS 2.2 §9.5: floated children are taken out of normal flow
         [(let ([s (get-box-styles child)])
            (let ([float-val (get-style-prop s 'float #f)])
              (and float-val (not (eq? float-val 'float-none)))))
          ;; lay out the float using shrink-to-fit width (CSS 2.2 §10.3.5)
          ;; float uses max-content intrinsic width, capped at parent available
          (define child-styles (get-box-styles child))
          (define float-side (get-style-prop child-styles 'float 'float-none))
          (define float-avail
            `(avail av-max-content ,(caddr child-avail)))
          (define child-view (dispatch-fn child float-avail))
          (define child-bm (extract-box-model child-styles avail-w))
          (define cw (view-width child-view))
          (define ch (view-height child-view))
          ;; position float: left floats go at the left edge at current-y
          (define float-x (+ offset-x (box-model-margin-left child-bm)))
          (define float-y (+ offset-y current-y))
          (define positioned-float (set-view-position child-view float-x float-y))
          ;; record float for BFC avoidance
          (when (eq? float-side 'float-left)
            (set! float-lefts
                  (cons (list 0 current-y cw ch) float-lefts)))
          ;; floats don't advance the y cursor
          (loop (cdr remaining)
                current-y
                prev-margin-bottom
                (cons positioned-float views)
                is-first-child?)]

         [else
          ;; check if this child creates a new BFC (overflow != visible)
          ;; and if there are active floats to avoid
          (define child-styles-pre (get-box-styles child))
          (define child-overflow (get-style-prop child-styles-pre 'overflow #f))
          (define creates-bfc?
            (and child-overflow
                 (memq child-overflow '(scroll auto hidden))))
          ;; compute float avoidance offset for BFC children
          (define bfc-float-offset
            (if creates-bfc?
                (let* ([child-bm-pre (extract-box-model child-styles-pre avail-w)]
                       [ch-est (or (resolve-block-height child-styles-pre avail-h avail-w) 50)]
                       [intrusion (float-left-intrusion-at current-y ch-est)])
                  intrusion)
                0))
          ;; for BFC children, reduce available width by float offset
          (define effective-child-avail
            (if (> bfc-float-offset 0)
                `(avail (definite ,(- avail-w bfc-float-offset)) ,(caddr child-avail))
                child-avail))
          ;; CSS 2.2 §16.1: text-indent affects first line available width for wrapping
          ;; negative indent → wider first line, text moves left off-screen
          (define text-indent-avail
            (if (and (not text-indent-applied?)
                     (< text-indent 0)
                     (match child [`(text . ,_) #t] [_ #f]))
                (let ([adj-w (- (avail-width->number (cadr effective-child-avail)) text-indent)])
                  `(avail (definite ,adj-w) ,(caddr effective-child-avail)))
                effective-child-avail))
          (define child-view (dispatch-fn child text-indent-avail))

          ;; extract child's box model for margin handling
          (define child-styles (get-box-styles child))
          (define child-bm (extract-box-model child-styles avail-w))

          ;; CSS 2.1 §8.3.1: first child's top margin collapses with parent's
          ;; top margin when the parent has no top border and no top padding.
          (define parent-has-top-barrier?
            (or (> (box-model-padding-top parent-bm) 0)
                (> (box-model-border-top parent-bm) 0)))

          ;; collapse top margin with previous bottom margin
          ;; for the first child with no top barrier, the child's top margin
          ;; collapses through the parent (i.e., we don't add it here)
          (define collapsed-margin
            (cond
              [(and is-first-child? (not parent-has-top-barrier?))
               ;; first child margin collapses through parent
               0]
              [else
               (collapse-margins prev-margin-bottom
                                (box-model-margin-top child-bm))]))

          ;; position child
          ;; apply text-indent to the first text child in this block
          (define indent-offset
            (if (and (not text-indent-applied?)
                     (not (= text-indent 0))
                     (match child-view [`(view-text . ,_) #t] [_ #f]))
                (begin
                  (set! text-indent-applied? #t)
                  text-indent)
                0))
          ;; inline ::before content width: offset first text child
          (define before-inline-w
            (if (and is-first-child? parent-styles
                     (match child-view [`(view-text . ,_) #t] [_ #f]))
                (let ([w (get-style-prop parent-styles '__before-inline-width 0)])
                  (if (number? w) w 0))
                0))
          (define child-x
            (cond
              ;; text-align applies to inline-level content (text views)
              [(and (match child-view [`(view-text . ,_) #t] [_ #f])
                    (eq? text-align 'center)
                    avail-w)
               (+ offset-x bfc-float-offset indent-offset before-inline-w (max 0 (/ (- avail-w (view-width child-view)) 2)))]
              [(and (match child-view [`(view-text . ,_) #t] [_ #f])
                    (eq? text-align 'right)
                    avail-w)
               (+ offset-x bfc-float-offset indent-offset before-inline-w (max 0 (- avail-w (view-width child-view))))]
              [(match child-view [`(view-text . ,_) #t] [_ #f])
               (+ offset-x bfc-float-offset indent-offset before-inline-w)]
              [else
               (+ offset-x bfc-float-offset (box-model-margin-left child-bm))]))
          (define child-y (+ offset-y current-y collapsed-margin))

          ;; for text views with half-leading (line-height > font-size),
          ;; preserve the internal y offset from layout-text
          (define effective-child-y
            (if (match child-view [`(view-text . ,_) #t] [_ #f])
                (+ child-y (view-y child-view))  ;; add half-leading
                child-y))

          ;; update the view with computed position
          (define positioned-view (set-view-position child-view child-x effective-child-y))

          ;; apply relative positioning offset after block positioning
          ;; pass containing block dimensions for percentage resolution
          (define final-view (apply-relative-offset positioned-view child-styles avail-w avail-h))

          ;; advance y by child's border-box height
          ;; for text children, use line-height from styles (not display height)
          ;; because CSS line-height determines the line box height for stacking
          ;; display-h = (n-1)*lh + fs → stacking-h = n*lh = display-h + (lh - fs)
          (define child-h
            (match child
              [`(text ,_ ,child-text-styles ,_ ,_)
               (let ([lh (get-style-prop child-text-styles 'line-height #f)]
                     [fs (get-style-prop child-text-styles 'font-size 10)])
                 (if (and lh (number? lh) (> lh fs))
                     (+ (view-height child-view) (- lh fs))
                     (view-height child-view)))]
              [_ (view-height child-view)]))
          (define new-y (+ current-y collapsed-margin child-h))

          (loop (cdr remaining)
                new-y
                (box-model-margin-bottom child-bm)
                (cons final-view views)
                #f)])])))

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
