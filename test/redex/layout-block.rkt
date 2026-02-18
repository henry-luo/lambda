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
         "layout-positioned.rkt"
         "layout-inline.rkt")

(provide layout-block
         layout-block-children
         current-ancestor-floats
         bfc-max-float-bottom)

;; ============================================================
;; Float Context Propagation
;; ============================================================

;; CSS 2.2 §9.5: floats affect line boxes in all descendant non-BFC blocks.
;; This parameter propagates float positions from ancestor blocks so that
;; inline content in descendant blocks wraps around the floats correctly.
;; Value: (list left-floats right-floats) where each is a list of (x y w h)
;; coordinates relative to the nearest block formatting context root.
(define current-ancestor-floats (make-parameter '(() ())))

;; CSS 2.2 §10.6.7: BFC roots must contain all descendant floats.
;; Non-BFC child blocks report their float bottoms (in BFC-root content coordinates)
;; to the nearest BFC ancestor via this boxed value.
;; Value: (box number) — mutable box holding the max float bottom in BFC-root coordinates
(define bfc-max-float-bottom (make-parameter (box 0)))
;; Cumulative y-offset from the BFC root's content area to the current block's content area.
;; Used to translate local float positions to BFC-root coordinates.
(define bfc-y-offset (make-parameter 0))

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
             ;; find max child border-box width + margins to get content-box width
             ;; CSS 2.2 §10.3.5: intrinsic width includes the full margin box of children
             (define offset-x (+ (box-model-padding-left bm) (box-model-border-left bm)))
             (define max-child-w
               (for/fold ([mx 0]) ([v (in-list measure-views)]
                                   [c (in-list (filter
                                                 (lambda (ch)
                                                   (not (match ch
                                                          [`(none ,_) #t]
                                                          [_ #f])))
                                                 children))])
                 (define child-w (view-width v))
                 (define child-x (view-x v))
                 ;; child-x includes offset-x + margin-left for left/intrinsic floats
                 ;; add margin-right to get the full margin-box contribution
                 (define child-styles (get-box-styles c))
                 (define child-bm (extract-box-model child-styles (or avail-w 0)))
                 (define margin-right (box-model-margin-right child-bm))
                 (define child-end (+ (- child-x offset-x) child-w margin-right))
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

     ;; CSS 2.2 §12.1: inline ::before pseudo-element on empty element generates a line box
     ;; that contributes its line-height to the parent's content height
     (define before-line-h
       (let ([blh (get-style-prop styles '__before-line-height 0)])
         (if (number? blh) blh 0)))

     ;; final content height: explicit or determined by children
     ;; apply min-height / max-height even for auto (content-determined) height
     (define final-content-h
       (let ([raw-h (or explicit-h (max content-height before-line-h))])
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

  ;; CSS 2.2 §9.5: float context — track placed floats for band-based positioning
  ;; each entry: (local-x local-y margin-box-width height) where local-x is relative to content edge
  ;; Initialize with any inherited ancestor floats (propagated from parent non-BFC blocks)
  (define ancestor-float-ctx (current-ancestor-floats))
  (define float-lefts (first ancestor-float-ctx))   ;; list of (x y w h) for left floats
  (define float-rights (second ancestor-float-ctx))  ;; list of (x y w h) for right floats
  (define max-float-bottom 0) ;; track maximum float bottom for BFC containment

  ;; helper: compute right edge of left floats overlapping y range [y, y+h)
  ;; returns local x offset (0 if no overlapping floats)
  (define (float-left-edge-at y h)
    (for/fold ([max-right 0]) ([f (in-list float-lefts)])
      (match f
        [(list fx fy fw fh)
         ;; float occupies y range [fy, fy+fh), query range [y, y+h)
         ;; overlap requires both ranges to intersect and float has nonzero height
         (if (and (> fh 0) (< y (+ fy fh)) (< fy (+ y h)))
             (max max-right (+ fx fw))
             max-right)])))

  ;; helper: compute left edge of right floats overlapping y range [y, y+h)
  ;; returns local x offset from content-left (avail-w if no overlapping right floats)
  (define (float-right-edge-at y h)
    (define aw (if (and avail-w (number? avail-w)) avail-w 0))
    (for/fold ([min-left aw]) ([f (in-list float-rights)])
      (match f
        [(list fx fy fw fh)
         (if (and (> fh 0) (< y (+ fy fh)) (< fy (+ y h)))
             (min min-left fx)
             min-left)])))

  ;; alias for BFC avoidance
  (define (float-left-intrusion-at y h) (float-left-edge-at y h))

  ;; CSS 2.2 §9.5: find y position where a BFC child of given outer-width fits
  ;; beside floats. The BFC box must not overlap any float margin boxes.
  ;; If it doesn't fit at start-y, shift down past float bottoms until it fits.
  ;; Returns (values left-offset adjusted-y) where left-offset is the x shift
  ;; to avoid left floats, and adjusted-y is where the child should be placed.
  (define (find-bfc-fit-position child-outer-w child-est-h start-y)
    (define aw (if (and avail-w (number? avail-w)) avail-w 0))
    (define all-floats (append float-lefts float-rights))
    ;; collect all unique float-bottom y values >= start-y as candidate break points
    (define break-points
      (sort
       (remove-duplicates
        (filter (lambda (by) (> by start-y))
                (for/list ([f (in-list all-floats)])
                  (match f [(list fx fy fw fh) (+ fy fh)]))))
       <))
    ;; try placing at start-y first, then at each break point
    (let loop ([candidates (cons start-y break-points)])
      (cond
        [(null? candidates)
         ;; no more floats to dodge — place below all floats
         (values 0 start-y)]
        [else
         (define try-y (car candidates))
         (define left-intr (float-left-edge-at try-y (max child-est-h 1)))
         (define right-edge (float-right-edge-at try-y (max child-est-h 1)))
         (define available-beside (- right-edge left-intr))
         (if (>= available-beside child-outer-w)
             ;; fits here
             (values left-intr try-y)
             ;; doesn't fit, try next break point
             (loop (cdr candidates)))])))

  ;; CSS 2.2 §9.5.2: compute clear offset — how far to push current-y below floats
  ;; clear-val: 'clear-left, 'clear-right, 'clear-both, or 'clear-none / #f
  ;; returns the minimum y that places the element below the relevant floats
  (define (compute-clear-y clear-val current-y)
    (define (max-bottom floats)
      (for/fold ([mb current-y]) ([f (in-list floats)])
        (match f
          [(list fx fy fw fh)
           (max mb (+ fy fh))])))
    (cond
      [(eq? clear-val 'clear-left) (max-bottom float-lefts)]
      [(eq? clear-val 'clear-right) (max-bottom float-rights)]
      [(eq? clear-val 'clear-both) (max (max-bottom float-lefts) (max-bottom float-rights))]
      [else current-y]))

  ;; CSS 2.2 §9.5.1 Rule 3/7: find y position where a float of given margin-box
  ;; width and height fits. If there's not enough horizontal space at start-y,
  ;; shift down past float bottoms until it fits or there are no more floats.
  ;; Returns the adjusted y for the float's margin-top edge.
  (define (find-float-fit-y float-margin-w float-margin-h start-y)
    (define aw (if (and avail-w (number? avail-w)) avail-w 0))
    (if (<= float-margin-w aw)
        ;; float fits within the container — check float bands
        (let ()
          (define all-floats (append float-lefts float-rights))
          (define break-points
            (sort
             (remove-duplicates
              (filter (lambda (by) (> by start-y))
                      (for/list ([f (in-list all-floats)])
                        (match f [(list fx fy fw fh) (+ fy fh)]))))
             <))
          (let loop ([candidates (cons start-y break-points)])
            (cond
              [(null? candidates) start-y]
              [else
               (define try-y (car candidates))
               (define left-intr (float-left-edge-at try-y (max float-margin-h 1)))
               (define right-edge (float-right-edge-at try-y (max float-margin-h 1)))
               (define available (- right-edge left-intr))
               (if (>= available float-margin-w)
                   try-y
                   (loop (cdr candidates)))])))
        ;; float is wider than container — just use start-y (can't fit anywhere)
        start-y))

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

  ;; CSS 2.2 §12.2: ::after pseudo-element with block display adds height
  ;; after all children of the parent element
  (define after-block-h
    (if parent-styles
        (let ([abh (get-style-prop parent-styles '__after-block-height 0)])
          (if (number? abh) abh 0))
        0))

  ;; combine before offsets
  (define initial-y (+ before-block-h before-strut-h))

  ;; CSS 2.2 §9.4.1: determine if this block establishes a BFC (needed early for
  ;; float containment propagation from descendant non-BFC blocks)
  (define establishes-bfc?
    (and parent-styles
         (let ([overflow (get-style-prop parent-styles 'overflow #f)]
               [display (get-style-prop parent-styles 'display #f)]
               [float (get-style-prop parent-styles 'float #f)]
               [position (get-style-prop parent-styles 'position #f)]
               [bfc-flag (get-style-prop parent-styles '__establishes-bfc #f)])
           (or bfc-flag
               (and overflow (memq overflow '(hidden auto scroll)))
               (and display (memq display '(inline-block table-cell table-caption table inline-table flex grid)))
               (and float (not (eq? float 'float-none)))
               (and position (memq position '(absolute fixed)))))))

  ;; BFC root: create fresh accumulator for descendant float bottoms
  ;; Non-BFC: inherit parent's accumulator (floats propagate upward)
  (define descendant-float-box
    (if establishes-bfc? (box 0) (bfc-max-float-bottom)))

  ;; ============================================================
  ;; CSS 2.2 §9.4.2: Inline Formatting Context Detection
  ;; ============================================================
  ;; If ALL visible children are inline-level (text, inline, inline-block),
  ;; use inline formatting context to lay them out horizontally with line wrapping,
  ;; instead of the default vertical block stacking.
  (define visible-children
    (filter (lambda (c)
              (and (not (match c [`(none ,_) #t] [_ #f]))
                   ;; skip absolute/fixed positioned children
                   (let ([s (get-box-styles c)])
                     (let ([pos (get-style-prop s 'position 'static)])
                       (not (or (eq? pos 'absolute) (eq? pos 'fixed)))))))
            children))

  (define all-inline-children?
    (and (not (null? visible-children))
         ;; Only use IFC path when there's at least one inline-block child.
         ;; Pure text/inline blocks are handled correctly by existing block layout.
         (for/or ([c (in-list visible-children)])
           (match c [`(inline-block . ,_) #t] [_ #f]))
         ;; don't use IFC path if there are floats among children (floats need block layout)
         (for/and ([c (in-list visible-children)])
           (match c
             [`(text . ,_) #t]
             [`(inline-block . ,_) #t]
             [`(inline . ,_) #t]
             [_ #f]))))

  (if all-inline-children?
      ;; === Inline formatting context path ===
      ;; Lay out inline-level children horizontally with line wrapping
      (let ()
        (define inline-avail-w (or avail-w +inf.0))
        ;; Create a zero box model for layout-inline-children since we handle
        ;; padding/border offsets ourselves
        (define zero-bm (extract-box-model '(style) 0))
        (define-values (raw-views total-height cursor-line-widths)
          (layout-inline-children visible-children inline-avail-w zero-bm dispatch-fn avail-h))

        ;; CSS 2.2 §10.6.1: each line box starts with a strut — an imaginary
        ;; zero-width inline box with the containing block's font & line-height.
        ;; For baseline-aligned inline-blocks whose baseline is at the bottom
        ;; margin edge (no in-flow line boxes), the strut's descent extends below
        ;; the baseline, adding to the line box height. Only add when:
        ;; 1. No text children present (text includes line-height covering strut)
        ;; 2. All inline-blocks use baseline alignment (not top/bottom)
        ;; 3. At least one inline-block has no in-flow line boxes (baseline at bottom)
        (define has-inline-block?
          (for/or ([c (in-list visible-children)])
            (match c [`(inline-block . ,_) #t] [_ #f])))
        (define has-text-children?
          (for/or ([c (in-list visible-children)])
            (match c [`(text . ,_) #t] [_ #f])))
        ;; check if any inline-block has non-baseline vertical-align
        (define all-baseline-aligned?
          (for/and ([c (in-list visible-children)])
            (match c
              [`(inline-block ,_ ,styles . ,_)
               (let ([va (get-style-prop styles 'vertical-align #f)])
                 (or (not va) (eq? va 'baseline)))]
              [_ #t])))
        ;; CSS §10.8.1: an inline-block's baseline is the baseline of its last
        ;; in-flow line box, unless it has no in-flow line boxes or overflow≠visible.
        ;; When baseline is at the bottom margin edge, strut descent extends below.
        (define has-bottom-baseline-ib?
          (for/or ([c (in-list visible-children)])
            (match c
              [`(inline-block ,_ ,_ ,ib-children)
               ;; no in-flow line boxes = no text/inline children
               (not (for/or ([ibc (in-list ib-children)])
                      (match ibc
                        [`(text . ,_) #t]
                        [`(inline . ,_) #t]
                        [_ #f])))]
              [_ #f])))
        (define strut-descent
          (if (and has-inline-block? (not has-text-children?)
                   all-baseline-aligned? has-bottom-baseline-ib? parent-styles)
              (let* ([fs (get-style-prop parent-styles 'font-size 16)]
                     [font-type (get-style-prop parent-styles 'font-type #f)]
                     [lh-prop (get-style-prop parent-styles 'line-height #f)]
                     ;; Times hhea: ascender=891, descender=216, unitsPerEm=1000
                     ;; → descent ratio = 216/1000 = 0.216
                     ;; Arial hhea: descender=434, unitsPerEm=2048
                     ;; → descent ratio = 434/2048 ≈ 0.212
                     [descent-ratio (if (eq? font-type 'arial) 0.212 0.216)]
                     [lh-ratio (if (eq? font-type 'arial) 1.15 1.107)]
                     [strut-lh (if (and lh-prop (number? lh-prop))
                                   lh-prop
                                   (* lh-ratio fs))]
                     [half-leading (/ (- strut-lh fs) 2)]
                     [descent (* descent-ratio fs)])
                (max 0 (+ descent half-leading)))
              0))
        (define effective-height (+ total-height strut-descent))

        ;; Group views by line: detect line boundaries by looking for x-coordinate
        ;; resets (x decreases from one view to the next = new line started)
        (define (group-by-line views)
          (if (null? views) '()
              (let loop ([remaining (cdr views)]
                         [current-line (list (car views))]
                         [prev-right-edge (+ (view-x (car views)) (view-width (car views)))]
                         [lines '()])
                (cond
                  [(null? remaining)
                   (reverse (cons (reverse current-line) lines))]
                  [else
                   (define v (car remaining))
                   (define vx (view-x v))
                   ;; if this view's x is less than the previous view's right edge,
                   ;; it must be on a new line (line-wrapped)
                   (if (< vx (- prev-right-edge 1))
                       (loop (cdr remaining)
                             (list v)
                             (+ vx (view-width v))
                             (cons (reverse current-line) lines))
                       (loop (cdr remaining)
                             (cons v current-line)
                             (+ vx (view-width v))
                             lines))]))))

        (define (shift-view v dx dy)
          (match v
            [`(view ,id ,x ,y ,w ,h ,ch ,baseline)
             `(view ,id ,(+ x dx) ,(+ y dy) ,w ,h ,ch ,baseline)]
            [`(view ,id ,x ,y ,w ,h ,ch)
             `(view ,id ,(+ x dx) ,(+ y dy) ,w ,h ,ch)]
            [`(view-text ,id ,x ,y ,w ,h ,text)
             `(view-text ,id ,(+ x dx) ,(+ y dy) ,w ,h ,text)]
            [_ v]))

        (define lines (group-by-line raw-views))

        ;; Apply text-align per line and text-indent on first line
        ;; using cursor-based line widths from layout-inline-children.
        (define aligned-views
          (apply append
            (for/list ([line (in-list lines)]
                       [lw (in-list cursor-line-widths)]
                       [line-idx (in-naturals)])
              ;; CSS §16.1: text-indent applies to the first line
              (define indent (if (= line-idx 0) text-indent 0))
              (define align-offset
                (cond
                  [(eq? text-align 'center) (max 0 (/ (- inline-avail-w lw) 2))]
                  [(eq? text-align 'right) (max 0 (- inline-avail-w lw))]
                  [else 0]))
              (for/list ([v (in-list line)])
                (shift-view v (+ offset-x align-offset indent) offset-y)))))

        (values aligned-views effective-height))

      ;; === Block formatting context path (existing vertical stacking) ===

  (let loop ([remaining children]
             [current-y initial-y]
             [prev-margin-bottom 0]
             [views '()]
             [is-first-child? #t]
             [column-ids '()])  ;; track table-column/column-group view IDs
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
            after-strut-h
            after-block-h))
       ;; CSS 2.2 §10.6.7: BFC-establishing containers expand to contain floats
       (define final-y-with-floats
         (if establishes-bfc?
             ;; BFC root: include both direct floats and descendant floats
             (let ([descendant-fb (unbox descendant-float-box)])
               (max final-y max-float-bottom descendant-fb))
             final-y))
       ;; CSS 2.2 §17.5.1: table-column and table-column-group don't generate visible boxes
       ;; but browsers report them with height matching the table's row area.
       ;; Post-process: set column/column-group view heights to the content height.
       (define final-views (reverse views))
       (define adjusted-views
         (if (null? column-ids)
             final-views
             (for/list ([v (in-list final-views)])
               (if (member (view-id v) column-ids)
                   (match v
                     [`(view ,vid ,vx ,vy ,vw ,vh ,vch . ,rest)
                      `(view ,vid ,vx ,vy ,vw ,final-y-with-floats ,vch . ,rest)]
                     [_ v])
                   v))))
       (values adjusted-views final-y-with-floats)]
      [else
       (define child (car remaining))

       ;; skip display:none children entirely
       (cond
         [(match child [`(none ,_) #t] [_ #f])
          (loop (cdr remaining) current-y prev-margin-bottom views is-first-child? column-ids)]
         ;; skip absolute/fixed positioned children (they're laid out by containing block)
         [(let ([s (get-box-styles child)])
            (let ([pos (get-style-prop s 'position 'static)])
              (or (eq? pos 'absolute) (eq? pos 'fixed))))
          (loop (cdr remaining) current-y prev-margin-bottom views is-first-child? column-ids)]

         ;; CSS 2.2 §9.3.2: <br> forced line break in block flow.
         ;; Position at the end of the previous sibling's line, with height equal
         ;; to one line of text. Don't advance y — the break is already reflected
         ;; by the vertical stacking of the surrounding text nodes.
         [(let ([s (get-box-styles child)])
            (get-style-prop s '__forced-break #f))
          (define br-id (match child [`(inline ,id ,_ ,_) id] [_ 'br]))
          ;; position at the right edge of the previous view (if any), on its line
          (define-values (br-x br-y br-h)
            (if (pair? views)
                (let ([prev (car views)])
                  (values (+ (view-x prev) (view-width prev))
                          (view-y prev)
                          (view-height prev)))
                (values (+ (box-model-padding-left parent-bm) (box-model-border-left parent-bm))
                        (+ (box-model-padding-top parent-bm) (box-model-border-top parent-bm))
                        0)))
          (define br-view `(view ,br-id ,br-x ,br-y 0 ,br-h ()))
          (loop (cdr remaining) current-y prev-margin-bottom (cons br-view views) is-first-child? column-ids)]

         ;; CSS 2.2 §9.5: floated children are taken out of normal flow
         [(let ([s (get-box-styles child)])
            (let ([float-val (get-style-prop s 'float #f)])
              (and float-val (not (eq? float-val 'float-none)))))
          ;; lay out the float using shrink-to-fit width (CSS 2.2 §10.3.5)
          ;; float uses max-content intrinsic width, capped at parent available
          (define child-styles (get-box-styles child))
          (define float-side (get-style-prop child-styles 'float 'float-none))
          ;; CSS 2.2 §9.5.2: clear on floated elements pushes them below prior floats
          (define float-clear (get-style-prop child-styles 'clear #f))
          ;; float positions from current-y + pending margin from previous normal-flow sibling
          ;; because float margins don't collapse with adjacent box margins
          (define float-base-y (+ current-y prev-margin-bottom))
          (define cleared-float-y (compute-clear-y float-clear float-base-y))
          ;; CSS 2.2 §10.3.5: floats with auto width use shrink-to-fit
          ;; (handled by dispatch), but floats with explicit width (e.g. width:100%)
          ;; need the real containing-block width for percentage resolution.
          ;; Pass the parent's available width so dispatch can resolve percentages,
          ;; while still triggering shrink-to-fit for auto-width floats.
          (define float-avail child-avail)
          (define child-view (dispatch-fn child float-avail))
          (define child-bm (extract-box-model child-styles avail-w))
          (define cw (view-width child-view))
          (define ch (view-height child-view))
          ;; CSS 2.2 §9.5.1: position float using band-based placement
          ;; compute available space at cleared y by checking overlap with existing floats
          (define float-margin-w (+ (box-model-margin-left child-bm) cw (box-model-margin-right child-bm)))
          ;; margin-box height for vertical overlap and clear calculations
          (define float-margin-h (+ (box-model-margin-top child-bm) ch (box-model-margin-bottom child-bm)))
          (define aw (if (and avail-w (number? avail-w)) avail-w 0))
          ;; CSS 2.2 §9.5.1 Rule 3/7: if the float doesn't fit beside existing
          ;; floats at cleared-float-y, shift it down to the first y where it fits
          (define fitted-float-y
            (if avail-w
                (find-float-fit-y float-margin-w float-margin-h cleared-float-y)
                cleared-float-y))
          ;; find the edges of existing floats that overlap vertically with this float
          (define left-edge (float-left-edge-at fitted-float-y (max float-margin-h 1)))
          (define right-edge (float-right-edge-at fitted-float-y (max float-margin-h 1)))
          (define float-x
            (cond
              ;; intrinsic sizing (avail-w is #f): position all floats from left
              ;; so their width contributes correctly to container measurement
              [(not avail-w)
               (+ offset-x left-edge (box-model-margin-left child-bm))]
              [(eq? float-side 'float-right)
               ;; CSS 2.2 §9.5.1: right float placed at right available edge
               (+ offset-x (- right-edge cw (box-model-margin-right child-bm)))]
              [else
               ;; CSS 2.2 §9.5.1: left float placed at left available edge
               (+ offset-x left-edge (box-model-margin-left child-bm))]))
          ;; CSS 2.2 §9.5: float margins don't collapse — add margin-top
          (define float-y (+ offset-y fitted-float-y (box-model-margin-top child-bm)))
          (define positioned-float (set-view-position child-view float-x float-y))
          ;; record float position for band-based stacking
          ;; store margin-box dimensions: y = margin-top edge, h = full margin-box height
          ;; this ensures both overlap checks and clear calculations use the full extent
          (cond
            [(not avail-w)
             ;; intrinsic sizing: treat all floats as left for measurement
             (set! float-lefts
                   (cons (list left-edge fitted-float-y float-margin-w float-margin-h) float-lefts))]
            [(eq? float-side 'float-left)
             (set! float-lefts
                   (cons (list left-edge fitted-float-y float-margin-w float-margin-h) float-lefts))]
            [else
             (set! float-rights
                   (cons (list (- right-edge float-margin-w) fitted-float-y float-margin-w float-margin-h) float-rights))])
          ;; track float bottom for BFC containment (margin-box bottom)
          (set! max-float-bottom (max max-float-bottom (+ fitted-float-y float-margin-h)))
          ;; CSS 2.2 §10.6.7: report float bottom to nearest BFC ancestor
          ;; translate local float-bottom to BFC-root coordinates
          (let ([local-float-bottom (+ fitted-float-y float-margin-h)]
                [y-off (bfc-y-offset)])
            (set-box! descendant-float-box
                      (max (unbox descendant-float-box) (+ y-off local-float-bottom))))
          ;; CSS 2.2 §9.5 + §9.4.3: if a float also has position: relative,
          ;; the relative offset is applied visually after float positioning.
          ;; The offset does NOT affect other floats' placement.
          (define final-float (apply-relative-offset positioned-float child-styles avail-w
                                (if (and avail-h (number? avail-h)) avail-h #f)))
          ;; floats don't advance the y cursor
          (loop (cdr remaining)
                current-y
                prev-margin-bottom
                (cons final-float views)
                is-first-child?
                column-ids)]

         [else
          ;; check if this child creates a new BFC (overflow != visible)
          ;; and if there are active floats to avoid
          (define child-styles-pre (get-box-styles child))
          (define child-overflow (get-style-prop child-styles-pre 'overflow #f))
          ;; CSS 2.2 §9.5.2: clear property pushes element below floats
          (define clear-val (get-style-prop child-styles-pre 'clear #f))
          (define cleared-y-initial (compute-clear-y clear-val current-y))
          ;; CSS 2.2 §17.2: tables always establish a block formatting context
          (define is-table-child?
            (match child [`(table . ,_) #t] [_ #f]))
          (define creates-bfc?
            (or (and child-overflow
                     (memq child-overflow '(scroll auto hidden)))
                is-table-child?))
          ;; CSS 2.2 §9.5: BFC boxes must not overlap float margin boxes.
          ;; Compute the x-offset to avoid left floats. If the BFC child's
          ;; outer width doesn't fit beside the floats, shift it down below
          ;; the nearest float bottom until it fits.
          ;; CSS 2.2 §17.5.2: tables with auto width use shrink-to-fit sizing,
          ;; so we must lay them out first to determine actual width for BFC avoidance.
          (define table-pre-view
            (if (and is-table-child? (or (pair? float-lefts) (pair? float-rights)) avail-w)
                (dispatch-fn child child-avail)
                #f))
          (define-values (bfc-float-offset cleared-y)
            (if (and creates-bfc? avail-w)
                (let* ([child-bm-pre (extract-box-model child-styles-pre avail-w)]
                       [ch-est (if table-pre-view
                                   (view-height table-pre-view)
                                   (or (resolve-block-height child-styles-pre avail-h avail-w) 50))]
                       ;; compute the child's outer width (margin-box)
                       ;; for tables, use actual laid-out width (shrink-to-fit)
                       [child-outer-w
                        (if table-pre-view
                            (+ (view-width table-pre-view)
                               (box-model-margin-left child-bm-pre)
                               (box-model-margin-right child-bm-pre))
                            (let ([child-css-w (resolve-block-width child-styles-pre avail-w)])
                              (+ child-css-w
                                 (horizontal-pb child-bm-pre)
                                 (horizontal-margin child-bm-pre))))])
                  (find-bfc-fit-position child-outer-w ch-est cleared-y-initial))
                (values 0 cleared-y-initial)))
          ;; CSS 2.2 §9.5: non-BFC in-flow content alongside floats
          ;; Line boxes next to a float are shortened to make room for the float's margin box.
          ;; For text nodes and inline-level children, reduce available width by float intrusion.
          (define is-text-child? (match child [`(text . ,_) #t] [_ #f]))
          (define has-floats? (or (pair? float-lefts) (pair? float-rights)))
          (define-values (float-left-intrusion float-right-intrusion)
            (if (and has-floats? (not creates-bfc?) avail-w)
                (let* ([est-h (if is-text-child? 
                                  ;; estimate text height as at least one line
                                  (let ([fs (get-style-prop child-styles-pre 'font-size 16)])
                                    (max fs 1))
                                  50)]
                       [left-intr (float-left-edge-at cleared-y est-h)]
                       [right-edge (float-right-edge-at cleared-y est-h)]
                       [right-intr (- avail-w right-edge)])
                  (values left-intr (max 0 right-intr)))
                (values 0 0)))
          (define float-narrowing (+ float-left-intrusion float-right-intrusion))
          ;; for BFC children, reduce available width by float offset.
          ;; For text children alongside floats (including inherited ancestor floats),
          ;; reduce available width by float intrusion so line boxes are shortened.
          ;; Note: this narrowing applies uniformly to all lines of the text.
          ;; When text extends below a float, this may over-narrow the later lines,
          ;; but for text entirely beside floats (common case), it's correct.
          (define effective-child-avail
            (cond
              [(> bfc-float-offset 0)
               `(avail (definite ,(- avail-w bfc-float-offset)) ,(caddr child-avail))]
              [(and (> float-narrowing 0) is-text-child?)
               `(avail (definite ,(max 0 (- avail-w float-narrowing))) ,(caddr child-avail))]
              [else child-avail]))
          ;; CSS 2.2 §16.1: text-indent affects first line available width for wrapping
          ;; negative indent → wider first line, text moves left off-screen
          (define text-indent-avail
            (if (and (not text-indent-applied?)
                     (< text-indent 0)
                     (match child [`(text . ,_) #t] [_ #f]))
                (let ([adj-w (- (avail-width->number (cadr effective-child-avail)) text-indent)])
                  `(avail (definite ,adj-w) ,(caddr effective-child-avail)))
                effective-child-avail))
          ;; CSS 2.2 §9.5: propagate float context to non-BFC block children
          ;; so their inline content (text) wraps around ancestor floats.
          ;; Translate parent float coordinates into child's coordinate system.
          (define is-block-child?
            (match child
              [`(block . ,_) #t]
              [_ #f]))
          ;; CSS 2.2 §10.6.7: compute the child's content-area y-offset from
          ;; the current block's content area, for BFC float-bottom propagation.
          ;; This is approximate (uses cleared-y without collapsed margin) but
          ;; sufficient for float containment tracking.
          (define child-bm-early (extract-box-model (get-box-styles child) avail-w))
          (define child-content-y-offset
            (+ cleared-y
               (box-model-border-top child-bm-early)
               (box-model-padding-top child-bm-early)))
          (define child-bfc-y-offset (+ (bfc-y-offset) child-content-y-offset))
          (define child-view-raw
            (if (and is-block-child? has-floats? (not creates-bfc?))
                ;; non-BFC block child alongside floats: propagate float context
                ;; The child's content area will be offset from parent's content area
                ;; by the child's padding+border. We translate float positions by
                ;; subtracting the child's expected position offset.
                (let* ([child-pad-left (box-model-padding-left child-bm-early)]
                       [child-border-left (box-model-border-left child-bm-early)]
                       [child-pad-top (box-model-padding-top child-bm-early)]
                       [child-border-top (box-model-border-top child-bm-early)]
                       [child-margin-left (box-model-margin-left child-bm-early)]
                       ;; child content area starts at: margin-left + border-left + pad-left
                       ;; relative to parent content edge
                       [dx (+ child-margin-left child-border-left child-pad-left)]
                       ;; vertical translation: child will be at cleared-y + collapsed margin
                       ;; + border-top + pad-top from parent content top
                       [dy (+ cleared-y child-border-top child-pad-top)]
                       ;; translate float positions to child's content coordinate system
                       [translated-lefts
                        (for/list ([f (in-list float-lefts)])
                          (match f
                            [(list fx fy fw fh)
                             (list (- fx dx) (- fy dy) fw fh)]))]
                       [translated-rights
                        (for/list ([f (in-list float-rights)])
                          (match f
                            [(list fx fy fw fh)
                             (list (- fx dx) (- fy dy) fw fh)]))]
                       ;; also include any inherited ancestor floats (translated further)
                       [ancestor (current-ancestor-floats)]
                       [ancestor-lefts (first ancestor)]
                       [ancestor-rights (second ancestor)]
                       [all-lefts (append translated-lefts
                                          (for/list ([f (in-list ancestor-lefts)])
                                            (match f
                                              [(list fx fy fw fh)
                                               (list (- fx dx) (- fy dy) fw fh)])))]
                       [all-rights (append translated-rights
                                           (for/list ([f (in-list ancestor-rights)])
                                             (match f
                                               [(list fx fy fw fh)
                                                (list (- fx dx) (- fy dy) fw fh)])))])
                  (parameterize ([current-ancestor-floats (list all-lefts all-rights)]
                                [bfc-max-float-bottom descendant-float-box]
                                [bfc-y-offset child-bfc-y-offset])
                    (dispatch-fn child text-indent-avail)))
                ;; non-block or BFC child: no float propagation needed
                ;; still parameterize BFC float tracking for descendant floats
                ;; for table BFC children, reuse pre-dispatched view to avoid double layout
                (if table-pre-view
                    table-pre-view
                    (parameterize ([bfc-max-float-bottom descendant-float-box]
                                   [bfc-y-offset child-bfc-y-offset])
                      (dispatch-fn child text-indent-avail)))))

          ;; CSS 2.2 §16.2: text-align:justify — justified text lines stretch to
          ;; fill the available width. The text view's width should be the container
          ;; width when the text wraps (multi-line). The last line is not justified
          ;; but the bounding box still spans the full width.
          (define child-view
            (if (and (eq? text-align 'justify)
                     avail-w
                     (match child-view-raw [`(view-text . ,_) #t] [_ #f])
                     (> (view-width child-view-raw) 0)
                     (< (view-width child-view-raw) avail-w))
                ;; text wraps: expand width to available width
                (match child-view-raw
                  [`(view-text ,vid ,vx ,vy ,vw ,vh ,vtext)
                   `(view-text ,vid ,vx ,vy ,avail-w ,vh ,vtext)])
                child-view-raw))

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
          (define is-inline-level-child?
            (match child
              [`(inline-block . ,_) #t]
              [`(inline . ,_) #t]
              [_ #f]))
          (define collapsed-margin
            (cond
              ;; CSS 2.2 §8.3.1: inline-block margins never collapse
              ;; with parent or siblings — use the full top margin as-is
              [is-inline-level-child?
               (box-model-margin-top child-bm)]
              [(and is-first-child? (not parent-has-top-barrier?))
               ;; first child margin collapses through parent
               0]
              [else
               (collapse-margins prev-margin-bottom
                                (box-model-margin-top child-bm))]))
          (define indent-offset
            (if (and (not text-indent-applied?)
                     (not (= text-indent 0))
                     (or (match child-view [`(view-text . ,_) #t] [_ #f])
                         is-inline-level-child?))
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
          ;; CSS 2.2 §12.5.1: list-style-position:inside marker offset
          ;; The marker is inline content before the first child, shifting text x
          (define list-marker-w
            (if (and is-first-child? parent-styles
                     (match child-view [`(view-text . ,_) #t] [_ #f]))
                (let ([w (get-style-prop parent-styles '__list-marker-inside-width 0)])
                  (if (number? w) w 0))
                0))
          ;; determine if child is inline-level content (text or inline-block)
          (define is-text-view? (match child-view [`(view-text . ,_) #t] [_ #f]))
          (define is-inline-content? (or is-text-view? is-inline-level-child?))
          ;; CSS 2.2 §9.5: for non-BFC inline content, left float intrusion shifts x
          (define inline-float-offset
            (if (and (not creates-bfc?) is-inline-content?)
                float-left-intrusion
                0))
          (define child-x
            (cond
              ;; text-align applies to inline-level content (text views and inline-block)
              [(and is-inline-content? (eq? text-align 'center) avail-w)
               (+ offset-x bfc-float-offset inline-float-offset indent-offset before-inline-w list-marker-w (max 0 (/ (- avail-w (view-width child-view)) 2)))]
              [(and is-inline-content? (eq? text-align 'right) avail-w)
               (+ offset-x bfc-float-offset inline-float-offset indent-offset before-inline-w list-marker-w (max 0 (- avail-w (view-width child-view))))]
              [is-inline-content?
               (+ offset-x bfc-float-offset inline-float-offset indent-offset before-inline-w list-marker-w
                  (box-model-margin-left child-bm))]
              [else
               ;; CSS 2.2 §10.3.3: resolve auto margins for block-level boxes in normal flow
               ;; when width is specified and margins are auto, distribute remaining space
               ;; NOTE: this only applies to block-level elements (display: block, list-item, table)
               ;; For inline-block (§10.3.9), auto margins compute to 0.
               (define child-display (get-style-prop child-styles 'display #f))
               (define is-block-level?
                 (or (eq? child-display 'block)
                     (eq? child-display 'list-item)
                     (eq? child-display 'table)
                     (eq? child-display 'flex)
                     (eq? child-display 'grid)
                     (not child-display)))  ;; default is block
               (define-values (raw-mt raw-mr raw-mb raw-ml) (get-raw-margins child-styles))
               (define ml-auto? (eq? raw-ml 'auto))
               (define mr-auto? (eq? raw-mr 'auto))
               ;; CSS 2.2 §10.3.3: direction of containing block affects over-constrained resolution
               (define parent-direction
                 (if parent-styles (get-style-prop parent-styles 'direction 'ltr) 'ltr))
               (define auto-margin-left
                 (cond
                   ;; auto margin resolution for block-level boxes
                   [(and avail-w is-block-level? (or ml-auto? mr-auto?))
                    (let* ([child-border-box-w (view-width child-view)]
                           [remaining (- avail-w child-border-box-w
                                       (if ml-auto? 0 (box-model-margin-left child-bm))
                                       (if mr-auto? 0 (box-model-margin-right child-bm)))])
                      (cond
                        [(and ml-auto? mr-auto?)
                         ;; both auto: split remaining space equally
                         (max 0 (/ remaining 2))]
                        [ml-auto?
                         ;; only left auto: absorb all remaining space
                         (max 0 remaining)]
                        [else
                         ;; only right auto: left margin stays as resolved
                         (box-model-margin-left child-bm)]))]
                   ;; CSS 2.2 §10.3.3: over-constrained in RTL — remaining space goes to margin-left
                   [(and avail-w is-block-level? (eq? parent-direction 'rtl)
                         (not ml-auto?) (not mr-auto?))
                    (let* ([child-border-box-w (view-width child-view)]
                           [used-ml (- avail-w child-border-box-w
                                      (box-model-margin-right child-bm))])
                      (max 0 used-ml))]
                   [else (box-model-margin-left child-bm)]))
               (+ offset-x bfc-float-offset auto-margin-left)]))
          ;; CSS 2.2 §9.5.2: when clearance is introduced (cleared-y > current-y),
          ;; the element's top border edge is placed at or below the float's bottom
          ;; outer edge. The clearance absorbs the margin — we use max() to pick
          ;; whichever is larger: the clear position or the normal margin position.
          ;; Only apply this when the element actually has a clear property AND
          ;; the clear position pushes it below the normal flow position.
          (define has-clearance?
            (and clear-val (not (eq? clear-val 'clear-none))
                 (> cleared-y current-y)))
          (define effective-block-y
            (if has-clearance?
                (max cleared-y (+ current-y collapsed-margin))
                (+ cleared-y collapsed-margin)))
          (define child-y (+ offset-y effective-block-y))

          ;; for text views with half-leading (line-height > font-metric),
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
          ;; for text children, the view reports font-metric dimensions:
          ;; height = font-metric-height (font-size * proportional-lh-ratio)
          ;; y = half-leading = (css-line-height - font-metric-height) / 2
          ;; The stacking height (line box) = height + 2*half-leading = css-line-height.
          ;; exception: font-size ≤ 0 → entire text is invisible, zero stacking height
          (define child-h
            (match child
              [`(text ,_ ,child-text-styles ,_ ,_)
               (let ([fs (get-style-prop child-text-styles 'font-size 10)])
                 (cond
                   [(and (number? fs) (<= fs 0)) 0]
                   ;; stacking = view-height + 2*view-y (half-leading on both sides)
                   [else (+ (view-height child-view) (* 2 (view-y child-view)))]))]
              ;; CSS 2.2 §10.8.1: inline-block non-replaced elements — the margin
              ;; box determines the line box height contribution.
              ;; CSS 2.2 §10.6.1: the line box height includes the strut (parent's
              ;; line-height). When vertical-align is baseline (default), the
              ;; inline-block bottom aligns with the text baseline, so the descent
              ;; below the baseline extends below the inline-block.
              ;; line-box-height = max(inline-block-margin-box-h, strut-line-height)
              ;; But with baseline alignment: height = ib-margin-box-h + descent
              ;; where descent = (line-height - ascender-height) for the parent font.
              [`(inline-block ,_ ,ib-styles ,_)
               (let* ([ib-h (+ (view-height child-view) (box-model-margin-bottom child-bm))]
                      ;; compute parent's strut descent:
                      ;; the amount below the baseline from the parent font's line-height
                      [parent-fs (if parent-styles (get-style-prop parent-styles 'font-size 16) 16)]
                      [parent-font-metrics (if parent-styles (get-style-prop parent-styles 'font-metrics #f) #f)]
                      ;; Times: ascender ratio = 891/1000, Arial: 1854/2048 ≈ 0.905
                      [is-proportional? (if parent-styles
                                            (let ([ft (get-style-prop parent-styles 'font-type #f)])
                                              (eq? ft 'proportional))
                                            #f)]
                      ;; for non-proportional (Ahem) font: no descent issue, strut = font-size
                      ;; for proportional fonts: compute descent below baseline
                      [strut-descent
                       (if is-proportional?
                           (let* ([is-arial? (eq? parent-font-metrics 'arial)]
                                  ;; line-height ratio
                                  [lh-ratio (if is-arial? 1.15 1.107)]
                                  ;; line-height = font-size * ratio
                                  [strut-lh (* parent-fs lh-ratio)]
                                  ;; ascender ratio: Times 891/1000, Arial ~905/1000
                                  [ascender-ratio (if is-arial? 0.905 0.891)]
                                  ;; descent from baseline = line-height - (ascender * font-size + half-leading)
                                  ;; half-leading = (line-height - font-size) / 2
                                  ;; descent below baseline = descender + half-leading
                                  ;; descender ratio: Times 216/1000, Arial ~212/1000
                                  [descender-ratio (if is-arial? 0.212 0.216)]
                                  [half-leading (/ (- strut-lh parent-fs) 2)]
                                  [descent (+ (* descender-ratio parent-fs) half-leading)])
                             descent)
                           0)]
                      ;; vertical-align: baseline (default) → inline-block bottom at baseline
                      ;; the descent extends below the inline-block
                      [va (get-style-prop ib-styles 'vertical-align 'baseline)])
                 (if (and is-proportional? (> strut-descent 0)
                          (or (eq? va 'baseline) (not va)))
                     ;; with baseline alignment: total height = ib-h + strut descent
                     (+ ib-h strut-descent)
                     ;; other vertical-align values: just margin-box height
                     ib-h))]
              [_ (view-height child-view)]))
          (define new-y (+ effective-block-y child-h))
          ;; track table-column/column-group children for height post-processing
          (define child-display-pre (get-style-prop child-styles-pre 'display #f))
          (define is-table-column?
            (or (eq? child-display-pre 'table-column)
                (eq? child-display-pre 'table-column-group)))
          (define child-box-id
            (match child
              [`(block ,id ,_ ,_) id]
              [`(inline ,id ,_ ,_) id]
              [`(inline-block ,id ,_ ,_) id]
              [_ #f]))

          (loop (cdr remaining)
                new-y
                ;; CSS 2.2 §8.3.1: inline-block margins don't collapse — bottom margin
                ;; is already included in child-h, so pass 0 to avoid double-counting
                (if is-inline-level-child? 0 (box-model-margin-bottom child-bm))
                (cons final-view views)
                #f
                (if (and is-table-column? child-box-id)
                    (cons child-box-id column-ids)
                    column-ids))])])))
  ) ;; close (if all-inline-children? ...)

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
