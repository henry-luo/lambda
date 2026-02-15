#lang racket/base

;; layout-positioned.rkt — Positioned layout (absolute, fixed, relative, sticky)
;;
;; Implements CSS 2.2 §9.3 positioned elements.
;; Corresponds to Radiant's layout_positioned.cpp.

(require racket/match
         racket/function
         racket/math
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
     (define bm (extract-box-model styles containing-w))

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
         ;; shrink-to-fit: measure content with max-content, clamp to containing block
         [else
          (define shrink-avail (max 0 (- containing-w (horizontal-pb bm) (horizontal-margin bm))))
          ;; measure max-content (preferred) width
          (define static-s (override-position styles 'static))
          (define static-b (replace-box-styles box static-s))
          (define measure-avail `(avail av-max-content indefinite))
          (define measure-view (dispatch-fn static-b measure-avail))
          (define intrinsic-w (view-width measure-view))
          ;; measure min-content (preferred minimum) width
          (define min-measure-avail `(avail av-min-content indefinite))
          (define min-measure-view (dispatch-fn static-b min-measure-avail))
          (define min-intrinsic-w (view-width min-measure-view))
          ;; shrink-to-fit: min(max(preferred-minimum, available), preferred)
          ;; per CSS 2.2 §10.3.7
          ;; content width = intrinsic minus pb (since intrinsic is border-box)
          (define content-intrinsic (max 0 (- intrinsic-w (horizontal-pb bm))))
          (define min-content-intrinsic (max 0 (- min-intrinsic-w (horizontal-pb bm))))
          (min content-intrinsic (max min-content-intrinsic shrink-avail))]))

     ;; apply min/max width constraints
     (define min-w-val (get-style-prop styles 'min-width 'auto))
     (define max-w-val (get-style-prop styles 'max-width 'none))
     (define min-w (or (resolve-size-value min-w-val containing-w) 0))
     (define max-w (or (resolve-size-value max-w-val containing-w) +inf.0))
     ;; min/max are border-box when box-sizing:border-box, convert to content
     (define min-w-content
       (if (eq? (box-model-box-sizing bm) 'border-box)
           (max 0 (- min-w (horizontal-pb bm)))
           min-w))
     (define max-w-content
       (if (and (eq? (box-model-box-sizing bm) 'border-box) (not (infinite? max-w)))
           (max 0 (- max-w (horizontal-pb bm)))
           max-w))
     (set! content-w (max min-w-content (min max-w-content content-w)))

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

     ;; apply min/max height constraints (when content-h is resolved)
     (when content-h
       (define min-h-val (get-style-prop styles 'min-height 'auto))
       (define max-h-val (get-style-prop styles 'max-height 'none))
       (define min-h (or (resolve-size-value min-h-val containing-h) 0))
       (define max-h (or (resolve-size-value max-h-val containing-h) +inf.0))
       (define min-h-c
         (if (eq? (box-model-box-sizing bm) 'border-box)
             (max 0 (- min-h (vertical-pb bm)))
             min-h))
       (define max-h-c
         (if (and (eq? (box-model-box-sizing bm) 'border-box) (not (infinite? max-h)))
             (max 0 (- max-h (vertical-pb bm)))
             max-h))
       (set! content-h (max min-h-c (min max-h-c content-h))))

     ;; === aspect-ratio support ===
     ;; If aspect-ratio is set, derive the missing dimension from the known one.
     ;; width takes precedence when both axes are determinate.
     (define ar (get-style-prop styles 'aspect-ratio #f))
     (when (and ar (number? ar) (> ar 0))
       (define has-definite-w (or resolved-w (and left-val right-val)))
       (define has-definite-h (or resolved-h (and top-val bottom-val)))
       (cond
         ;; width is definite (explicit or from insets) → derive height
         [has-definite-w
          (set! content-h (/ content-w ar))]
         ;; height is definite → derive width (override shrink-to-fit)
         [(and has-definite-h content-h)
          (set! content-w (* content-h ar))]
         ;; neither axis definite, but min-width may have increased content-w
         [(> content-w 0)
          (set! content-h (/ content-w ar))]))

     ;; lay out children to determine auto height
     ;; override position to static to avoid infinite recursion
     ;; (dispatch checks position and would re-call layout-positioned)
     (define static-styles (override-position styles 'static))
     (define static-box (replace-box-styles box static-styles))
     ;; avail must include our own padding+border+margin so dispatch's
     ;; resolve-block-width can re-derive the correct content-w:
     ;;   content-w = avail-w - horizontal-pb - horizontal-margin
     (define avail-w-for-dispatch (+ content-w (horizontal-pb bm) (horizontal-margin bm)))
     (define avail-h-for-dispatch
       (if content-h
           (+ content-h (vertical-pb bm) (vertical-margin bm))
           #f))
     (define avail `(avail (definite ,avail-w-for-dispatch)
                           ,(if avail-h-for-dispatch
                                `(definite ,avail-h-for-dispatch)
                                'indefinite)))
     (define child-view (dispatch-fn static-box avail))
     ;; actual-h is content height; when auto, use child-view's height
     ;; but child-view returns border-box height, so convert back to content height
     (define actual-h
       (or content-h
           (max 0 (- (view-height child-view) (vertical-pb bm)))))

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

     ;; set both position AND size
     ;; final dimensions are the resolved border-box size
     (define final-w (compute-border-box-width bm content-w))
     (define final-h (compute-border-box-height bm actual-h))
     (set-view-size (set-view-pos child-view x y) final-w final-h)]

    [_ (error 'layout-positioned "unexpected box: ~a" box)]))

;; ============================================================
;; Relative Positioning (CSS 2.2 §9.4.3)
;; ============================================================

;; apply relative offset to an already-laid-out view.
;; does not affect layout of siblings.
;; containing-w / containing-h are optional containing-block dimensions
;; for resolving percentage offsets (left/right use width, top/bottom use height).
(define (apply-relative-offset view styles [containing-w #f] [containing-h #f])
  (define pos (get-style-prop styles 'position 'static))
  (cond
    [(eq? pos 'relative)
     (define css-top (get-style-prop styles 'top 'auto))
     (define css-left (get-style-prop styles 'left 'auto))
     (define css-bottom (get-style-prop styles 'bottom 'auto))
     (define css-right (get-style-prop styles 'right 'auto))

     ;; top takes precedence over bottom; left over right
     ;; percentage offsets resolve against the containing block dimensions
     (define dx
       (cond
         [(resolve-size-value css-left containing-w) => identity]
         [(resolve-size-value css-right containing-w) => (λ (v) (- v))]
         [else 0]))
     (define dy
       (cond
         [(resolve-size-value css-top containing-h) => identity]
         [(resolve-size-value css-bottom containing-h) => (λ (v) (- v))]
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

;; override the position property in a styles term
(define (override-position styles new-pos)
  (match styles
    [`(style . ,props)
     (define new-props
       (cons `(position ,new-pos)
             (filter (lambda (p) (not (and (pair? p) (eq? (car p) 'position))))
                     props)))
     `(style ,@new-props)]
    [_ styles]))

;; replace the styles in a box term
(define (replace-box-styles box new-styles)
  (match box
    [`(block ,id ,_ ,children)       `(block ,id ,new-styles ,children)]
    [`(inline ,id ,_ ,children)      `(inline ,id ,new-styles ,children)]
    [`(inline-block ,id ,_ ,children) `(inline-block ,id ,new-styles ,children)]
    [`(flex ,id ,_ ,children)        `(flex ,id ,new-styles ,children)]
    [`(grid ,id ,_ ,gd ,children)    `(grid ,id ,new-styles ,gd ,children)]
    [`(table ,id ,_ ,children)       `(table ,id ,new-styles ,children)]
    [`(text ,id ,_ ,c ,w)           `(text ,id ,new-styles ,c ,w)]
    [`(replaced ,id ,_ ,iw ,ih)     `(replaced ,id ,new-styles ,iw ,ih)]
    [_ box]))

(define (set-view-pos view x y)
  (match view
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))

(define (set-view-size view w h)
  (match view
    [`(view ,id ,x ,y ,_ ,_ ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,x ,y ,_ ,_ ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))
