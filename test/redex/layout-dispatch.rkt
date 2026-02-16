#lang racket/base

;; layout-dispatch.rkt — Top-level layout dispatch
;;
;; Routes box types to their respective layout algorithms.
;; This is the single entry point for all layout operations.
;; Corresponds to Radiant's layout_block.cpp top-level dispatch.

(require racket/match
         racket/list
         racket/string
         "css-layout-lang.rkt"
         "layout-common.rkt"
         "layout-block.rkt"
         "layout-inline.rkt"
         "layout-flex.rkt"
         "layout-grid.rkt"
         "layout-positioned.rkt"
         "layout-intrinsic.rkt")

(provide layout
         layout-document)

;; ============================================================
;; Layout Dispatch — Main Entry Point
;; ============================================================

;; dispatch layout based on box type.
;; box: a Box term from css-layout-lang
;; avail: an AvailableSpace term: (avail AvailWidth AvailHeight)
;; returns: a View term
(define (layout box avail)
  (define styles (get-box-styles box))
  (define position (get-style-prop styles 'position 'static))

  ;; handle positioned elements (absolute/fixed are pulled from flow)
  ;; they are laid out by their containing block, not here.
  ;; for absolute/fixed, we call layout-positioned directly.

  (define view
    (cond
      ;; absolute or fixed: lay out via layout-positioned
      [(or (eq? position 'absolute) (eq? position 'fixed))
       (define avail-w (avail-width->number (cadr avail)))
       (define avail-h (avail-height->number (caddr avail)))
       (layout-positioned box (or avail-w 0) (or avail-h 0) layout)]
      [else
       (match box
      ;; display:none
      [`(none ,id)
       (make-empty-view id)]

      ;; block container
      [`(block ,id ,styles ,children)
       (layout-block box avail layout)]

      ;; inline container
      [`(inline ,id ,styles ,children)
       (layout-inline box avail layout)]

      ;; inline-block: lay out as block, placed inline
      [`(inline-block ,id ,styles ,children)
       ;; treat as block layout internally
       (layout-block `(block ,id ,styles ,children) avail layout)]

      ;; flex container
      [`(flex ,id ,styles ,children)
       (layout-flex box avail layout)]

      ;; grid container
      [`(grid ,id ,styles ,grid-def ,children)
       (layout-grid box avail layout)]

      ;; table container (simplified: treat as block)
      [`(table ,id ,styles ,table-children)
       (layout-table-simple box avail)]

      ;; text leaf
      [`(text ,id ,styles ,content ,measured-w)
       (layout-text box avail)]

      ;; replaced element (img, etc.)
      [`(replaced ,id ,styles ,intrinsic-w ,intrinsic-h)
       (layout-replaced box avail)]

      [_ (error 'layout "unknown box type: ~a" box)])]))

  ;; apply relative positioning offset
  ;; pass containing block dimensions for percentage resolution
  (if (eq? position 'relative)
      (let ([cb-w (avail-width->number (cadr avail))]
            [cb-h (avail-height->number (caddr avail))])
        (apply-relative-offset view styles cb-w cb-h))
      view))

;; ============================================================
;; Document-Level Layout
;; ============================================================

;; lay out a document root box at a given viewport size.
;; viewport-w, viewport-h in pixels.
(define (layout-document root-box viewport-w viewport-h)
  (define avail `(avail (definite ,viewport-w) (definite ,viewport-h)))
  (layout root-box avail))

;; ============================================================
;; Text Layout
;; ============================================================

(define (layout-text box avail)
  (match box
    [`(text ,id ,styles ,content ,measured-w)
     (define bm (extract-box-model styles))
     (define avail-w (avail-width->number (cadr avail)))

     ;; detect font type: Ahem (Taffy tests) vs proportional (CSS2.1 tests)
     (define font-type (get-style-prop styles 'font-type 'ahem))
     (define is-proportional? (eq? font-type 'proportional))

     ;; font metrics depend on font type
     (define font-size
       (cond
         [is-proportional? 16]
         ;; check for explicit font-size in styles (e.g., CSS2.1 tests with non-default Ahem size)
         [else (get-style-prop styles 'font-size 10)]))
     (define line-height (if is-proportional? 18 font-size))
     ;; character width categories for proportional fonts
     (define upper-w (if is-proportional? 8.5 font-size))
     (define lower-w (if is-proportional? 5.8 font-size))
     (define space-w-char (if is-proportional? 4.0 font-size))
     (define default-w (if is-proportional? 5.9 font-size))

     (cond
       ;; no wrapping needed: text fits or no constraint
       [(or (not avail-w) (<= measured-w avail-w))
        (make-text-view id 0 0 measured-w line-height content)]
       [else
        (cond
          ;; proportional font: split on spaces for word wrapping
          [is-proportional?
           (define words (string-split content " "))
           ;; compute word widths using per-character categories
           (define (word-width w)
             (for/fold ([total 0]) ([ch (in-string w)])
               (cond
                 [(char-upper-case? ch) (+ total upper-w)]
                 [(char-lower-case? ch) (+ total lower-w)]
                 [(char-numeric? ch) (+ total default-w)]
                 [else (+ total default-w)])))
           (define word-ws
             (for/list ([w (in-list words)])
               (word-width w)))
           (define space-w space-w-char)
           (define-values (num-lines max-line-w)
             (let loop ([remaining-words word-ws]
                        [line-w 0]
                        [lines 1]
                        [max-w 0])
               (cond
                 [(null? remaining-words)
                  (values lines (max max-w line-w))]
                 [else
                  (define ww (car remaining-words))
                  (define new-line-w
                    (if (= line-w 0) ww (+ line-w space-w ww)))
                  (cond
                    ;; first word on line always fits
                    [(= line-w 0)
                     (loop (cdr remaining-words) ww lines max-w)]
                    ;; word fits on current line
                    [(<= new-line-w avail-w)
                     (loop (cdr remaining-words) new-line-w lines max-w)]
                    ;; word doesn't fit → start new line
                    [else
                     (loop (cdr remaining-words) ww (+ lines 1) (max max-w line-w))])])))
           ;; for proportional text, report max line width (not container width)
           (define text-w max-line-w)
           (define text-h (* num-lines line-height))
           (make-text-view id 0 0 text-w text-h content)]

          ;; Ahem font: split on zero-width spaces
          [else
           (define words (string-split content "\u200B"))
           (define word-ws
             (for/list ([w (in-list words)])
               ;; each visible char = font-size px wide
               (for/sum ([ch (in-string w)])
                 (if (or (char=? ch #\u200B) (char=? ch #\u200C)
                         (char=? ch #\u200D) (char=? ch #\uFEFF))
                     0 font-size))))
           (define-values (num-lines max-line-w)
             (let loop ([remaining-words word-ws]
                        [line-w 0]
                        [lines 1]
                        [max-w 0])
               (cond
                 [(null? remaining-words)
                  (values lines (max max-w line-w))]
                 [else
                  (define ww (car remaining-words))
                  (define new-line-w (+ line-w ww))
                  (cond
                    ;; first word on line always fits
                    [(= line-w 0)
                     (loop (cdr remaining-words) new-line-w lines max-w)]
                    ;; word fits on current line
                    [(<= new-line-w avail-w)
                     (loop (cdr remaining-words) new-line-w lines max-w)]
                    ;; word doesn't fit → start new line
                    [else
                     (loop (cdr remaining-words) ww (+ lines 1) (max max-w line-w))])])))
           (define text-w (min measured-w (max max-line-w avail-w)))
           (define text-h (* num-lines line-height))
           (make-text-view id 0 0 text-w text-h content)])])]

    [_ (error 'layout-text "expected text box, got: ~a" box)]))

;; ============================================================
;; Replaced Element Layout (img, svg, etc.)
;; ============================================================

(define (layout-replaced box avail)
  (match box
    [`(replaced ,id ,styles ,intrinsic-w ,intrinsic-h)
     (define avail-w (avail-width->number (cadr avail)))
     (define avail-h (avail-height->number (caddr avail)))
     (define bm (extract-box-model styles avail-w))

     ;; resolve explicit width/height
     (define css-w (get-style-prop styles 'width 'auto))
     (define css-h (get-style-prop styles 'height 'auto))
     (define resolved-w (resolve-size-value css-w (or avail-w intrinsic-w)))
     (define resolved-h (resolve-size-value css-h (or avail-h intrinsic-h)))

     ;; determine final size maintaining aspect ratio
     (define aspect (if (> intrinsic-h 0)
                       (/ intrinsic-w intrinsic-h)
                       1))
     (define-values (final-w final-h)
       (cond
         ;; both specified
         [(and resolved-w resolved-h)
          (values resolved-w resolved-h)]
         ;; only width specified
         [resolved-w
          (values resolved-w (/ resolved-w aspect))]
         ;; only height specified
         [resolved-h
          (values (* resolved-h aspect) resolved-h)]
         ;; neither specified: use intrinsic
         [else
          (values intrinsic-w intrinsic-h)]))

     ;; apply min/max constraints
     (define min-w (or (resolve-size-value (get-style-prop styles 'min-width 'auto) avail-w) 0))
     (define max-w (or (resolve-size-value (get-style-prop styles 'max-width 'none) avail-w) +inf.0))
     (define min-h (or (resolve-size-value (get-style-prop styles 'min-height 'auto) avail-h) 0))
     (define max-h (or (resolve-size-value (get-style-prop styles 'max-height 'none) avail-h) +inf.0))

     (define clamped-w (max min-w (min max-w final-w)))
     (define clamped-h (max min-h (min max-h final-h)))

     (define border-box-w (compute-border-box-width bm clamped-w))
     (define border-box-h (compute-border-box-height bm clamped-h))

     (make-view id 0 0 border-box-w border-box-h '())]

    [_ (error 'layout-replaced "expected replaced box, got: ~a" box)]))

;; ============================================================
;; Table Layout (simplified)
;; ============================================================

;; simplified table: treat as vertical block stacking
;; a proper implementation would handle column sizing,
;; border-spacing, caption placement, etc.
(define (layout-table-simple box avail)
  (match box
    [`(table ,id ,styles (,row-groups ...))
     (define avail-w (avail-width->number (cadr avail)))
     (define bm (extract-box-model styles avail-w))
     (define content-w (if avail-w (resolve-block-width styles avail-w) 0))
     (define offset-x (+ (box-model-padding-left bm) (box-model-border-left bm)))
     (define offset-y (+ (box-model-padding-top bm) (box-model-border-top bm)))

     (define-values (views total-h)
       (let loop ([groups row-groups]
                  [y 0]
                  [acc '()])
         (cond
           [(null? groups) (values (reverse acc) y)]
           [else
            (define group (car groups))
            (define rows (extract-table-rows group))
            (define-values (row-views row-h)
              (layout-table-rows rows content-w y offset-x offset-y avail))
            (loop (cdr groups) (+ y row-h)
                  (append (reverse row-views) acc))])))

     (define border-box-w (compute-border-box-width bm content-w))
     (define border-box-h (compute-border-box-height bm total-h))
     (make-view id 0 0 border-box-w border-box-h views)]

    [_ (error 'layout-table-simple "expected table box, got: ~a" box)]))

(define (extract-table-rows group)
  (match group
    [`(row-group ,_ ,_ (,rows ...)) rows]
    [`(row ,_ ,_ ,_) (list group)]
    [_ '()]))

(define (layout-table-rows rows content-w y offset-x offset-y avail)
  (let loop ([remaining rows]
             [current-y y]
             [views '()])
    (cond
      [(null? remaining)
       (values (reverse views) (- current-y y))]
      [else
       (match (car remaining)
         [`(row ,row-id ,row-styles (,cells ...))
          (define num-cells (length cells))
          (define cell-w (if (> num-cells 0) (/ content-w num-cells) content-w))
          (define row-h 0)
          (define cell-views
            (for/list ([cell (in-list cells)]
                       [col (in-naturals)])
              (match cell
                [`(cell ,cell-id ,cell-styles ,colspan (,children ...))
                 (define cw (* cell-w (max 1 colspan)))
                 (define cell-box `(block ,cell-id ,cell-styles (,@children)))
                 (define cell-avail `(avail (definite ,cw) indefinite))
                 (define cell-view (layout cell-box cell-avail))
                 (define ch (view-height cell-view))
                 (set! row-h (max row-h ch))
                 (set-view-pos cell-view
                              (+ offset-x (* col cell-w))
                              (+ offset-y current-y))]
                [_ (make-empty-view 'cell)])))
          (loop (cdr remaining)
                (+ current-y row-h)
                (append (reverse cell-views) views))]
         [_ (loop (cdr remaining) current-y views)])])))

;; ============================================================
;; Helpers
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

(define (set-view-pos view x y)
  (match view
    [`(view ,id ,_ ,_ ,w ,h ,children ,baseline)
     `(view ,id ,x ,y ,w ,h ,children ,baseline)]
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))
